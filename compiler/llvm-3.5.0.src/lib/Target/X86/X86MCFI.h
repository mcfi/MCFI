#ifndef X86MCFI_H
#define X86MCFI_H

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86InstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

static bool RepOp(unsigned opcode) {
  switch (opcode) {
  case X86::REP_MOVSB_32:case X86::REP_MOVSB_64:
  case X86::REP_MOVSD_32:case X86::REP_MOVSD_64:
  case X86::REP_MOVSQ_64:case X86::REP_MOVSW_32:
  case X86::REP_MOVSW_64:case X86::REP_STOSB_32:
  case X86::REP_STOSB_64:case X86::REP_STOSD_32:
  case X86::REP_STOSD_64:case X86::REP_STOSQ_64:
  case X86::REP_STOSW_32:case X86::REP_STOSW_64:
    return true;
  }
  return false;
}

static unsigned XCHGOp(unsigned opcode)
{
  switch (opcode) {
  // xchg is special
  case X86::XCHG16rm: case X86::XCHG32rm:
  case X86::XCHG64rm: case X86::XCHG8rm:
  case X86::LXADD8:   case X86::LXADD16:
  case X86::LXADD32:  case X86::LXADD64:
    return 2;
  }
  return 0;
}

static void MCFIx64CheckMemWrite(MachineBasicBlock* MBB,
                                 MachineBasicBlock::iterator &MI,
                                 const TargetInstrInfo *TII,
                                 const unsigned MemOpOffset,
                                 const unsigned ScratchReg) {
  auto &MIB = BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(X86::LEA64r))
    .addReg(ScratchReg, RegState::Define);
  
  for (unsigned i = 0; i < 5; i++)
    MIB.addOperand(MI->getOperand(MemOpOffset+i));

  MIB->setSandboxCheck();
}

static void MCFIx64RewriteMemWrite(MachineBasicBlock* MBB,
                                   MachineBasicBlock::iterator &MI,
                                   const TargetInstrInfo *TII,
                                   const unsigned MemOpOffset,
                                   const unsigned ScratchReg) {
  const unsigned Opcode = MI->getOpcode();

  if (!MemOpOffset) {
    auto &MIB = BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(Opcode))
      .addReg(ScratchReg).addImm(0).addReg(0).addImm(0).addReg(0);
        
    for (unsigned i = 5; i < MI->getNumOperands(); i++) {
      if (MI->getOperand(i).isReg() && MI->getOperand(i).isImplicit())
        continue;
      MIB.addOperand(MI->getOperand(i));
    }
  } else {
    auto &MIB = BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(Opcode))
      .addOperand(MI->getOperand(0));
    for (unsigned i = 1; i < MemOpOffset; i++) {
      if (MI->getOperand(i).isReg() && MI->getOperand(i).isImplicit())
        continue;
      MIB.addOperand(MI->getOperand(i));
    }
    MIB.addReg(ScratchReg).addImm(0).addReg(0).addImm(0).addReg(0);
    for (unsigned i = MemOpOffset + 5; i < MI->getNumOperands(); i++) {
      if (MI->getOperand(i).isReg() && MI->getOperand(i).isImplicit())
        continue;
      MIB.addOperand(MI->getOperand(i));
    }
  }
  MI = MBB->erase(MI);
  MI = std::prev(MI);
  MI->setSandboxed();
}

#endif
