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
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

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
         N == "__bit_sar" || N == "__pow2u" || N == "__adddi3" ||
         N == "__subdi3";
}

static FunctionCallee getFn(Module &M, StringRef Name, Type *RetTy,
                            ArrayRef<Type *> Args) {
  return M.getOrInsertFunction(Name, FunctionType::get(RetTy, Args, false));
}

static bool isAllOnesI32(Value *V) {
  auto *CI = dyn_cast<ConstantInt>(V);
  return CI && CI->getType()->isIntegerTy(32) && CI->isMinusOne();
}

static ConstantInt *getRepresentableSignedNarrowConst(ConstantInt *CI64,
                                                      IntegerType *DstTy) {
  if (!CI64 || !CI64->getType()->isIntegerTy(64) || !DstTy)
    return nullptr;
  unsigned BW = DstTy->getBitWidth();
  APInt Narrow = CI64->getValue().trunc(BW);
  if (Narrow.sext(64) != CI64->getValue())
    return nullptr;
  return cast<ConstantInt>(ConstantInt::get(DstTy, Narrow));
}

static bool matchSExtToI64(Value *V, Value *&Src, IntegerType *&SrcTy) {
  auto *SI = dyn_cast<SExtInst>(V);
  if (!SI)
    return false;
  if (!SI->getType()->isIntegerTy(64))
    return false;
  SrcTy = dyn_cast<IntegerType>(SI->getSrcTy());
  if (!SrcTy || SrcTy->getBitWidth() > 32)
    return false;
  Src = SI->getOperand(0);
  return true;
}

static bool isShiftByBitWidthMinusOne(Value *V, Value *X) {
  auto *CI = dyn_cast<ConstantInt>(V);
  if (!CI || !X->getType()->isIntegerTy())
    return false;
  unsigned BW = cast<IntegerType>(X->getType())->getBitWidth();
  return CI->getValue() == BW - 1;
}

static bool matchSignedSignMask(Value *V, Value *X) {
  auto *BO = dyn_cast<BinaryOperator>(V);
  return BO && BO->getOpcode() == Instruction::AShr &&
         BO->getOperand(0) == X &&
         isShiftByBitWidthMinusOne(BO->getOperand(1), X);
}

static Value *buildBranchFriendlyAbs(Instruction &InsertBefore, Value *X,
                                     bool IntMinIsPoison) {
  auto *ITy = cast<IntegerType>(X->getType());
  IRBuilder<> HeadBuilder(&InsertBefore);
  Value *Zero = ConstantInt::get(ITy, 0);
  Value *IsNeg = HeadBuilder.CreateICmpSLT(X, Zero, X->getName() + ".isneg");

  BasicBlock *ThenBB = nullptr;
  BasicBlock *ElseBB = nullptr;
  SplitBlockAndInsertIfThenElse(IsNeg, &InsertBefore, &ThenBB, &ElseBB);

  BasicBlock *TailBB = InsertBefore.getParent();
  IRBuilder<> ThenBuilder(ThenBB->getTerminator());
  Value *NegX = ThenBuilder.CreateNeg(X, X->getName() + ".neg", IntMinIsPoison);

  auto *Phi =
      PHINode::Create(ITy, 2, X->getName() + ".abs", &InsertBefore);
  Phi->addIncoming(NegX, ThenBB);
  Phi->addIncoming(X, ElseBB);
  return Phi;
}

static Value *getBitTrickAbsInput(Instruction &I) {
  auto *Sub = dyn_cast<BinaryOperator>(&I);
  if (!Sub || Sub->getOpcode() != Instruction::Sub ||
      !Sub->getType()->isIntegerTy())
    return nullptr;

  Value *Mask = Sub->getOperand(1);
  auto *Xor = dyn_cast<BinaryOperator>(Sub->getOperand(0));
  if (!Xor || Xor->getOpcode() != Instruction::Xor)
    return nullptr;

  Value *X = nullptr;
  if (Xor->getOperand(1) == Mask && matchSignedSignMask(Mask, Xor->getOperand(0)))
    X = Xor->getOperand(0);
  else if (Xor->getOperand(0) == Mask &&
           matchSignedSignMask(Mask, Xor->getOperand(1)))
    X = Xor->getOperand(1);
  else
    return nullptr;

  return X;
}

