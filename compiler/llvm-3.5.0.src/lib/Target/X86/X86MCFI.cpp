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
#include "X86MCFI.h"
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
#include "llvm/CodeGen/MachineBasicBlock.h"
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
#include <random>
#include <vector>
#include <cxxabi.h>

using namespace llvm;

#define DEBUG_TYPE "x86-mcfi"

STATISTIC(NumIndirectCall, "Number of instrumented indirect calls");
STATISTIC(NumReturns, "Number of instrumented returns");

namespace {
struct MCFI : public MachineFunctionPass {
  static char ID;

  // Each indirect call/jmp or return has its own Bary Slot.
  static unsigned BarySlot;

  MCFI() : MachineFunctionPass(ID) { M = nullptr; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  const char *getPassName() const override { return "X86 MCFI Instrumentation"; }

private:
  bool SmallSandbox;
  bool SmallID;
  Module *M;
  unsigned long ModuleID;
  std::set<StringRef> GlobalCtors;
  std::set<StringRef> GlobalDtors;
  
  std::vector<std::string> Returns;
  std::vector<std::string> IndirectTailCalls;
  std::vector<std::string> DirectTailCalls;

  const char *__report_cfi_violation_for_return = "__report_cfi_violation_for_return";
  const char *__report_cfi_violation = "__report_cfi_violation";

  std::vector<std::string> IndirectCalls;

  void extractGlobalArray(const StringRef ArrayName,
                          std::set<StringRef> &StrSet);

  const std::string CXXDemangledName(const char* MangledName) const {
    int status = 0;
    char* result = abi::__cxa_demangle(MangledName, 0, 0, &status);

    if (result) {
      const std::string DemangledName(result);
      free(result);
      size_t Start = 0;
      const std::string VirtualThunk("virtual thunk to ");
      const std::string NonVirtualThunk("non-virtual thunk to ");
      // for thunks, we don't output the annoying "virtual thunk to" things
      // we match the longer NonVirtualThunk first.
      if (DemangledName.find(NonVirtualThunk) != std::string::npos) {
        Start = NonVirtualThunk.size();
      } else if (DemangledName.find(VirtualThunk) != std::string::npos) {
        Start = VirtualThunk.size();
      }
      return DemangledName.substr(Start);
    }
    return std::string("");
  }

  const std::string SanitizeTypeStr(const std::string TStr,
                                    const std::string StructStr) const {
    std::string postpro_result;
    size_t pos;
    size_t resultPos = 0;
    do {
      pos = TStr.find(StructStr, resultPos);
      if (pos == std::string::npos)
        break;
      pos += StructStr.size();
      while (pos < TStr.size() &&
             (isalnum(TStr[pos]) || TStr[pos] == '_' || TStr[pos] == ':'))
        ++pos;// advance until the end of the id
      if (pos >= TStr.size())
        break;
      postpro_result += TStr.substr(resultPos, pos);
      if (TStr[pos] == '.') {
        while (pos < TStr.size() &&
               (TStr[pos] == '.' || isalnum(TStr[pos])))
          ++pos;
      }
      resultPos = pos;
    } while (true);
    return postpro_result + TStr.substr(resultPos);
  }

  const std::string TypeStr(const Type *Ty) const {
    std::string result;
    raw_string_ostream TStr(result);
    Ty->print(TStr);
    TStr.flush();
    // Clang emits different types identifiers for the same struct
    // For example, struct.op.1324 and struct.op.1423 are actually the
    // same struct definition, but compiled in different files. We need
    // to process the type string to remove the trailing digits.
    result = SanitizeTypeStr(result, "%struct.");
    result = SanitizeTypeStr(result, "%\"class.");
    return result;
  }

  const std::string FuncTypeStr(const FunctionType *FTy) const {
    std::string FTStr;
    FTStr += TypeStr(FTy->getReturnType()) + '!';
    for (FunctionType::param_iterator PI = FTy->param_begin();
         PI != FTy->param_end(); ++PI) {
      FTStr += TypeStr(*PI) + '@';
    }
    if (FTy->isVarArg()) {
      FTStr += '$';
    }
    return FTStr;
  }

  const std::string splitMethodName(const std::string MethodName) const {
    size_t i;
    // go back until the ')' symbol
    for (i = MethodName.size() - 1; i > 0 && MethodName[i] != ')'; i--)
      ;
    size_t LeftParenToClose = 1;
    for (--i; i > 0; i--) {
      if (MethodName[i] == ')')
        ++LeftParenToClose;
      else if (MethodName[i] == '(') {
        if (0 == --LeftParenToClose)
          break;
      }
    }
    // we get to the method name, but we need to bypass the template types
    for (--i; i > 0 && isspace(MethodName[i]); i--)
      ;
    if (MethodName[i] == '>') {
      LeftParenToClose = 1;
      for (--i; i > 0; i--) {
        if (MethodName[i] == '>')
          ++LeftParenToClose;
        else if (MethodName[i] == '<') {
          if (0 == --LeftParenToClose)
            break;
        }
      }
    }
    for (; i > 0 && MethodName[i] != ':'; i--)
      ;
    return MethodName.substr(0, i-1) + "#" + MethodName.substr(i+1);
  }
  
  void MCFIFuncInfo(const MachineFunction &MF) {
    StringRef FuncName = MF.getName();
    const Function *F = MF.getFunction();
    std::string FuncInfo("{ ");
    FuncInfo += FuncName.str() + '\n';

    /// We emit C++ constructor's demangled name
    if (F->hasFnAttribute(Attribute::CXXCtor)) {
      // if the class does not contain any virtual methods, don't
      // emit any other information.
      if (!F->hasFnAttribute(Attribute::NoInline))
        goto NoFuncInfoEmit;
      std::string DemangledName = CXXDemangledName(FuncName.data());
      if (DemangledName.size()) {
        DemangledName = splitMethodName(DemangledName);
        size_t Sharp = DemangledName.find_last_of('#');
        //llvm::errs() << DemangledName.substr(0, Sharp) << "\n";
        FuncInfo += std::string("C ") + DemangledName.substr(0, Sharp) + '\n';
        goto NoFuncInfoEmit;
      } else {
        llvm::errs() << FuncName.data() << "\n";
        exit(-1);
      }
    }
    /// Other methods, including instance methods and destructors, should have
    /// their demangled name output, so that class hierarchy reconstruction
    /// can be done.
    if (F->hasFnAttribute(Attribute::CXXInstanceMethod)) {
      std::string DemangledName = CXXDemangledName(FuncName.data());
      if (DemangledName.size()) {
        size_t Tilde = DemangledName.find_last_of('~');
        if (Tilde == std::string::npos) {
          FuncInfo += std::string("N ") + splitMethodName(DemangledName) + '\n';
        } else {
          FuncInfo += std::string("D ") + DemangledName.substr(0, Tilde-2) + '\n';
          goto NoFuncInfoEmit;
        }
      }
    }
    FuncInfo += std::string("Y ");
    if (F->hasFnAttribute(Attribute::SignalHandler)) {
      FuncInfo += "SignalHandler\n";
    } else if (F->hasFnAttribute(Attribute::ThreadEntry)) {
      FuncInfo += "ThreadEntry\n";
    } else if (GlobalDtors.find(FuncName) != std::end(GlobalDtors) ||
               (FuncName.size() > 12 && FuncName.substr(0, 12) == "_GLOBAL__D__")) {
      FuncInfo += "GblDtor\n";
    } else if (FuncName.size() > 12 && FuncName.substr(0, 12) == "_GLOBAL__E__") {
      FuncInfo += "GblExnDtor\n";
    } else if (FuncName.equals("main")) {
      // all main functions have the same type int (*)(int, char**, char**);
      FuncInfo += "i32!i32@i8**@i8**@\n";
    } else {
      FuncInfo += FuncTypeStr(MF.getFunction()->getFunctionType()) + '\n';
    }
    NoFuncInfoEmit:
#define addList(name, list) do {                \
      if (list.size()) {                        \
        FuncInfo += std::string(name);          \
        for (const auto & s: list) {            \
          FuncInfo += std::string(" ") + s;     \
        }                                       \
        FuncInfo += '\n';                       \
      }} while (0);
    addList("R", Returns);
    addList("T", DirectTailCalls);
    addList("I", IndirectTailCalls);
#undef addList
    FuncInfo += "}";
    Returns.clear();
    DirectTailCalls.clear();
    IndirectTailCalls.clear();
    NamedMDNode *MD = M->getOrInsertNamedMetadata("MCFIFuncInfo");
    MD->addOperand(MDNode::get(M->getContext(),
                               MDString::get(M->getContext(), FuncInfo.c_str())));
    
    MD = M->getOrInsertNamedMetadata("MCFIIndirectCalls");
    for (const auto & s: IndirectCalls) {
      MD->addOperand(MDNode::get(M->getContext(),
                                 MDString::get(M->getContext(), s.c_str())));
    }
    IndirectCalls.clear();
  }
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
                   DebugLoc &DL,
                   bool isReturn);

