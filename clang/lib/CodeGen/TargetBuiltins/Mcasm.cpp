//===-- Mcasm.cpp - Emit LLVM Code for Mcasm builtins --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CGBuiltin.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/IR/Attributes.h"

using namespace clang;
using namespace CodeGen;

llvm::Value *CodeGenFunction::EmitMcasmBuiltinExpr(unsigned BuiltinID,
                                                   const CallExpr *E) {
  switch (BuiltinID) {
  case Mcasm::BI__builtin_pow2u: {
    llvm::Value *B = EmitScalarExpr(E->getArg(0));
    llvm::Type *I32Ty = Builder.getInt32Ty();
    B = Builder.CreateZExtOrTrunc(B, I32Ty);
    llvm::FunctionCallee Pow2u =
        CGM.CreateRuntimeFunction(llvm::FunctionType::get(I32Ty, {I32Ty}, false),
                                  "__pow2u");
    return Builder.CreateCall(Pow2u, {B});
  }
  case Mcasm::BI__builtin_declen: {
    llvm::Value *V = EmitScalarExpr(E->getArg(0));
    llvm::Type *I32Ty = Builder.getInt32Ty();
    V = Builder.CreateSExtOrTrunc(V, I32Ty);
    llvm::AttrBuilder FnAttrB(getLLVMContext());
    FnAttrB.addAttribute(llvm::Attribute::NoUnwind);
    FnAttrB.addAttribute(llvm::Attribute::WillReturn);
    FnAttrB.addAttribute(llvm::Attribute::NoFree);
    FnAttrB.addAttribute(llvm::Attribute::NoSync);
    FnAttrB.addMemoryAttr(llvm::MemoryEffects::none());
    llvm::AttributeList DeclenAttrs = llvm::AttributeList::get(
        getLLVMContext(), llvm::AttributeList::FunctionIndex, FnAttrB);
    llvm::FunctionCallee Declen =
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(I32Ty, {I32Ty}, false), "__declen",
            DeclenAttrs);
    return Builder.CreateCall(Declen, {V});
  }
  default:
    return nullptr;
  }
}