static Value *matchBitTrickAbs(Instruction &I) {
  Value *X = getBitTrickAbsInput(I);
  if (!X)
    return nullptr;
  return buildBranchFriendlyAbs(I, X, false);
}

static Value *getWordBasePtr(IRBuilder<> &B, Value *Ptr, Type *IntPtrTy) {
  Value *PtrInt = B.CreatePtrToInt(Ptr, IntPtrTy, "mcasm.ptrint");
  Value *WordInt =
      B.CreateAnd(PtrInt, ConstantInt::get(IntPtrTy, ~uint64_t(3)),
                  "mcasm.wordbase");
  return B.CreateIntToPtr(WordInt, Ptr->getType(), "mcasm.wordptr");
}

static Value *getByteLane(IRBuilder<> &B, Value *Ptr, Type *IntPtrTy) {
  Value *PtrInt = B.CreatePtrToInt(Ptr, IntPtrTy, "mcasm.ptrint");
  return B.CreateAnd(PtrInt, ConstantInt::get(IntPtrTy, 3), "mcasm.lane");
}

static Value *getBitShift(IRBuilder<> &B, Value *Lane, Type *IntPtrTy,
                          Type *I32) {
  Value *Shift = B.CreateShl(Lane, ConstantInt::get(IntPtrTy, 3),
                             "mcasm.bitshift");
  if (Shift->getType() != I32)
    Shift = B.CreateTrunc(Shift, I32, "mcasm.bitshift32");
  return Shift;
}

