//===-- McasmLowerBitOps.cpp - Link runtime and lower bitops -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Mcasm.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

namespace {

static cl::opt<bool> McasmLowerBitopsToLibcalls(
    "mcasm-lower-bitops-to-libcalls", cl::Hidden, cl::init(true),
    cl::desc("Lower i32 bitops to __bit_xxx libcalls"));

class McasmLowerBitOpsPass final : public ModulePass {
public:
  static char ID;
  McasmLowerBitOpsPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};

char McasmLowerBitOpsPass::ID = 0;

static bool isRuntimeFunctionName(StringRef N) {
  return N == "__bit_and" || N == "__bit_or" || N == "__bit_xor" ||
         N == "__bit_not" || N == "__bit_shl" || N == "__bit_shr" ||
         N == "__bit_sar" || N == "__pow2u";
}

static FunctionCallee getFn(Module &M, StringRef Name, Type *RetTy,
                            ArrayRef<Type *> Args) {
  return M.getOrInsertFunction(Name, FunctionType::get(RetTy, Args, false));
}

static bool isAllOnesI32(Value *V) {
  auto *CI = dyn_cast<ConstantInt>(V);
  return CI && CI->getType()->isIntegerTy(32) && CI->isMinusOne();
}

bool McasmLowerBitOpsPass::runOnModule(Module &M) {
  bool Changed = false;

  if (!McasmLowerBitopsToLibcalls)
    return false;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);

  FunctionCallee BitAnd = getFn(M, "__bit_and", I32, {I32, I32});
  FunctionCallee BitOr = getFn(M, "__bit_or", I32, {I32, I32});
  FunctionCallee BitXor = getFn(M, "__bit_xor", I32, {I32, I32});
  FunctionCallee BitNot = getFn(M, "__bit_not", I32, {I32});
  FunctionCallee BitShl = getFn(M, "__bit_shl", I32, {I32, I32});
  FunctionCallee BitShr = getFn(M, "__bit_shr", I32, {I32, I32});
  FunctionCallee BitSar = getFn(M, "__bit_sar", I32, {I32, I32});

  // mcasm uses 32-bit scalar storage slots. Rewrite i8/i16 memory operations
  // to i32 loads/stores in IR to avoid subword SelectionDAG paths.
  {
    SmallVector<Instruction *, 128> ToEraseMem;
    for (Function &F : M) {
      if (F.isDeclaration() || isRuntimeFunctionName(F.getName()))
        continue;

      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (auto *LI = dyn_cast<LoadInst>(&I)) {
            Type *Ty = LI->getType();
            if (!(Ty->isIntegerTy(8) || Ty->isIntegerTy(16)))
              continue;
            if (LI->isVolatile() || LI->isAtomic())
              continue;

            IRBuilder<> B(LI);
            auto *Wide = B.CreateLoad(I32, LI->getPointerOperand(),
                                      LI->getName() + ".w32");
            Wide->setAlignment(LI->getAlign());
            Value *Narrow = B.CreateTrunc(Wide, Ty, LI->getName() + ".narrow");
            LI->replaceAllUsesWith(Narrow);
            ToEraseMem.push_back(LI);
            Changed = true;
            continue;
          }

          if (auto *SI = dyn_cast<StoreInst>(&I)) {
            Type *Ty = SI->getValueOperand()->getType();
            if (!(Ty->isIntegerTy(8) || Ty->isIntegerTy(16)))
              continue;
            if (SI->isVolatile() || SI->isAtomic())
              continue;

            IRBuilder<> B(SI);
            Value *Wide = B.CreateZExt(SI->getValueOperand(), I32, "st.w32");
            auto *NewStore = B.CreateStore(Wide, SI->getPointerOperand());
            NewStore->setAlignment(SI->getAlign());
            ToEraseMem.push_back(SI);
            Changed = true;
          }
        }
      }
    }

    for (auto It = ToEraseMem.rbegin(), End = ToEraseMem.rend(); It != End;
         ++It)
      (*It)->eraseFromParent();
  }

  SmallVector<Instruction *, 128> ToErase;

  for (Function &F : M) {
    if (F.isDeclaration() || isRuntimeFunctionName(F.getName()))
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        auto *BO = dyn_cast<BinaryOperator>(&I);
        if (!BO || !BO->getType()->isIntegerTy(32))
          continue;

        IRBuilder<> B(BO);
        Value *L = BO->getOperand(0);
        Value *R = BO->getOperand(1);
        Value *NewV = nullptr;

        switch (BO->getOpcode()) {
        case Instruction::And:
          NewV = B.CreateCall(BitAnd, {L, R});
          break;
        case Instruction::Or:
          NewV = B.CreateCall(BitOr, {L, R});
          break;
        case Instruction::Xor:
          if (isAllOnesI32(L))
            NewV = B.CreateCall(BitNot, {R});
          else if (isAllOnesI32(R))
            NewV = B.CreateCall(BitNot, {L});
          else
            NewV = B.CreateCall(BitXor, {L, R});
          break;
        case Instruction::Shl:
          NewV = B.CreateCall(BitShl, {L, R});
          break;
        case Instruction::LShr:
          NewV = B.CreateCall(BitShr, {L, R});
          break;
        case Instruction::AShr:
          NewV = B.CreateCall(BitSar, {L, R});
          break;
        default:
          break;
        }

        if (!NewV)
          continue;
        BO->replaceAllUsesWith(NewV);
        ToErase.push_back(BO);
        Changed = true;
      }
    }
  }

  for (auto It = ToErase.rbegin(), End = ToErase.rend(); It != End; ++It)
    (*It)->eraseFromParent();

  return Changed;
}

} // namespace

Pass *llvm::createMcasmLowerBitOpsPass() { return new McasmLowerBitOpsPass(); }
