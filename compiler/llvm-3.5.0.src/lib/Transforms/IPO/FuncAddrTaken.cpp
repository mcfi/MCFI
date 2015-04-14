#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/CtorUtils.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <set>

using namespace llvm;

#define DEBUG_TYPE "funcaddrtaken"

namespace {
  struct FuncAddrTaken : public ModulePass {
    void getAnalysisUsage(AnalysisUsage &AU) const override {
    }
    static char ID; // Pass identification, replacement for typeid
    FuncAddrTaken() : ModulePass(ID) {
      initializeGlobalOptPass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) override;
  private:
    Value *InnerMost(Value *V);
    void addPatchAt(Module &M, FunctionType *FT,
                    Value *V, Instruction *MI);
  };
}

char FuncAddrTaken::ID = 0;
ModulePass *llvm::createFuncAddrTakenPass() { return new FuncAddrTaken(); }
//static std::set<Module*> rewritten_modules;

// TODO: finish this function
Value *FuncAddrTaken::InnerMost(Value *V) {
  if (isa<ConstantExpr>(V) && cast<ConstantExpr>(V)->isCast()) {
    Value *NV = cast<ConstantExpr>(V)->stripPointerCasts();
    if (isa<Function>(NV))
      V = NV;
    else if (cast<ConstantExpr>(V)->getOpcode() == Instruction::PtrToInt) {
      NV = cast<ConstantExpr>(V)->getOperand(0);
      if (isa<Function>(NV))
        V = NV;
    }
  }
  return V;
}

void FuncAddrTaken::addPatchAt(Module &M, FunctionType *FT,
                               Value *V, Instruction *MI) {
  if (isa<Function>(V) && cast<Function>(V)->hasName()) {
    StringRef fn = cast<Function>(V)->getName();
    if (fn.startswith("_GLOBAL__D_") || fn.startswith("_GLOBAL__E_"))
      return;
    IRBuilder<> Builder(M.getContext());
    Constant* PatchAtHere =
      M.getOrInsertFunction(
        std::string("__patch_at") + fn.str(), FT);
    CallInst *PatchAtCI =
      Builder.CreateCall(PatchAtHere);
    PatchAtCI->insertBefore(MI);
  }
}

bool FuncAddrTaken::runOnModule(Module &M) {
  bool Changed = false;
  // add declaration of function __patch_at
  // declare void @__patch_at(void)
  FunctionType *FT = FunctionType::get(Type::getVoidTy(M.getContext()),
                                       false);
  Function* PatchAt = Function::Create(FT, Function::ExternalLinkage, "__patch_at", &M);
  if (PatchAt->getName() != "__patch_at") {
    PatchAt->eraseFromParent();
    return false;
  }
  Changed = true;

  // before each store instruction that manipulates a function, create a call
  // to __patch_at
  for (auto F = M.getFunctionList().begin(); F != M.getFunctionList().end(); F++) {
    for (auto BB = F->begin(); BB != F->end(); BB++) {
      for (auto MI = BB->begin(); MI != BB->end(); MI++) {
        if (isa<StoreInst>(MI)) {
          // check if the store inst moves a function to a variable
          Value *V = InnerMost(cast<StoreInst>(MI)->getValueOperand());
          addPatchAt(M, FT, V, MI);
          if (isa<ConstantVector>(V)) {
            std::set<Value*> patched;
            for (unsigned i = 0; i < cast<ConstantVector>(V)->getNumOperands(); i++) {
              Value *VV = InnerMost(cast<ConstantVector>(V)->getOperand(i));
              if (patched.find(VV) == patched.end()) {
                addPatchAt(M, FT, VV, MI);
                patched.insert(VV);
              }
            }
          } else if (isa<ConstantStruct>(V)) {
            std::set<Value*> patched;
            for (unsigned i = 0; i < cast<ConstantStruct>(V)->getNumOperands(); i++) {
              Value *VV = InnerMost(cast<ConstantStruct>(V)->getOperand(i));
              if (patched.find(VV) == patched.end()) {
                addPatchAt(M, FT, VV, MI);
                patched.insert(VV);
              }
            }
          } else if (isa<ConstantArray>(V)) {
            std::set<Value*> patched;
            for (unsigned i = 0; i < cast<ConstantArray>(V)->getNumOperands(); i++) {
              Value *VV = InnerMost(cast<ConstantArray>(V)->getOperand(i));
              if (patched.find(VV) == patched.end()) {
                addPatchAt(M, FT, VV, MI);
                patched.insert(VV);
              }
            }
          }
        } else if (isa<SelectInst>(MI)) {
          Value *V = InnerMost(cast<SelectInst>(MI)->getTrueValue());
          addPatchAt(M, FT, V, MI);
          V = InnerMost(cast<SelectInst>(MI)->getFalseValue());
          addPatchAt(M, FT, V, MI);
        } else if (isa<CallInst>(MI)) {
          CallInst* CI = cast<CallInst>(MI);
          for (unsigned i = 0; i < CI->getNumArgOperands(); i++) {
            Value *V = InnerMost(CI->getArgOperand(i));
            addPatchAt(M, FT, V, MI);
          }
        } else if (isa<InvokeInst>(MI)) {
          InvokeInst* CI = cast<InvokeInst>(MI);
          for (unsigned i = 0; i < CI->getNumArgOperands(); i++) {
            Value *V = InnerMost(CI->getArgOperand(i));
            addPatchAt(M, FT, V, MI);
          }
        } else if (isa<ReturnInst>(MI)) {
          Value *V = cast<ReturnInst>(MI)->getReturnValue();
          if (V) {
            V = InnerMost(V);
            addPatchAt(M, FT, V, MI);
          }
        } else if (isa<PHINode>(MI)) {
          for (unsigned i = 0; i < cast<PHINode>(MI)->getNumIncomingValues(); i++) {
            Value *V = InnerMost(cast<PHINode>(MI)->getIncomingValue(i));
            BasicBlock* BB = cast<PHINode>(MI)->getIncomingBlock(i);
            // right before the last (maybe terminator) instruction.
            addPatchAt(M, FT, V, &(BB->back()));
          }
        }
      }
    }
  }
  return Changed;
}