static void expandSubwordStore(StoreInst &SI, SmallVectorImpl<Instruction *> &ToErase,
                               Type *I32, Type *IntPtrTy) {
  Type *Ty = SI.getValueOperand()->getType();
  if (!(Ty->isIntegerTy(8) || Ty->isIntegerTy(16)))
    return;
  if (SI.isVolatile() || SI.isAtomic())
    return;

  IRBuilder<> B(&SI);
  B.SetCurrentDebugLocation(SI.getDebugLoc());

  Value *Ptr = SI.getPointerOperand();
  Value *Lane = getByteLane(B, Ptr, IntPtrTy);
  Value *WordPtr = getWordBasePtr(B, Ptr, IntPtrTy);
  auto *OldWord = B.CreateLoad(I32, WordPtr, "mcasm.oldword");
  OldWord->setAlignment(Align(4));

  Value *Shift = getBitShift(B, Lane, IntPtrTy, I32);
  Value *Stored = B.CreateZExtOrTrunc(SI.getValueOperand(), I32, "mcasm.store32");

  if (Ty->isIntegerTy(8)) {
    Value *Mask = B.CreateShl(ConstantInt::get(I32, 0xFF), Shift, "mcasm.mask");
    Value *Cleared =
        B.CreateAnd(OldWord, B.CreateXor(Mask, ConstantInt::get(I32, -1)),
                    "mcasm.cleared");
    Value *Inserted = B.CreateShl(
        B.CreateAnd(Stored, ConstantInt::get(I32, 0xFF)), Shift,
        "mcasm.inserted");
    auto *NewStore = B.CreateStore(B.CreateOr(Cleared, Inserted), WordPtr);
    NewStore->setAlignment(Align(4));
    ToErase.push_back(&SI);
    return;
  }

  bool SingleWordHalfword = SI.getAlign() >= Align(2);
  if (SingleWordHalfword) {
    Value *Mask =
        B.CreateShl(ConstantInt::get(I32, 0xFFFF), Shift, "mcasm.mask16");
    Value *Cleared =
        B.CreateAnd(OldWord, B.CreateXor(Mask, ConstantInt::get(I32, -1)),
                    "mcasm.cleared16");
    Value *Inserted = B.CreateShl(
        B.CreateAnd(Stored, ConstantInt::get(I32, 0xFFFF)), Shift,
        "mcasm.inserted16");
    auto *NewStore = B.CreateStore(B.CreateOr(Cleared, Inserted), WordPtr);
    NewStore->setAlignment(Align(4));
    ToErase.push_back(&SI);
    return;
  }

  Value *LowByte = B.CreateAnd(Stored, ConstantInt::get(I32, 0xFF), "mcasm.lo");
  Value *HighByte = B.CreateAnd(B.CreateLShr(Stored, ConstantInt::get(I32, 8)),
                                ConstantInt::get(I32, 0xFF), "mcasm.hi");

  Value *LowMask =
      B.CreateShl(ConstantInt::get(I32, 0xFF), Shift, "mcasm.lowmask");
  Value *LowCleared =
      B.CreateAnd(OldWord, B.CreateXor(LowMask, ConstantInt::get(I32, -1)),
                  "mcasm.lowcleared");
  Value *LowInserted = B.CreateShl(LowByte, Shift, "mcasm.lowinserted");
  auto *FirstStore = B.CreateStore(B.CreateOr(LowCleared, LowInserted), WordPtr);
  FirstStore->setAlignment(Align(4));

  Value *NextPtrInt =
      B.CreateAdd(B.CreatePtrToInt(Ptr, IntPtrTy), ConstantInt::get(IntPtrTy, 1),
                  "mcasm.nextptrint");
  Value *NextPtr = B.CreateIntToPtr(NextPtrInt, Ptr->getType(), "mcasm.nextptr");
  Value *NextLane = getByteLane(B, NextPtr, IntPtrTy);
  Value *NextWordPtr = getWordBasePtr(B, NextPtr, IntPtrTy);
  auto *NextOld = B.CreateLoad(I32, NextWordPtr, "mcasm.nextoldword");
  NextOld->setAlignment(Align(4));
  Value *NextShift = getBitShift(B, NextLane, IntPtrTy, I32);
  Value *NextMask =
      B.CreateShl(ConstantInt::get(I32, 0xFF), NextShift, "mcasm.nextmask");
  Value *NextCleared =
      B.CreateAnd(NextOld, B.CreateXor(NextMask, ConstantInt::get(I32, -1)),
                  "mcasm.nextcleared");
  Value *NextInserted =
      B.CreateShl(HighByte, NextShift, "mcasm.nextinserted");
  auto *SecondStore =
      B.CreateStore(B.CreateOr(NextCleared, NextInserted), NextWordPtr);
  SecondStore->setAlignment(Align(4));
  (void)FirstStore;
  (void)SecondStore;
  ToErase.push_back(&SI);
}

static Value *branchifySelect(SelectInst &Sel) {
  BasicBlock *ThenBB = nullptr;
  BasicBlock *ElseBB = nullptr;
  SplitBlockAndInsertIfThenElse(Sel.getCondition(), &Sel, &ThenBB, &ElseBB);

  auto *Phi =
      PHINode::Create(Sel.getType(), 2, Sel.getName() + ".branch", &Sel);
  Phi->addIncoming(Sel.getTrueValue(), ThenBB);
  Phi->addIncoming(Sel.getFalseValue(), ElseBB);
  return Phi;
}

