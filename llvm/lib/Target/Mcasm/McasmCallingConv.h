//=== McasmCallingConv.h - Mcasm Custom Calling Convention Routines -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the Mcasm Calling Convention that
// aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Mcasm_McasmCALLINGCONV_H
#define LLVM_LIB_TARGET_Mcasm_McasmCALLINGCONV_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

// Return value calling convention
// Based on RISC-V's approach for 32-bit architecture
bool RetCC_Mcasm(unsigned ValNo, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                 Type *OrigTy, CCState &State);

// Main calling convention handler
// Based on RISC-V's approach for handling i64 on 32-bit architecture
bool CC_Mcasm(unsigned ValNo, MVT ValVT, MVT LocVT,
              CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
              Type *OrigTy, CCState &State);

} // End llvm namespace

#endif

