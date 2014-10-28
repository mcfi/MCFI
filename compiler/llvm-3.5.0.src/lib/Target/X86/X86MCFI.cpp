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
#include "llvm/CodeGen/LiveVariables.h"
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
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  const char *getPassName() const override { return "X86 MCFI Instrumentation"; }

private:

  // 64-bit
  bool MCFIx64(MachineFunction &MF);  // standard AMD64, LP64
  void MCFIx64MBBs(MachineFunction &MF,
                   MachineBasicBlock *MBB,
                   const unsigned BIDReg,
                   const unsigned TIDReg,
                   const unsigned TargetReg,
                   MachineBasicBlock *&IDCmpMBB,
                   MachineBasicBlock *&ICJMBB,
                   const unsigned CJOp,
                   MachineBasicBlock *&IDValidityCheckMBB,
                   MachineBasicBlock *&VerCheckMBB,
                   MachineBasicBlock *&ReportMBB,
                   bool SmallID);

  void MCFIx64Ret(MachineFunction &MF, MachineBasicBlock *MBB,
                  MachineBasicBlock::iterator &MI, bool SmallID);
  void MCFIx64IndirectCall(MachineFunction &MF, MachineBasicBlock *MBB,
                           MachineBasicBlock::iterator &MI, bool SmallID);
  void MCFIx64IndirectMemWrite(MachineFunction &MF, MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator &MI, bool SmallID);
  void MCFIx64StackPointer(MachineFunction &MF, MachineBasicBlock *MBB,
                           MachineBasicBlock::iterator &MI);

  void MCFIx64ICF(MachineFunction &MF, MachineBasicBlock *MBB,
                  MachineInstr *MI, bool SmallSandbox, bool SmallID);
  
  void MCFIx64IDCmp(MachineFunction &MF,
                    MachineBasicBlock *MBB,
                    unsigned BIDReg,
                    const unsigned TargetReg,
                    bool SmallID);

  void MCFIx64ICJ(MachineFunction &MF,
                  MachineBasicBlock *MBB,
                  const unsigned CJOp,
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

void MCFI::MCFIx64ICJ(MachineFunction &MF, MachineBasicBlock* MBB,
                      const unsigned CJOp, const unsigned TargetReg) {
  DebugLoc DL;
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);
  BuildMI(*MBB, I, DL, TII->get(CJOp)).addReg(TargetReg);
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

void MCFI::MCFIx64MBBs(MachineFunction &MF,
                       MachineBasicBlock *MBB,
                       const unsigned BIDReg,
                       const unsigned TIDReg,
                       const unsigned TargetReg,
                       MachineBasicBlock *&IDCmpMBB,
                       MachineBasicBlock *&ICJMBB,
                       const unsigned CJOp,
                       MachineBasicBlock *&IDValidityCheckMBB,
                       MachineBasicBlock *&VerCheckMBB,
                       MachineBasicBlock *&ReportMBB,
                       bool SmallID) {
  MachineFunction::iterator MBBI;
  
  IDCmpMBB = MF.CreateMachineBasicBlock();
  MBBI = MBB;
  MF.insert(++MBBI, IDCmpMBB); // original MBB to ICCmpMBB, fallthrough
  
  MCFIx64IDCmp(MF, IDCmpMBB, BIDReg, TargetReg, SmallID); // fill the IDCmp block
  
  ICJMBB = MF.CreateMachineBasicBlock();
  MBBI = IDCmpMBB;
  MF.insert(++MBBI, ICJMBB); // fall through ICJMBB
  IDCmpMBB->addSuccessor(ICJMBB);
  MCFIx64ICJ(MF, ICJMBB, CJOp, TargetReg); // fill ICJMBB
  
  IDValidityCheckMBB = MF.CreateMachineBasicBlock();
  MF.push_back(IDValidityCheckMBB);
  MCFIx64IDValidityCheck(MF, IDValidityCheckMBB, BIDReg,
                         TIDReg, TargetReg, SmallID); // fill IDValCheck MBB
  
  IDCmpMBB->addSuccessor(IDValidityCheckMBB, UINT_MAX); // as far as possible

  VerCheckMBB = MF.CreateMachineBasicBlock();
  MF.push_back(VerCheckMBB);
  MCFIx64IDVersionCheck(MF, VerCheckMBB, BIDReg, TIDReg, SmallID); // fill VerCheckMBB

  ReportMBB = MF.CreateMachineBasicBlock();
  MBBI = VerCheckMBB;
  MF.insert(++MBBI, ReportMBB);
  MCFIx64Report(MF, ReportMBB, IDCmpMBB, TargetReg); // fill report MBB
  
  IDValidityCheckMBB->addSuccessor(VerCheckMBB);
  IDValidityCheckMBB->addSuccessor(ReportMBB);

  VerCheckMBB->addSuccessor(IDCmpMBB);
  VerCheckMBB->addSuccessor(ReportMBB);

  // transfer MBB's current successor to ICJMBB
  ICJMBB->transferSuccessors(MBB);

  // now MBB should be followed by IDCmpMBB
  MBB->addSuccessor(IDCmpMBB);
  
  MachineInstrBuilder(MF, &IDCmpMBB->instr_back()).addMBB(IDValidityCheckMBB);
  MachineInstrBuilder(MF, &IDValidityCheckMBB->instr_back()).addMBB(ReportMBB);
  MachineInstrBuilder(MF, &VerCheckMBB->instr_back()).addMBB(IDCmpMBB);
}

void MCFI::MCFIx64Ret(MachineFunction &MF, MachineBasicBlock *MBB,
                      MachineBasicBlock::iterator &MI, bool SmallID) {
  ++NumReturns;

  DebugLoc DL = MI->getDebugLoc();
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

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

  // remove the return instruction
  MI = MBB->erase(MI); // MI points std::end(MBB)

  MachineBasicBlock *IDCmpMBB, *ICJMBB,
    *IDValidityCheckMBB, *VerCheckMBB, *ReportMBB;

  MCFIx64MBBs(MF, MBB, BIDReg, TIDReg, TargetReg, IDCmpMBB, ICJMBB, X86::JMP64r,
              IDValidityCheckMBB, VerCheckMBB, ReportMBB, SmallID);
}

// get a general register that is neither Reg1 nor Reg2
static unsigned getX64ScratchReg(const unsigned Reg1, const unsigned Reg2) {
  static const std::set<unsigned> GRegs = {
    X86::RAX, X86::RBX, X86::RCX, X86::RDX, X86::RDI, X86::RSI, X86::R8,
    X86::R9, X86::R10, X86::R11, X86::R12, X86::R13, X86::R14, X86::R15
  };
  for (auto reg : GRegs)
    if (Reg1 != reg && Reg2 != reg)
      return reg;
  llvm_unreachable("getX64ScratchReg does not find scratch registers.");
}

void MCFI::MCFIx64IndirectCall(MachineFunction &MF, MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator &MI, bool SmallID) {
  ++NumIndirectCall;
  MachineFunction::iterator MBBI;
  const Instruction* I = MI->getIRInst();
  const unsigned CJOp =
    (MI->getOpcode() == X86::CALL64r ||
     MI->getOpcode() == X86::CALL64m) ? X86::CALL64r : X86::JMP64r;

  DebugLoc DL = MI->getDebugLoc();

  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  // MCFI needs three scratch registers.
  std::set<unsigned> ScratchRegs = {
    X86::RAX, X86::RCX, X86::RDX, X86::RDI,
    X86::RSI, X86::R8, X86::R9, X86::R10};

  for (auto it = MI->operands_begin(); it != MI->operands_end(); it++) {
    if (it->isReg() && it->isUse() && it->getReg() != X86::RIP && (
          X86::GR32RegClass.contains(it->getReg()) ||
          X86::GR64RegClass.contains(it->getReg()) ||
          X86::GR8RegClass.contains(it->getReg()))) {
      ScratchRegs.erase(getX86SubSuperRegister(it->getReg(), MVT::i64, true));
    }
  }

  // R11 must be available even if no other GRegs are available
  ScratchRegs.insert(X86::R11);

  unsigned TargetReg;
  if (MI->getOpcode() == X86::JMP64r || MI->getOpcode() == X86::CALL64r) {
    TargetReg = MI->getOperand(0).getReg();
    // search back the basic block and its predecessor to see if TargetReg
    // is defined in this basic block. If negative, then we sandbox it.
    bool DefFound = false;
    MachineBasicBlock *DefMBB = MBB;
    auto RIT = MBB->instr_rbegin(); // points to MI;
    const TargetRegisterInfo *TRI = MF.getTarget().getRegisterInfo();
    for (++RIT; RIT != MBB->instr_rend(); RIT++) {
      if (RIT->definesRegister(TargetReg, TRI)) {
        DefFound = true;
        break;
      }
    }
    if (!DefFound && MBB->pred_size() == 1) {
      // often the definition of the target register is in MBB's previous
      // BB for implementing if (target != 0) *target;
      DefMBB = *MBB->pred_begin();
      for (RIT = DefMBB->instr_rbegin(); RIT != DefMBB->instr_rend(); RIT++) {
        if (RIT->definesRegister(TargetReg, TRI)) {
          DefFound = true;
          break;
        }
      }
    }
    if (DefFound) {
      MachineBasicBlock::iterator DefI(*RIT);
      if (DefI->getOpcode() == X86::MOV64rm) {
        auto &MIB = BuildMI(*DefMBB, DefI, DL, TII->get(X86::MOV32rm))
          .addReg(getX86SubSuperRegister(TargetReg, MVT::i32, true), RegState::Define);
        for (auto idx = 1; idx < 6; idx++) // 5 machineoperands
          MIB.addOperand(DefI->getOperand(idx));
        DefMBB->erase(DefI);
      } else {
        DefFound = false; // be safe!
      }
    }

    if (!DefFound) {
      BuildMI(*MBB, MI, DL, TII->get(X86::MOV32rr))
        .addReg(getX86SubSuperRegister(TargetReg, MVT::i32, true), RegState::Define)
        .addReg(getX86SubSuperRegister(TargetReg, MVT::i32, true), RegState::Undef);
    }
  } else { // JMP64m or CALL64m
    TargetReg = *ScratchRegs.begin();
    ScratchRegs.erase(TargetReg);
    // mov MemOperand, TargetReg
    auto &MIB = BuildMI(*MBB, MI, DL, TII->get(X86::MOV32rm))
      .addReg(getX86SubSuperRegister(TargetReg, MVT::i32, true), RegState::Define);
    /* testing-use
    auto &MIB = BuildMI(*MBB, MI, DL, TII->get(X86::MOV64rm))
      .addReg(TargetReg, RegState::Define);
    */
    for (auto idx = 0; idx < 5; idx++) // a memory operand consists of 5 machineoperands
      MIB.addOperand(MI->getOperand(idx));
  }
  // remove IBMI
  MI = MBB->erase(MI);

  unsigned BIDRegXMM = 0;
  unsigned TIDRegXMM = 0;
  unsigned BIDReg;
  unsigned TIDReg;

  if (ScratchRegs.size() >= 2) {
    BIDReg = *ScratchRegs.begin();
    ScratchRegs.erase(BIDReg);
    TIDReg = *ScratchRegs.begin();
  } else if (ScratchRegs.size() == 1) {
    BIDReg = *ScratchRegs.begin();
    TIDReg = getX64ScratchReg(TargetReg, BIDReg);
    TIDRegXMM = X86::XMM15;
  } else { // no scratch register at all!!
    BIDReg = getX64ScratchReg(TargetReg, TargetReg);
    BIDRegXMM = X86::XMM14;
    TIDReg = getX64ScratchReg(TargetReg, BIDReg);
    TIDRegXMM = X86::XMM15;
  }
  // build the MCFI basic blocks
  MachineBasicBlock *IDCmpMBB, *ICJMBB,
    *IDValidityCheckMBB, *VerCheckMBB, *ReportMBB;

  MCFIx64MBBs(MF, MBB, BIDReg, TIDReg, TargetReg, IDCmpMBB, ICJMBB, CJOp,
              IDValidityCheckMBB, VerCheckMBB, ReportMBB, SmallID);

  if (BIDRegXMM) {
    BuildMI(*MBB, MI, DL, TII->get(X86::MOV64toPQIrr))
      .addReg(BIDRegXMM, RegState::Define).addReg(BIDReg);
    BuildMI(*ICJMBB, std::begin(*ICJMBB), DL, TII->get(X86::MOVPQIto64rr))
      .addReg(BIDReg, RegState::Define).addReg(BIDRegXMM);
  }

  if (TIDRegXMM) {
    BuildMI(*IDValidityCheckMBB, std::begin(*IDValidityCheckMBB),
            DL, TII->get(X86::MOV64toPQIrr))
      .addReg(TIDRegXMM, RegState::Define).addReg(TIDReg);
    BuildMI(*ICJMBB, std::begin(*ICJMBB), DL, TII->get(X86::MOVPQIto64rr))
      .addReg(TIDReg, RegState::Define).addReg(TIDRegXMM);
  }

  IDCmpMBB->instr_front().setIRInst(I); // transfer the IR to the BID read instruction.
}

void MCFI::MCFIx64IndirectMemWrite(MachineFunction &MF, MachineBasicBlock *MBB,
                                   MachineBasicBlock::iterator &MI, bool SmallID) {
  ++NumIndirectMemWrite;
}

void MCFI::MCFIx64StackPointer(MachineFunction &MF, MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator &MI) {
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
      } else if (MI->getIRInst()) {
        const unsigned op = MI->getOpcode();
        switch (op) {
        case X86::CALL64m:
        case X86::CALL64r:
        {
          MachineBasicBlock::iterator tmpMI = MI;
          if (++tmpMI != std::end(*MBB)) {
            // split the current basic block into two and make the CALL64[rm]
            // instruction a terminator of MBB
            MachineBasicBlock* newMBB = MF.CreateMachineBasicBlock();
            MachineFunction::iterator MBBI = MBB;
            MF.insert(++MBBI, newMBB);
            newMBB->transferSuccessors(MBB);
            MBB->addSuccessor(newMBB);
            newMBB->splice(std::begin(*newMBB), MBB, tmpMI, std::end(*MBB));
          }
        }
        case X86::JMP64m:
        case X86::JMP64r:
        {
          MCFIx64IndirectCall(MF, MBB, MI, SmallID);
          break;
        }
        }
      } else if (MI->mayStore() && !MI->memoperands_empty()) { // indirect memory write
        MCFIx64IndirectMemWrite(MF, MBB, MI, SmallSandbox);
      } else if (MI->definesRegister(X86::RSP, TRI)) { // RSP register modification
        MCFIx64StackPointer(MF, MBB, MI);
      }
      if (MI == std::end(*MBB)) break;
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