bool McasmLowerBitOpsPass::runOnModule(Module &M) {
  bool Changed = false;

  if (!McasmLowerBitopsToLibcalls)
    return false;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);

  FunctionCallee BitAnd = getFn(M, "__bit_and", I32, {I32, I32});
  FunctionCallee BitOr = getFn(M, "__bit_or", I32, {I32, I32});
  FunctionCallee BitXor = getFn(M, "__bit_xor", I32, {I32, I32});
  FunctionCallee BitNot = getFn(M, "__bit_not", I32, {I32});
  FunctionCallee BitShl = getFn(M, "__bit_shl", I32, {I32, I32});
  FunctionCallee BitShr = getFn(M, "__bit_shr", I32, {I32, I32});
  FunctionCallee BitSar = getFn(M, "__bit_sar", I32, {I32, I32});
  FunctionCallee AddDi3 = getFn(M, "__adddi3", I64, {I64, I64});
  FunctionCallee SubDi3 = getFn(M, "__subdi3", I64, {I64, I64});

  // Keep subword loads in IR so SelectionDAG lowering can preserve byte
  // semantics. Rewrite subword stores here into explicit i32 RMW sequences to
  // avoid pathological byte-store DAG lowering.
  {
    SmallVector<Instruction *, 128> ToEraseMem;
    for (Function &F : M) {
      if (F.isDeclaration() || isRuntimeFunctionName(F.getName()))
        continue;

      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (auto *SI = dyn_cast<StoreInst>(&I)) {
            expandSubwordStore(*SI, ToEraseMem, I32, Type::getInt32Ty(Ctx));
            if (!ToEraseMem.empty() && ToEraseMem.back() == SI)
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

    SmallVector<SelectInst *, 32> SelectsToBranchify;
    SmallVector<Instruction *, 16> AbsToBranchify;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *Sel = dyn_cast<SelectInst>(&I)) {
          if (Sel->getType()->isIntegerTy(32) || Sel->getType()->isIntegerTy(64))
            SelectsToBranchify.push_back(Sel);
          continue;
        }

        if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
          if (II->getIntrinsicID() == Intrinsic::abs &&
              II->getType()->isIntegerTy(32)) {
            AbsToBranchify.push_back(II);
            continue;
          }
        }

        if (I.getType()->isIntegerTy(32) && getBitTrickAbsInput(I)) {
          AbsToBranchify.push_back(&I);
          continue;
        }
      }
    }

    for (SelectInst *Sel : SelectsToBranchify) {
      if (!Sel->getParent())
        continue;
      Value *BrSel = branchifySelect(*Sel);
      Sel->replaceAllUsesWith(BrSel);
      ToErase.push_back(Sel);
      Changed = true;
    }

    for (Instruction *I : AbsToBranchify) {
      if (!I->getParent())
        continue;

      Value *AbsV = nullptr;
      if (auto *II = dyn_cast<IntrinsicInst>(I)) {
        bool IntMinIsPoison =
            cast<ConstantInt>(II->getArgOperand(1))->isOne();
        AbsV = buildBranchFriendlyAbs(*II, II->getArgOperand(0), IntMinIsPoison);
      } else {
        AbsV = matchBitTrickAbs(*I);
      }
      if (!AbsV)
        continue;

      I->replaceAllUsesWith(AbsV);
      ToErase.push_back(I);
      Changed = true;
    }

    SmallVector<PHINode *, 16> PhisToErase;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *Sel = dyn_cast<SelectInst>(&I)) {
          if (!Sel->getType()->isIntegerTy(64))
            continue;

          Value *Src = nullptr, *Other = nullptr;
          IntegerType *SrcTy = nullptr;
          Value *TrueV = Sel->getTrueValue();
          Value *FalseV = Sel->getFalseValue();

          bool TrueIsSExt = matchSExtToI64(TrueV, Src, SrcTy);
          if (TrueIsSExt) {
            Other = FalseV;
          } else {
            bool FalseIsSExt = matchSExtToI64(FalseV, Src, SrcTy);
            if (!FalseIsSExt)
              continue;
            Other = TrueV;
          }
          auto *OtherCI = dyn_cast<ConstantInt>(Other);
          ConstantInt *NarrowC = getRepresentableSignedNarrowConst(OtherCI, SrcTy);
          if (!NarrowC)
            continue;

          IRBuilder<> B(Sel);
          Value *NarrowSel = TrueIsSExt
                                 ? B.CreateSelect(Sel->getCondition(), Src, NarrowC)
                                 : B.CreateSelect(Sel->getCondition(), NarrowC, Src);
          Value *Wide = B.CreateSExt(NarrowSel, I64);
          Sel->replaceAllUsesWith(Wide);
          ToErase.push_back(Sel);
          Changed = true;
          continue;
        }

        if (auto *Phi = dyn_cast<PHINode>(&I)) {
          if (!Phi->getType()->isIntegerTy(64))
            continue;
          if (Phi->getNumIncomingValues() < 2)
            continue;

          IntegerType *SrcTy = nullptr;
          bool Valid = true;
          bool HasSExtArm = false;
          SmallVector<ConstantInt *, 4> PendingConsts;
          for (unsigned Idx = 0; Idx < Phi->getNumIncomingValues(); ++Idx) {
            BasicBlock *Pred = Phi->getIncomingBlock(Idx);
            if (Pred == Phi->getParent()) {
              Valid = false;
              break;
            }
            Value *InV = Phi->getIncomingValue(Idx);
            Value *Src = nullptr;
            IntegerType *ThisTy = nullptr;
            if (matchSExtToI64(InV, Src, ThisTy)) {
              HasSExtArm = true;
              if (!SrcTy)
                SrcTy = ThisTy;
              else if (SrcTy != ThisTy) {
                Valid = false;
                break;
              }
              continue;
            }
            auto *CI = dyn_cast<ConstantInt>(InV);
            if (!CI || !CI->getType()->isIntegerTy(64)) {
              Valid = false;
              break;
            }
            PendingConsts.push_back(CI);
          }
          if (!Valid || !HasSExtArm || !SrcTy)
            continue;
          for (ConstantInt *CI : PendingConsts) {
            if (!getRepresentableSignedNarrowConst(CI, SrcTy)) {
              Valid = false;
              break;
            }
          }
          if (!Valid)
            continue;

          auto *NarrowPhi = PHINode::Create(
              SrcTy, Phi->getNumIncomingValues(), Phi->getName() + ".narrow", Phi);
          for (unsigned Idx = 0; Idx < Phi->getNumIncomingValues(); ++Idx) {
            Value *InV = Phi->getIncomingValue(Idx);
            Value *Src = nullptr;
            IntegerType *ThisTy = nullptr;
            if (matchSExtToI64(InV, Src, ThisTy)) {
              NarrowPhi->addIncoming(Src, Phi->getIncomingBlock(Idx));
            } else {
              auto *CI = cast<ConstantInt>(InV);
              NarrowPhi->addIncoming(
                  getRepresentableSignedNarrowConst(CI, SrcTy),
                  Phi->getIncomingBlock(Idx));
            }
          }
          Instruction *InsertPt = Phi->getParent()->getFirstNonPHI();
          IRBuilder<> B(InsertPt);
          Value *Wide = B.CreateSExt(NarrowPhi, I64);
          Phi->replaceAllUsesWith(Wide);
          PhisToErase.push_back(Phi);
          Changed = true;
          continue;
        }

        auto *BO = dyn_cast<BinaryOperator>(&I);
        if (!BO)
          continue;

        IRBuilder<> B(BO);
        Value *L = BO->getOperand(0);
        Value *R = BO->getOperand(1);
        Value *NewV = nullptr;

        if (BO->getType()->isIntegerTy(64)) {
          switch (BO->getOpcode()) {
          case Instruction::Add:
            NewV = B.CreateCall(AddDi3, {L, R});
            break;
          case Instruction::Sub:
            NewV = B.CreateCall(SubDi3, {L, R});
            break;
          default:
            break;
          }
        } else if (!BO->getType()->isIntegerTy(32)) {
          continue;
        }

        if (!NewV) {
          if (!BO->getType()->isIntegerTy(32))
            continue;
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
        }

        if (!NewV)
          continue;
        BO->replaceAllUsesWith(NewV);
        ToErase.push_back(BO);
        Changed = true;
      }
    }

    for (auto It = PhisToErase.rbegin(), End = PhisToErase.rend(); It != End;
         ++It)
      (*It)->eraseFromParent();
  }

  for (auto It = ToErase.rbegin(), End = ToErase.rend(); It != End; ++It)
    (*It)->eraseFromParent();

  return Changed;
}

} // namespace

Pass *llvm::createMcasmLowerBitOpsPass() { return new McasmLowerBitOpsPass(); }