  void MCFIx64Ret(MachineFunction &MF, MachineBasicBlock *MBB,
                  MachineBasicBlock::iterator &MI);
  void MCFIx64IndirectCall(MachineFunction &MF, MachineBasicBlock *MBB,
                           MachineBasicBlock::iterator &MI);

  void MCFIx64IndirectMemWriteSmall(MachineFunction &MF, MachineBasicBlock *MBB);
  void MCFIx64IndirectMemWriteLarge(MachineFunction &MF, MachineBasicBlock *MBB);
    
  void MCFIx64StackPointer(MachineFunction &MF, MachineBasicBlock *MBB,
                           MachineBasicBlock::iterator &MI);

  void MCFIx64ICF(MachineFunction &MF, MachineBasicBlock *MBB,
                  MachineInstr *MI);
  
  void MCFIx64IDCmp(MachineFunction &MF,
                    MachineBasicBlock *MBB,
                    unsigned BIDReg,
                    const unsigned TargetReg,
                    DebugLoc& DL);

  void MCFIx64ICJ(MachineFunction &MF,
                  MachineBasicBlock *MBB,
                  const unsigned CJOp,
                  const unsigned TargetReg,
                  DebugLoc& DL);
  
  void MCFIx64IDValidityCheck(MachineFunction &MF,
                              MachineBasicBlock *MBB,
                              unsigned BIDReg,
                              unsigned TIDReg,
                              const unsigned TargetReg,
                              DebugLoc& DL);
  
