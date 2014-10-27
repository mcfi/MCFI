//===-- X86MCFI.cpp - Modular-Control-Flow Integrity Insrumentation ------===//
//
// By Ben Niu (niuben003@gmail.com)
//
//===----------------------------------------------------------------------===//
//
// This file defines the pass which instruments the machine code using MCFI.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "X86InstrBuilder.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "x86-mcfi"

STATISTIC(NumIndirectCall, "Number of instrumented indirect calls");
STATISTIC(NumReturns, "Number of instrumented returns");
STATISTIC(NumIndirectMemWrite, "Number of instrumented indirect memory writes");

namespace {
struct MCFI : public MachineFunctionPass {
  static char ID;
  MCFI() : MachineFunctionPass(ID) { }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  const char *getPassName() const override { return "X86 MCFI Instrumentation"; }

private:

  // 64-bit
  bool MCFIx64(MachineFunction &MF);  // standard AMD64, LP64
  void MCFIx64Ret(MachineFunction &MF, MachineBasicBlock *MBB,
                  MachineBasicBlock::iterator &MI, bool SmallID);

  void MCFIx64ICF(MachineFunction &MF, MachineBasicBlock *MBB,
                  MachineInstr *MI, bool SmallSandbox, bool SmallID);
  
  void MCFIx64IDCmp(MachineFunction &MF,
                    MachineBasicBlock *MBB,
                    unsigned BIDReg,
                    const unsigned TargetReg,
                    bool SmallID);

  void MCFIx64IJmp(MachineFunction &MF,
                                 MachineBasicBlock *MBB,
                                 const unsigned TargetReg);
  
  void MCFIx64IDValidityCheck(MachineFunction &MF,
                              MachineBasicBlock *MBB,
                              unsigned BIDReg,
                              unsigned TIDReg,
                              const unsigned TargetReg,
                              bool SmallID);
  
  void MCFIx64IDVersionCheck(MachineFunction &MF,
                             MachineBasicBlock *MBB,
                             unsigned BIDReg,
                             unsigned TIDReg,
                             bool SmallID);

  void MCFIx64Report(MachineFunction &MF,
                     MachineBasicBlock *MBB,
                     MachineBasicBlock *IDCmpMBB,
                     const unsigned TargetReg);
  
  bool MCFIx32(MachineFunction &MF);  // x86_64 with ILP32

  // 32-bit
  bool MCFIx86(MachineFunction &MF);  // x86_32
};
char MCFI::ID = 0;
}

FunctionPass *llvm::createX86MCFIInstrumentation() { return new MCFI(); }

bool MCFI::runOnMachineFunction(MachineFunction &MF) {
  // we only standard AMD64
  if (MF.getTarget().getSubtarget<X86Subtarget>().isTarget64BitLP64())
    return MCFIx64(MF);  
  else if (MF.getTarget().getSubtarget<X86Subtarget>().isTarget64BitILP32())
    return MCFIx32(MF);
  else if (MF.getTarget().getSubtarget<X86Subtarget>().is32Bit())
    return MCFIx86(MF);
  else {
    report_fatal_error("MCFI instrumentation is only available for AMD64 ABI!");
    return false;
  }
}

void MCFI::MCFIx64IDCmp(MachineFunction &MF,
                        MachineBasicBlock* MBB,
                        unsigned BIDReg,
                        const unsigned TargetReg,
                        bool SmallID) {
  DebugLoc DL;

  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();
  
  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);
  
  unsigned BIDRegReadOp = X86::MOV64rm;
  unsigned CmpOp = X86::CMP64mr;

  if (SmallID) {
    BIDReg = getX86SubSuperRegister(BIDReg, MVT::i32, true);
    BIDRegReadOp = X86::MOV32rm;
    CmpOp = X86::CMP32mr;
  }

  // mov %gs:0, BIDReg
  BuildMI(*MBB, I, DL, TII->get(BIDRegReadOp))
    .addReg(BIDReg, RegState::Define)
    .addReg(0).addImm(1).addReg(0).addImm(0).addReg(X86::GS);
  
  // cmp BIDReg, %gs:(TargetReg)
  BuildMI(*MBB, I, DL, TII->get(CmpOp))
    .addReg(TargetReg).addImm(1).addReg(0).addImm(0).addReg(X86::GS).addReg(BIDReg);
  // jne Lcheck
  BuildMI(*MBB, I, DL, TII->get(X86::JNE_4));
}

