//===-- X86MCFIRegReserve.cpp - PhysRegister Reservation for MCFI ------===//
//
// By Ben Niu (niuben003@gmail.com)
//
//===----------------------------------------------------------------------===//
//
// This file defines the pass that reserves scratch registers for memory
// sandboxing.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86MCFI.h"
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

#define DEBUG_TYPE "x86-mcfiregreserve"

namespace {
struct MCFIRegReserve : public MachineFunctionPass {
  static char ID;

  MCFIRegReserve() : MachineFunctionPass(ID) { M = nullptr; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  const char *getPassName() const override { return "X86 MCFI Register Reservation"; }

private:
  bool SmallSandbox;
  const Module *M;

  bool MCFIRRx64Small(MachineFunction &MF);  // LP64 or ILP32 (x32)
  bool MCFIRRx64Large(MachineFunction &MF);  // LP64 or ILP32 (x32)  
};
char MCFIRegReserve::ID = 0;
}

FunctionPass *llvm::createX86MCFIRegReserve() { return new MCFIRegReserve(); }

bool MCFIRegReserve::runOnMachineFunction(MachineFunction &MF) {
  const Module* newM = MF.getMMI().getModule();
  if (M != newM) {
    M = newM;
    SmallSandbox = !M->getNamedMetadata("MCFILargeSandbox");
  }

  // AMD64
  if (MF.getTarget().getSubtarget<X86Subtarget>().is64Bit()) {
    if (SmallSandbox)
      MCFIRRx64Small(MF);
    else
      MCFIRRx64Large(MF);
  }
  // x86
  return false;
}

bool MCFIRegReserve::MCFIRRx64Small(MachineFunction &MF) {
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();
  const TargetLowering *TLI = MF.getTarget().getTargetLowering();
  const TargetRegisterClass *RC = TLI->getRegClassFor(MVT::i64);
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // No xchg-like functions are possible to appear here, because
  // before register allocation, the machine code is in SSA form.
  for (auto MBB = std::begin(MF); MBB != std::end(MF); MBB++) {
    for (auto MI = std::begin(*MBB); MI != std::end(*MBB); MI++) {
      if (MI->mayStore() && MI->getNumOperands() >= 5 &&
          !MI->isBranch() && !MI->isInlineAsm()) {
        const auto Opcode = MI->getOpcode();
        
        if (RepOp(Opcode)) // in-place sandboxing
          continue;
        
        const unsigned MemOpOffset = XCHGOp(Opcode);
        const auto BaseReg = MI->getOperand(MemOpOffset).isReg() ?
          MI->getOperand(MemOpOffset).getReg() : 0;
        const auto IndexReg = MI->getOperand(MemOpOffset+2).getReg();
        const auto Offset = MI->getOperand(MemOpOffset+3).isImm() ?
          MI->getOperand(MemOpOffset+3).getImm() : -1;

        if (BaseReg == X86::RIP ||         // PC-relative
            (BaseReg == 0 && IndexReg == 0)||   // direct memory write
            (BaseReg != 0 && IndexReg == 0 && isMagicOffset(Offset))) // in-place sandboxing
          continue;

        const auto ScratchReg = MRI.createVirtualRegister(RC);
        MCFIx64CheckMemWrite(MBB, MI, TII, MemOpOffset, ScratchReg, false);
        MCFIx64RewriteMemWrite(MBB, MI, TII, MemOpOffset, ScratchReg, false);
      }
    }
  }
  return false;
}

bool MCFIRegReserve::MCFIRRx64Large(MachineFunction &MF) {
  return false;
}