  void MCFIx64IDVersionCheck(MachineFunction &MF,
                             MachineBasicBlock *MBB,
                             unsigned BIDReg,
                             unsigned TIDReg,
                             DebugLoc& DL);

  void MCFIx64Report(MachineFunction &MF,
                     MachineBasicBlock *MBB,
                     MachineBasicBlock *IDCmpMBB,
                     const unsigned TargetReg,
                     DebugLoc& DL,
                     bool isReturn);
  
  bool MCFIx32(MachineFunction &MF);  // x86_64 with ILP32

  // 32-bit
  bool MCFIx86(MachineFunction &MF);  // x86_32
};
char MCFI::ID = 0;
unsigned MCFI::BarySlot = 0;
}

FunctionPass *llvm::createX86MCFIInstrumentation() { return new MCFI(); }

void MCFI::extractGlobalArray(const StringRef ArrayName,
                              std::set<StringRef> &StrSet) {
  StrSet.clear();
  const GlobalVariable *GV = M->getNamedGlobal(ArrayName);
  if (GV) {
    const Constant *C = GV->getInitializer();
    const ConstantArray *CA = dyn_cast<ConstantArray>(C);
    if (CA) { // CA might be a zero initializer
      for (unsigned i = 0, e = CA->getNumOperands(); i != e; ++i) {
        const Value *V = CA->getOperand(i);
        const ConstantStruct *CS = dyn_cast<ConstantStruct>(V);
        const Function* F = dyn_cast<Function>(CS->getOperand(1));
        StrSet.insert(F->getName());
      }
    }
  }
}

bool MCFI::runOnMachineFunction(MachineFunction &MF) {

  if (MF.getTarget().Options.DisableCFI)
    return false;

  Module* newM = const_cast<Module*>(MF.getMMI().getModule());
  if (M != newM) {
    M = newM;
    SmallSandbox = !M->getNamedMetadata("MCFILargeSandbox");
    SmallID = !M->getNamedMetadata("MCFILargeID");
    
    std::random_device rd;
    std::mt19937_64 e2(rd());
    std::uniform_int_distribution<unsigned long>dist(0UL, (unsigned long)-1);
    ModuleID = dist(e2);

    // global constructors and destructors
    extractGlobalArray("llvm.global_ctors", GlobalCtors);
    extractGlobalArray("llvm.global_dtors", GlobalDtors);

    // add global constructors and destructors to address taken functions
    for (const auto& s: GlobalCtors) {
      llvm::NamedMDNode* MD = M->getOrInsertNamedMetadata("MCFIAddrTaken");
      MD->addOperand(llvm::MDNode::get(M->getContext(),
                                       llvm::MDString::get(
                                         M->getContext(), s.data())));
    }

    for (const StringRef& s: GlobalDtors) {
      llvm::NamedMDNode* MD = M->getOrInsertNamedMetadata("MCFIAddrTaken");
      MD->addOperand(llvm::MDNode::get(M->getContext(),
                                       llvm::MDString::get(
                                         M->getContext(), s.data())));
    }
  }
  // we only support AMD64
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
                        DebugLoc &DL) {
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

  // mov %gs:BarySlot, BIDReg
  auto &MIB = BuildMI(*MBB, I, DL, TII->get(BIDRegReadOp))
    .addReg(BIDReg, RegState::Define)
    .addReg(0).addImm(1).addReg(0)
    .addImm(0x1000 + BarySlot * (SmallID ? 4 : 8))
    .addReg(X86::GS);
  MIB->setBarySlot(ModuleID + BarySlot);
  
  // cmp BIDReg, %gs:(TargetReg)
  BuildMI(*MBB, I, DL, TII->get(CmpOp))
    .addReg(TargetReg).addImm(1).addReg(0).addImm(0).addReg(X86::GS).addReg(BIDReg);
  // jne Lcheck
  BuildMI(*MBB, I, DL, TII->get(X86::JNE_1));
}