void MCFI::MCFIx64IJmp(MachineFunction &MF, MachineBasicBlock* MBB,
                       const unsigned TargetReg) {
  DebugLoc DL;
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);
  BuildMI(*MBB, I, DL, TII->get(X86::JMP64r)).addReg(TargetReg);
}

void MCFI::MCFIx64IDValidityCheck(MachineFunction &MF,
                                  MachineBasicBlock *MBB,
                                  unsigned BIDReg,
                                  unsigned TIDReg,
                                  const unsigned TargetReg,
                                  bool SmallID) {
  DebugLoc DL;
  
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(BIDReg);
  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);

  const unsigned TestOp = X86::TEST8ri;
  unsigned TIDRegReadOp = X86::MOV64rm;
  
  if (SmallID) {
    BIDReg = getX86SubSuperRegister(BIDReg, MVT::i32, true);
    TIDReg = getX86SubSuperRegister(TIDReg, MVT::i32, true);
    TIDRegReadOp = X86::MOV32rm;
  }

  // mov %gs:(TargetReg), %TIDReg
  BuildMI(*MBB, I, DL, TII->get(TIDRegReadOp))
    .addReg(TIDReg, RegState::Define)
    .addReg(TargetReg).addImm(1).addReg(0).addImm(0).addReg(X86::GS);
  // test $1, TIDReg
  BuildMI(*MBB, I, DL, TII->get(TestOp))
    .addReg(getX86SubSuperRegister(TIDReg, MVT::i8, false)).addImm(1);
  // jne Lreport
  BuildMI(*MBB, I, DL, TII->get(X86::JNE_4));
}

void MCFI::MCFIx64IDVersionCheck(MachineFunction &MF,
                                 MachineBasicBlock *MBB,
                                 unsigned BIDReg,
                                 unsigned TIDReg,
                                 bool SmallID) {
  DebugLoc DL;
  
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(BIDReg);
  MBB->addLiveIn(TIDReg);
  
  auto I = std::begin(*MBB);

  unsigned CmpOp = X86::CMP64rr;
    
  if (SmallID) {
    BIDReg = getX86SubSuperRegister(BIDReg, MVT::i16, true);
    TIDReg = getX86SubSuperRegister(TIDReg, MVT::i16, true);
    CmpOp = X86::CMP16rr;
  } else {
    BIDReg = getX86SubSuperRegister(BIDReg, MVT::i32, true);
    TIDReg = getX86SubSuperRegister(TIDReg, MVT::i32, true);
    CmpOp = X86::CMP32rr;    
  }

  // cmp BIDReg, TIDReg
  BuildMI(*MBB, I, DL, TII->get(CmpOp))
    .addReg(BIDReg).addReg(TIDReg);
  // jne Ltry
  BuildMI(*MBB, I, DL, TII->get(X86::JNE_4));
}

void MCFI::MCFIx64Report(MachineFunction &MF,
                         MachineBasicBlock *MBB,
                         MachineBasicBlock *IDCmpMBB,
                         const unsigned TargetReg) {
  DebugLoc DL;
  
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);

  if (TargetReg != X86::RSI) {
    BuildMI(*MBB, I, DL, TII->get(X86::MOV64rr))
      .addReg(X86::RSI).addReg(TargetReg);
  }
  // load the indirect branch address
  BuildMI(*MBB, I, DL, TII->get(X86::LEA64r))
    .addReg(X86::RDI, RegState::Define).addReg(0).addImm(1)
    .addReg(0).addMBB(IDCmpMBB).addReg(0);
 
  // hlt for now
  BuildMI(*MBB, I, DL, TII->get(X86::HLT));
}

