//===-- X86AsmPrinter.h - X86 implementation of AsmPrinter ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef X86ASMPRINTER_H
#define X86ASMPRINTER_H

#include "X86Subtarget.h"
#include "llvm/IR/Module.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class MCStreamer;
class MCSymbol;

class LLVM_LIBRARY_VISIBILITY X86AsmPrinter : public AsmPrinter {
  const X86Subtarget *Subtarget;
  StackMaps SM;

  void GenerateExportDirective(const MCSymbol *Sym, bool IsData);

  const Module *M;
  bool SmallSandbox;
  bool SmallID;
  std::set<StringRef> NoReturnFunctions;
  std::set<std::string> AddrTakenFunctionsInCode;
  std::set<std::string> AddrTakenFunctions;
  std::set<std::string> AllFunctions;

  // test if ID is an identifier in C
  bool isID(std::string &ID) {
    if (ID[0] != '_' && !isalpha(ID[0]))
      return false;
    /* A function's address may be taken in expressions
       such as func@GOTPCREL*/
    size_t at_sym = ID.find_first_of('@');
    if (at_sym != std::string::npos) {
      ID = ID.substr(0, at_sym);
    }
    for (const auto c : ID) {
      if (c != '_' && !isalnum(c))
        return false;
    }
    return true;
  }

  bool isNoReturnFunction(const MachineOperand &MO) {
    if (MO.isSymbol()) {
      if (NoReturnFunctions.find(MO.getSymbolName()) !=
          NoReturnFunctions.end())
        return true;
    }
    if (MO.isGlobal() && MO.getGlobal()->hasName()) {
      if (NoReturnFunctions.find(MO.getGlobal()->getName()) !=
          NoReturnFunctions.end())
        return true;
    }
    return false;
  }

 public:
  explicit X86AsmPrinter(TargetMachine &TM, MCStreamer &Streamer)
    : AsmPrinter(TM, Streamer), SM(*this) {
    Subtarget = &TM.getSubtarget<X86Subtarget>();
    M = nullptr;
  }

  const char *getPassName() const override {
    return "X86 Assembly / Object Emitter";
  }

  const X86Subtarget &getSubtarget() const { return *Subtarget; }

  void EmitStartOfAsmFile(Module &M) override;

  void EmitEndOfAsmFile(Module &M) override;

  void EmitBasicBlockStart(const MachineBasicBlock &MBB) const;

  void EmitMCFIInfo(const StringRef SectName, const Module& M);
    
  void EmitInstruction(const MachineInstr *MI) override;

  void EmitMCFIPadding(const MachineInstr *MI) override;

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       unsigned AsmVariant, const char *ExtraCode,
                       raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             unsigned AsmVariant, const char *ExtraCode,
                             raw_ostream &OS) override;

  void EmitInlineAsmInstrumentation(StringRef Str, const MDNode *LocMDNode,
                                    InlineAsm::AsmDialect Dialect) override;
  
  /// \brief Return the symbol for the specified constant pool entry.
  MCSymbol *GetCPISymbol(unsigned CPID) const override;

  bool runOnMachineFunction(MachineFunction &F) override;
};

} // end namespace llvm

#endif