void MCFI::MCFIx64ICJ(MachineFunction &MF, MachineBasicBlock* MBB,
                      const unsigned CJOp, const unsigned TargetReg,
                      DebugLoc &DL) {
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);
  BuildMI(*MBB, I, DL, TII->get(CJOp)).addReg(TargetReg);
  std::prev(I)->setBarySlot(ModuleID + BarySlot);
}

void MCFI::MCFIx64IDValidityCheck(MachineFunction &MF,
                                  MachineBasicBlock *MBB,
                                  unsigned BIDReg,
                                  unsigned TIDReg,
                                  const unsigned TargetReg,
                                  DebugLoc &DL) {
  
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
  // if TIDReg's least significant bit is zero, the previous test
  // instruction would set ZF (zero flag) of EFLAGS to 1.
  // Therefore, je(jz) Lreport
  BuildMI(*MBB, I, DL, TII->get(X86::JE_4));
}

void MCFI::MCFIx64IDVersionCheck(MachineFunction &MF,
                                 MachineBasicBlock *MBB,
                                 unsigned BIDReg,
                                 unsigned TIDReg,
                                 DebugLoc &DL) {
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
  BuildMI(*MBB, I, DL, TII->get(X86::JNE_1));
}

void MCFI::MCFIx64Report(MachineFunction &MF,
                         MachineBasicBlock *MBB,
                         MachineBasicBlock *IDCmpMBB,
                         const unsigned TargetReg,
                         DebugLoc &DL, bool isReturn) {
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();

  MBB->addLiveIn(TargetReg);
  
  auto I = std::begin(*MBB);

  if (!isReturn && TargetReg != X86::RSI) {
    BuildMI(*MBB, I, DL, TII->get(X86::MOV64rr))
      .addReg(X86::RSI).addReg(TargetReg);
  }
  // load the indirect branch address
  BuildMI(*MBB, I, DL, TII->get(X86::LEA64r))
    .addReg(X86::RDI, RegState::Define).addReg(X86::RIP).addImm(1)
    .addReg(0).addMBB(IDCmpMBB).addReg(0);
 
  // report the violation
  // It is weird that only TAILJMPd64 and CALL64pcrel32 would be
  // able to generate PIC code.
  const char *report = isReturn ?
    __report_cfi_violation_for_return :
    __report_cfi_violation;
  BuildMI(*MBB, I, DL, TII->get(X86::TAILJMPd64))
    .addExternalSymbol(report, X86II::MO_PLT);
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
                       DebugLoc &DL,
                       bool isReturn) {
  MachineFunction::iterator MBBI;
  
  IDCmpMBB = MF.CreateMachineBasicBlock();
  MBBI = MBB;
  MF.insert(++MBBI, IDCmpMBB); // original MBB to ICCmpMBB, fallthrough
  
  MCFIx64IDCmp(MF, IDCmpMBB, BIDReg, TargetReg, DL); // fill the IDCmp block
  
  ICJMBB = MF.CreateMachineBasicBlock();
  MBBI = IDCmpMBB;
  MF.insert(++MBBI, ICJMBB); // fall through ICJMBB
  IDCmpMBB->addSuccessor(ICJMBB);
  MCFIx64ICJ(MF, ICJMBB, CJOp, TargetReg, DL); // fill ICJMBB
  
  IDValidityCheckMBB = MF.CreateMachineBasicBlock();
  MF.push_back(IDValidityCheckMBB);
  MCFIx64IDValidityCheck(MF, IDValidityCheckMBB, BIDReg,
                         TIDReg, TargetReg, DL); // fill IDValCheck MBB
  
  IDCmpMBB->addSuccessor(IDValidityCheckMBB, UINT_MAX); // as far as possible

  VerCheckMBB = MF.CreateMachineBasicBlock();
  MF.push_back(VerCheckMBB);
  MCFIx64IDVersionCheck(MF, VerCheckMBB, BIDReg, TIDReg, DL); // fill VerCheckMBB

  ReportMBB = MF.CreateMachineBasicBlock();
  MBBI = VerCheckMBB;
  MF.insert(++MBBI, ReportMBB);
  MCFIx64Report(MF, ReportMBB, IDCmpMBB, TargetReg, DL, isReturn); // fill report MBB
  
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
                      MachineBasicBlock::iterator &MI) {
  ++NumReturns;

  DebugLoc DL = MI->getDebugLoc();
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();
  const Function *F = MF.getFunction();

  const unsigned BIDReg = X86::RDI;
  const unsigned TIDReg = X86::RSI;
  const unsigned TargetReg = X86::RCX;

  // popq %rcx
  BuildMI(*MBB, MI, DL, TII->get(X86::POP64r))
    .addReg(TargetReg, RegState::Define);

  // signal handlers' return are replaced with a direct sigreturn system call
  if (F->hasFnAttribute(Attribute::SignalHandler)) {
    BuildMI(*MBB, MI, DL, TII->get(X86::MOV64ri32))
      .addReg(X86::RAX).addImm(15); // sigreturn system call number is 15
    BuildMI(*MBB, MI, DL, TII->get(X86::SYSCALL));
    MI = MBB->erase(MI); // MI points std::end(MBB)
    return;
  }

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
              IDValidityCheckMBB, VerCheckMBB, ReportMBB, DL, true);
  Returns.push_back(to_hex(ModuleID + BarySlot));
  BarySlot++;
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