void MCFI::MCFIx64Ret(MachineFunction &MF, MachineBasicBlock *MBB,
                      MachineBasicBlock::iterator &MI, bool SmallID) {
  ++NumReturns;
  MachineFunction::iterator MBBI;
 
  DebugLoc DL = MI->getDebugLoc();
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();
  // remove the return instruction
  MI = MBB->erase(MI); // MI points std::end(MBB)
  
  const unsigned BIDReg = X86::RDI;
  const unsigned TIDReg = X86::RSI;
  const unsigned TargetReg = X86::RCX;

  // popq %rcx
  BuildMI(*MBB, MI, DL, TII->get(X86::POP64r))
    .addReg(TargetReg, RegState::Define);

  // movl %ecx, %ecx
  BuildMI(*MBB, MI, DL, TII->get(X86::MOV32rr))
    .addReg(getX86SubSuperRegister(TargetReg, MVT::i32, true), RegState::Define)
    .addReg(getX86SubSuperRegister(TargetReg, MVT::i32, true), RegState::Undef)
    .addReg(TargetReg, RegState::ImplicitDefine);

  MachineBasicBlock* IDCmpMBB = MF.CreateMachineBasicBlock();
  MBBI = MBB;
  MF.insert(++MBBI, IDCmpMBB); // original MBB to ICCmpMBB, fallthrough
  MBB->addSuccessor(IDCmpMBB);
  MCFIx64IDCmp(MF, IDCmpMBB, BIDReg, TargetReg, SmallID); // fill the IDCmp block
  
  MachineBasicBlock* IJmpMBB = MF.CreateMachineBasicBlock();
  MBBI = IDCmpMBB;
  MF.insert(++MBBI, IJmpMBB); // fall through IJmpMBB
  IDCmpMBB->addSuccessor(IJmpMBB);
  MCFIx64IJmp(MF, IJmpMBB, TargetReg); // fill IJmpMBB
  
  MachineBasicBlock* IDValidityCheckMBB = MF.CreateMachineBasicBlock();
  MF.push_back(IDValidityCheckMBB);
  MCFIx64IDValidityCheck(MF, IDValidityCheckMBB, BIDReg,
                         TIDReg, TargetReg, SmallID); // fill IDValCheck MBB
  
  IDCmpMBB->addSuccessor(IDValidityCheckMBB, UINT_MAX); // as far as possible :)

  MachineBasicBlock* VerCheckMBB = MF.CreateMachineBasicBlock();
  MF.push_back(VerCheckMBB);
  MCFIx64IDVersionCheck(MF, VerCheckMBB, BIDReg, TIDReg, SmallID); // fill VerCheckMBB

  MachineBasicBlock* ReportMBB = MF.CreateMachineBasicBlock();
  MBBI = VerCheckMBB;
  MF.insert(++MBBI, ReportMBB);
  MCFIx64Report(MF, ReportMBB, IDCmpMBB, TargetReg); // fill report MBB
  
  IDValidityCheckMBB->addSuccessor(VerCheckMBB);
  IDValidityCheckMBB->addSuccessor(ReportMBB);

  VerCheckMBB->addSuccessor(IDCmpMBB);
  VerCheckMBB->addSuccessor(ReportMBB);

  MachineInstrBuilder(MF, &IDCmpMBB->back()).addMBB(IDValidityCheckMBB);
  MachineInstrBuilder(MF, &IDValidityCheckMBB->back()).addMBB(VerCheckMBB);
  MachineInstrBuilder(MF, &VerCheckMBB->back()).addMBB(IDCmpMBB);

  MF.dump();
}

bool MCFI::MCFIx64(MachineFunction &MF) {
  const TargetMachine& TM = MF.getTarget();
  const TargetRegisterInfo *TRI = TM.getRegisterInfo();
  const Module* M = MF.getMMI().getModule();
  const bool SmallSandbox = !M->getNamedMetadata("MCFILargeSandbox");
  const bool SmallID = !M->getNamedMetadata("MCFILargeID");

  for (auto MBB = std::begin(MF); MBB != std::end(MF); MBB++) {
    for (auto MI = std::begin(*MBB); MI != std::end(*MBB); MI++) {
      if (MI->getOpcode() == X86::RETQ) { // return instruction
        MCFIx64Ret(MF, MBB, MI, SmallID);
        break;
      } else if (MI->getIRInst()) { // indirect branch
      } else if (MI->mayStore() && !MI->memoperands_empty()) { // indirect memory write
      } else if (MI->modifiesRegister(X86::RSP, TRI)) { // RSP register modification
      } else {} // do nothing
    }
  }
  return false;
}

bool MCFI::MCFIx32(MachineFunction &MF) {
  report_fatal_error("MCFI instrumentation for x32 ABI has not been implemented yet!");
  return false;
}

bool MCFI::MCFIx86(MachineFunction &MF) {
  report_fatal_error("MCFI instrumentation for x86 has not been implemented yet!");  
  return false;
}