// get a general register that is not in RegSet
static unsigned getX64ScratchReg(const std::set<unsigned> RegSet) {
  static const std::set<unsigned> GRegs = {
    X86::RAX, X86::RBX, X86::RCX, X86::RDX, X86::RDI, X86::RSI, X86::R8,
    X86::R9, X86::R10, X86::R11, X86::R12, X86::R13, X86::R14, X86::R15
  };
  for (auto reg : GRegs)
    if (RegSet.find(reg) == std::end(RegSet))
      return reg;
  llvm_unreachable("getX64ScratchReg does not find scratch registers.");
}

static void MCFIx64SpillRegToStack(MachineBasicBlock* MBB,
                                   MachineBasicBlock::iterator &MI,
                                   const TargetInstrInfo *TII,
                                   const unsigned ScratchReg,
                                   const int64_t Offset) {
  BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(X86::MOV64mr))
    .addReg(X86::RSP).addImm(1).addReg(0).addImm(Offset).addReg(0)
    .addReg(ScratchReg);
}

static void MCFIx64ReloadRegFromStack(MachineBasicBlock* MBB,
                                      MachineBasicBlock::iterator &MI,
                                      const TargetInstrInfo *TII,
                                      const unsigned ScratchReg,
                                      const int64_t Offset) {
  BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(X86::MOV64rm))
    .addReg(ScratchReg)
    .addReg(X86::RSP).addImm(1).addReg(0).addImm(Offset).addReg(0);
}

void MCFI::MCFIx64IndirectCall(MachineFunction &MF, MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator &MI) {
  ++NumIndirectCall;
  MachineFunction::iterator MBBI;
  const Instruction* I = MI->getIRInst();
  unsigned CJOp;
  if (MI->getOpcode() == X86::CALL64r ||
      MI->getOpcode() == X86::CALL64m)
    CJOp = X86::CALL64r;
  else if (MI->getOpcode() == X86::JMP64r ||
           MI->getOpcode() == X86::JMP64m)
    CJOp = X86::JMP64r;
  else
    CJOp = X86::TAILJMPr64;

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
  if (MI->getOpcode() == X86::JMP64r ||
      MI->getOpcode() == X86::TAILJMPr64 ||
      MI->getOpcode() == X86::CALL64r) {
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
  } else { // JMP64m or CALL64m or TAILJMPm64
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

  bool BIDRegSpill = false;
  bool TIDRegSpill = false;
  unsigned BIDReg;
  unsigned TIDReg;

  if (ScratchRegs.size() >= 2) {
    BIDReg = *ScratchRegs.begin();
    ScratchRegs.erase(BIDReg);
    TIDReg = *ScratchRegs.begin();
  } else if (ScratchRegs.size() == 1) {
    BIDReg = *ScratchRegs.begin();
    TIDReg = getX64ScratchReg(TargetReg, BIDReg);
    TIDRegSpill = true;
  } else { // no scratch register at all!!
    BIDReg = getX64ScratchReg(TargetReg, TargetReg);
    BIDRegSpill = true;
    TIDReg = getX64ScratchReg(TargetReg, BIDReg);
    TIDRegSpill = true;
  }
  // build the MCFI basic blocks
  MachineBasicBlock *IDCmpMBB, *ICJMBB,
    *IDValidityCheckMBB, *VerCheckMBB, *ReportMBB;

  MCFIx64MBBs(MF, MBB, BIDReg, TIDReg, TargetReg, IDCmpMBB, ICJMBB, CJOp,
              IDValidityCheckMBB, VerCheckMBB, ReportMBB, DL, false);

  if (BIDRegSpill) {
    MCFIx64SpillRegToStack(MBB, MI, TII, BIDReg, -8);
    auto ICJBegin = std::begin(*ICJMBB);
    MCFIx64ReloadRegFromStack(ICJMBB, ICJBegin, TII, BIDReg, -8);
  }

  if (TIDRegSpill) {
    MCFIx64SpillRegToStack(MBB, MI, TII, TIDReg, -16);
    auto ICJBegin = std::begin(*ICJMBB);
    MCFIx64ReloadRegFromStack(ICJMBB, ICJBegin, TII, TIDReg, -16);
  }

  IDCmpMBB->instr_front().setIRInst(I); // transfer the IR to the BID read instruction.
  if (CJOp == X86::TAILJMPr64) {
    IndirectTailCalls.push_back(to_hex(ModuleID + BarySlot));
  }
  {
    // indirect call types
    std::string ICStr(to_hex(ModuleID + BarySlot));
    const CallInst *CI = dyn_cast<CallInst>(I);
    const InvokeInst *II = dyn_cast<InvokeInst>(I);
    assert(CI || II);
#define selectInst(CI, II, func, arg) (CI ? CI->func(arg) : II->func(arg))
    const Value *CV = selectInst(CI, II, getCalledValue, );
    const PointerType *PTy = cast<PointerType>(CV->getType());
    const FunctionType *FTy = cast<FunctionType>(PTy->getElementType());
    
    if (I->hasMetadata()) {
      const MDNode* mdNode = I->getMetadata("CXXVirtual");
      if (mdNode) {
        const Value *V = mdNode->getOperand(0);
        const std::string VStr = cast<MDString>(V)->getString().str();
        ICStr += "#" + VStr;
        if (VStr.find("P#") == 0) { // virtual call or virtual destructor
          ICStr += "#" + FuncTypeStr(FTy);
        }
        goto AddToList;
      }
    }
    ICStr += "#N#" + FuncTypeStr(FTy);
#undef selectInst
    AddToList:
    IndirectCalls.push_back(ICStr);
  }
  BarySlot++;
}

#if 0
static void MCFIx64CheckMemWriteInPlace(MachineBasicBlock* MBB,
                                        MachineBasicBlock::iterator &MI,
                                        const TargetInstrInfo *TII,
                                        const unsigned ScratchReg) {
  BuildMI(*MBB, MI, MI->getDebugLoc(),
          TII->get(X86::MOV64rr)).addReg(ScratchReg).addReg(ScratchReg);
}

static int64_t MCFIx64SpillInRedZone(MachineFunction& MF) {
  const Function *Fn = MF.getFunction();
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  const X86Subtarget &STI = MF.getTarget().getSubtarget<X86Subtarget>();
  const TargetRegisterInfo *RegInfo = MF.getTarget().getRegisterInfo();
  bool IsWin64 = STI.isTargetWin64();

  // if the stack does not need any adjustment in this function,
  return (Fn->getAttributes().hasAttribute(AttributeSet::FunctionIndex,
                                           Attribute::NoRedZone) &&
          !RegInfo->needsStackRealignment(MF) &&
          !MFI->hasVarSizedObjects() &&                     // No dynamic alloca.
          !MFI->adjustsStack() &&                           // No calls.
          !IsWin64 &&                                       // Win64 has no Red Zone
          !MF.shouldSplitStack());                          // Regular stack
}

void MCFI::MCFIx64IndirectMemWriteSmall(MachineFunction &MF, MachineBasicBlock *MBB) {
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();
  MachineBasicBlock::iterator LastRegReload = nullptr;
  MachineBasicBlock::iterator LastInPlaceSandboxed = nullptr;
  unsigned LastInPlaceSandboxedReg = 0;
  
  MachineBasicBlock::iterator MI;
  for (auto MI = std::begin(*MBB); MI != std::end(*MBB); MI++) {
    if (MI->getOpcode() == X86::LOCK_PREFIX ||
        MI->getOpcode() == X86::REPNE_PREFIX ||
        MI->getOpcode() == X86::REP_PREFIX) {
      llvm_unreachable("MCFI Error: Indirect memory rewriting encounters lock or rep prefix.");
    }

    // FIXME: for each machine instruction claimed to be sandboxed, we
    // should check whether it's actually sandboxed.
    if (MI->mayStore() && MI->getNumOperands() >= 5 &&
        !MI->isBranch() && !MI->isSandboxed() && !MI->isInlineAsm()) {
      const auto Opcode = MI->getOpcode();

      if (RepOp(Opcode)) {  // in-place sandboxing
        MCFIx64CheckMemWriteInPlace(MBB, MI, TII, X86::RDI);
        continue;
      }

      const unsigned MemOpOffset = XCHGOp(Opcode);
      const auto BaseReg = MI->getOperand(MemOpOffset).isReg() ?
        MI->getOperand(MemOpOffset).getReg() : 0;
      const auto IndexReg = MI->getOperand(MemOpOffset+2).getReg();

      if ((BaseReg == 0 && IndexReg == 0) || // direct mem write
          (BaseReg == X86::RSP && IndexReg == 0) || // on stack write
          BaseReg == X86::RIP) { // pc-relative write
        continue;
      }

      const auto Offset = MI->getOperand(MemOpOffset+3).isImm() ?
        MI->getOperand(MemOpOffset+3).getImm() : -1;

      // The reason why 1 << 22 is in X86MCFIRegReserve
      if (IndexReg == 0 && isMagicOffset(Offset)) {
        bool NeedSandboxing = true;
        if (MI != std::begin(*MBB)) {
          auto PrevMI = std::prev(MI);
          if (LastInPlaceSandboxed == PrevMI &&
              BaseReg == LastInPlaceSandboxedReg)
            NeedSandboxing = false; // the previous instruction
        }
        // in-place sandboxing
        if (NeedSandboxing) {
          MCFIx64CheckMemWriteInPlace(MBB, MI, TII, BaseReg);
          LastInPlaceSandboxedReg = BaseReg;
        }
        LastInPlaceSandboxed = MI;
      } else {
        std::set<unsigned> UsedRegSet;
        for (auto MO = MI->operands_begin(); MO != MI->operands_end(); ++MO) {
          if (MO->isReg()) {
            const unsigned reg = MO->getReg();
            if (reg != X86::RIP && (
                  X86::GR8RegClass.contains(reg) ||
                  X86::GR16RegClass.contains(reg) ||
                  X86::GR32RegClass.contains(reg) ||
                  X86::GR64RegClass.contains(reg)))
              UsedRegSet.insert(getX86SubSuperRegister(MO->getReg(), MVT::i64, true));
          }
        }
        const unsigned ScratchReg = getX64ScratchReg(UsedRegSet);
        bool NeedSpill = true;
        if (MI != std::begin(*MBB)) {
          auto PrevMI = std::prev(MI);
          if (PrevMI == LastRegReload &&
              ScratchReg == PrevMI->getOperand(0).getReg()) {
            NeedSpill = false;
            MBB->erase(PrevMI); // delete the previous reg reload
          }
        }
        bool redZoneAlreadyUsed = MCFIx64SpillInRedZone(MF);
        // spill scratchreg
        if (NeedSpill) {
          if (redZoneAlreadyUsed) {
            // if this leaf function already uses the red zone, then
            // we can not simply use -128...-8(%rsp), because they
            // might be occupied by local variables. Further, we can
            // not use -136(%rsp) because its not in red zone and its
            // integrity is not guaranteed by the system. We need to
            // first subtract 8 from %rsp to make a the original
            // -136(%rsp) in the red zone.
            BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(X86::SUB64ri8))
              .addReg(X86::RSP).addReg(X86::RSP).addImm(-8);
            MCFIx64SpillRegToStack(MBB, MI, TII, ScratchReg, -128);
          } else {
            MCFIx64SpillRegToStack(MBB, MI, TII, ScratchReg, -8);
          }
        }
        // load address
        MCFIx64CheckMemWrite(MBB, MI, TII, MemOpOffset, ScratchReg, true);
        if (redZoneAlreadyUsed) {
          // %rsp should never be an index reg.
          assert(IndexReg != X86::RSP);
          if (BaseReg == X86::RSP) {
            // assume that %rsp is never spilled to memory.
            BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(X86::ADD64ri8))
              .addReg(ScratchReg).addReg(ScratchReg).addImm(8);
          }
        }
        // mem write
        MCFIx64RewriteMemWrite(MBB, MI, TII, MemOpOffset, ScratchReg, true);
        MI = std::next(MI);
        if (redZoneAlreadyUsed) {
          MCFIx64ReloadRegFromStack(MBB, MI, TII, ScratchReg, -128);
          BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(X86::ADD64ri8))
            .addReg(X86::RSP).addReg(X86::RSP).addImm(-8);
        } else {
          MCFIx64ReloadRegFromStack(MBB, MI, TII, ScratchReg, -8);
        }
        MI = std::prev(MI);
        LastRegReload = MI;
      }
    }
  }
}

void MCFI::MCFIx64IndirectMemWriteLarge(MachineFunction &MF,
                                        MachineBasicBlock *MBB) {
}

#endif

// RSP modification is unsafe except the following instructions
// LLVM-dependent routines
static bool SafeStackPointerModification(const unsigned opcode) {
  // push
  if (opcode >= X86::PUSH16i8 && opcode <= X86::PUSHi32)
    return true;
  // pop
  if (opcode >= X86::POP16r && opcode <= X86::POPA32)
    return true;
  if (opcode >= X86::POPDS16 && opcode <= X86::POPSS32)
    return true;
  return false;
}

// turn 64-bit RSP definition to 32-bit
static unsigned RspToEspDef(unsigned opcode)
{
  switch (opcode) {
  case X86::ADD64ri32: return X86::ADD32ri;
  case X86::ADD64ri8:  return X86::ADD32ri8;
  case X86::ADD64rm:   return X86::ADD32rm;
  case X86::ADD64rr:   return X86::ADD32rr;
  case X86::SUB64ri32: return X86::SUB32ri;
  case X86::SUB64ri8:  return X86::SUB32ri8;
  case X86::SUB64rm:   return X86::SUB32rm;
  case X86::SUB64rr:   return X86::SUB32rr;
  case X86::AND64ri32: return X86::AND32ri;
  case X86::AND64ri8:  return X86::AND32ri8;
  case X86::AND64rm:   return X86::AND32rm;
  case X86::AND64rr:   return X86::AND32rr;
  case X86::OR64ri32:  return X86::OR32ri;
  case X86::OR64ri8:   return X86::OR32ri8;
  case X86::OR64rm:    return X86::OR32rm;
  case X86::OR64rr:    return X86::OR32rr;
  case X86::LEA64r:    return X86::LEA64_32r;
  case X86::MOV64rr:   return X86::MOV32rr;
  case X86::MOV64rm:   return X86::MOV32rm;
  default:
    return 0;
  }
}

void MCFI::MCFIx64StackPointer(MachineFunction &MF, MachineBasicBlock *MBB,
                               MachineBasicBlock::iterator &MI) {
  const unsigned opcode = MI->getOpcode();
  if (SafeStackPointerModification(opcode))
    return;
  const unsigned newopcode = RspToEspDef(opcode);
  const TargetInstrInfo *TII = MF.getTarget().getInstrInfo();
  if (!newopcode) {
    // we add a movl %esp, %esp after MI to sandbox %rsp
    auto nextMI = std::next(MI);
    BuildMI(*MBB, nextMI, MI->getDebugLoc(), TII->get(X86::MOV32rr))
      .addReg(X86::ESP, RegState::Define).addReg(X86::ESP);
    MI = std::next(MI);
  } else {
    auto &MIB = BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(newopcode))
      .addReg(X86::ESP, RegState::Define);
    for (unsigned i = 1; i < MI->getNumOperands(); i++) {
      if (MI->getOperand(i).isReg() &&
          MI->getOperand(i).getReg() == X86::RSP &&
          MI->getOperand(i).isTied())
        MIB.addReg(X86::ESP);
      else
        MIB.addOperand(MI->getOperand(i));
    }
    MI = MBB->erase(MI); // remove the old instruction
    MI = std::prev(MI);  // back the iterator to the newly inserted instruction
  }
}

bool MCFI::MCFIx64(MachineFunction &MF) {
  const TargetMachine& TM = MF.getTarget();
  const TargetRegisterInfo *TRI = TM.getRegisterInfo();

  for (auto MBB = std::begin(MF); MBB != std::end(MF); MBB++) {
    for (auto MI = std::begin(*MBB); MI != std::end(*MBB); MI++) {
      if (MI->getOpcode() == X86::RETQ) { // return instruction
        MCFIx64Ret(MF, MBB, MI);
      } else if (MI->getOpcode() == X86::TAILJMPd64) {
        const MachineOperand &MOP = MI->getOperand(0);
        std::string fn;
        if (MOP.isGlobal()) {
          const GlobalValue *GV = MOP.getGlobal();
          fn = GV->getName().str();
        } else if (MOP.isSymbol()) {
          fn = std::string(MOP.getSymbolName());
        }
        if (fn != __report_cfi_violation &&
            fn != __report_cfi_violation_for_return)
          DirectTailCalls.push_back(fn);
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
        case X86::TAILJMPm64:
        case X86::TAILJMPr64:
        {
          MCFIx64IndirectCall(MF, MBB, MI);
          break;
        }
        }
      } else if (MI->definesRegister(X86::RSP, TRI) && // RSP register modification
                 !MI->isCall() && !MI->isReturn()) { // but not function calls or returns
        MCFIx64StackPointer(MF, MBB, MI);
      }
      if (MI == std::end(*MBB)) break;
    }
  }
  MCFIFuncInfo(MF);
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
