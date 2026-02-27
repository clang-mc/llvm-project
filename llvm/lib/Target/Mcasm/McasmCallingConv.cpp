//===-- McasmCallingConv.cpp - Mcasm Custom CC Routines ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the Mcasm Calling Convention.
// Based on RISC-V's approach for handling i64 on 32-bit architecture.
//
//===----------------------------------------------------------------------===//

#include "McasmCallingConv.h"
#include "McasmRegisterInfo.h"
#include "McasmSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/DataLayout.h"

// Include register enum definitions
#define GET_REGINFO_ENUM
#include "McasmGenRegisterInfo.inc"

using namespace llvm;

// Argument registers for Mcasm calling convention: r0-r7
static const MCPhysReg ArgGPRs[] = {
    Mcasm::r0, Mcasm::r1, Mcasm::r2, Mcasm::r3,
    Mcasm::r4, Mcasm::r5, Mcasm::r6, Mcasm::r7};

// Pass a 2*32-bit argument (i64) that has been split into two 32-bit values
// through registers or the stack as necessary.
// Based on RISC-V's CC_RISCVAssign2XLen.
static bool CC_McasmAssign2x32(CCState &State, CCValAssign VA1,
                                ISD::ArgFlagsTy ArgFlags1, unsigned ValNo2,
                                MVT ValVT2, MVT LocVT2,
                                ISD::ArgFlagsTy ArgFlags2) {
  unsigned XLenInBytes = 4; // Mcasm is 32-bit

  // Try to allocate first register (low 32 bits)
  if (MCRegister Reg = State.AllocateReg(ArgGPRs)) {
    // At least one half can be passed via register
    State.addLoc(CCValAssign::getReg(VA1.getValNo(), VA1.getValVT(), Reg,
                                     VA1.getLocVT(), CCValAssign::Full));
  } else {
    // Both halves must be passed on the stack with proper alignment
    Align StackAlign = Align(XLenInBytes);
    State.addLoc(
        CCValAssign::getMem(VA1.getValNo(), VA1.getValVT(),
                            State.AllocateStack(XLenInBytes, StackAlign),
                            VA1.getLocVT(), CCValAssign::Full));
    State.addLoc(CCValAssign::getMem(
        ValNo2, ValVT2, State.AllocateStack(XLenInBytes, Align(XLenInBytes)),
        LocVT2, CCValAssign::Full));
    return false;
  }

  // Try to allocate second register (high 32 bits)
  if (MCRegister Reg = State.AllocateReg(ArgGPRs)) {
    // The second half can also be passed via register
    State.addLoc(
        CCValAssign::getReg(ValNo2, ValVT2, Reg, LocVT2, CCValAssign::Full));
  } else {
    // The second half is passed via the stack
    State.addLoc(CCValAssign::getMem(
        ValNo2, ValVT2, State.AllocateStack(XLenInBytes, Align(XLenInBytes)),
        LocVT2, CCValAssign::Full));
  }

  return false;
}

// Main calling convention handler for Mcasm
// Based on RISC-V's CC_RISCV implementation
bool llvm::CC_Mcasm(unsigned ValNo, MVT ValVT, MVT LocVT,
                    CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                    Type *OrigTy, CCState &State) {
  // Promote i1, i8, i16 to i32
  if (LocVT == MVT::i1 || LocVT == MVT::i8 || LocVT == MVT::i16) {
    LocVT = MVT::i32;
    if (ArgFlags.isSExt())
      LocInfo = CCValAssign::SExt;
    else if (ArgFlags.isZExt())
      LocInfo = CCValAssign::ZExt;
    else
      LocInfo = CCValAssign::AExt;
  }

  // Handle f32 as i32 (soft float)
  if (LocVT == MVT::f32) {
    LocVT = MVT::i32;
    LocInfo = CCValAssign::BCvt;
  }

  // Handle f64 on stack (8 bytes, 4-byte aligned)
  if (LocVT == MVT::f64) {
    MCRegister Reg = State.AllocateReg(ArgGPRs);
    if (!Reg) {
      int64_t StackOffset = State.AllocateStack(8, Align(4));
      State.addLoc(
          CCValAssign::getMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
      return false;
    }
    // f64 passed in register pair
    LocVT = MVT::i32;
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    MCRegister HiReg = State.AllocateReg(ArgGPRs);
    if (HiReg) {
      State.addLoc(
          CCValAssign::getCustomReg(ValNo, ValVT, HiReg, LocVT, LocInfo));
    } else {
      int64_t StackOffset = State.AllocateStack(4, Align(4));
      State.addLoc(
          CCValAssign::getCustomMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
    }
    return false;
  }

  SmallVectorImpl<CCValAssign> &PendingLocs = State.getPendingLocs();
  SmallVectorImpl<ISD::ArgFlagsTy> &PendingArgFlags =
      State.getPendingArgFlags();

  assert(PendingLocs.size() == PendingArgFlags.size() &&
         "PendingLocs and PendingArgFlags out of sync");

  // If the split argument only had two elements, it should be passed directly
  // in registers or on the stack.
  // This handles i64 which LLVM splits into two i32 values.
  if (ValVT.isScalarInteger() && ArgFlags.isSplitEnd() &&
      PendingLocs.size() <= 1) {
    assert(PendingLocs.size() == 1 && "Unexpected PendingLocs.size()");
    // Apply the normal calling convention rules to the first half of the
    // split argument.
    CCValAssign VA = PendingLocs[0];
    ISD::ArgFlagsTy AF = PendingArgFlags[0];
    PendingLocs.clear();
    PendingArgFlags.clear();
    return CC_McasmAssign2x32(State, VA, AF, ValNo, ValVT, LocVT, ArgFlags);
  }

  // Split arguments might be passed indirectly, so keep track of the pending
  // values.
  if (ValVT.isScalarInteger() &&
      (ArgFlags.isSplit() || !PendingLocs.empty())) {
    PendingLocs.push_back(
        CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));
    PendingArgFlags.push_back(ArgFlags);
    if (!ArgFlags.isSplitEnd()) {
      return false;
    }
  }

  // Allocate to a register if possible, or else a stack slot
  MCRegister Reg;
  unsigned StoreSizeBytes = 4; // 32-bit
  Align StackAlign = Align(4);

  if (LocVT == MVT::i32 || LocVT.isInteger()) {
    Reg = State.AllocateReg(ArrayRef<MCPhysReg>(ArgGPRs));
    LocVT = MVT::i32;
  }

  if (Reg) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return false;
  }

  // Assign to stack
  int64_t StackOffset = State.AllocateStack(StoreSizeBytes, StackAlign);
  State.addLoc(CCValAssign::getMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
  return false;
}

// Return value calling convention
bool llvm::RetCC_Mcasm(unsigned ValNo, MVT ValVT, MVT LocVT,
                       CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                       Type *OrigTy, CCState &State) {
  // Return values larger than 2*32-bit must be passed indirectly
  if (ValNo > 1)
    return true;

  // Promote i1 to i8 for return values
  if (LocVT == MVT::i1) {
    LocVT = MVT::i8;
    LocInfo = CCValAssign::ZExt;
  }

  // Promote i8, i16 to i32
  if (LocVT == MVT::i8 || LocVT == MVT::i16) {
    LocVT = MVT::i32;
    if (ArgFlags.isSExt())
      LocInfo = CCValAssign::SExt;
    else if (ArgFlags.isZExt())
      LocInfo = CCValAssign::ZExt;
    else
      LocInfo = CCValAssign::AExt;
  }

  // Handle f32 as i32 (soft float)
  if (LocVT == MVT::f32) {
    LocVT = MVT::i32;
    LocInfo = CCValAssign::BCvt;
  }

  // f64 is returned via sret (handled by frontend)
  if (LocVT == MVT::f64) {
    return true;
  }

  SmallVectorImpl<CCValAssign> &PendingLocs = State.getPendingLocs();
  SmallVectorImpl<ISD::ArgFlagsTy> &PendingArgFlags =
      State.getPendingArgFlags();

  // Handle i64 split into two i32s
  if (ValVT.isScalarInteger() && ArgFlags.isSplitEnd() &&
      PendingLocs.size() <= 1) {
    assert(PendingLocs.size() == 1 && "Unexpected PendingLocs.size()");
    CCValAssign VA = PendingLocs[0];
    ISD::ArgFlagsTy AF = PendingArgFlags[0];
    PendingLocs.clear();
    PendingArgFlags.clear();

    // Return registers: rax (low), t0 (high)
    static const MCPhysReg RetRegs[] = {Mcasm::rax, Mcasm::t0};

    // Allocate low register
    MCRegister Reg1 = State.AllocateReg(RetRegs[0]);
    if (!Reg1)
      return true; // Cannot return via registers

    State.addLoc(CCValAssign::getReg(VA.getValNo(), VA.getValVT(), Reg1,
                                     VA.getLocVT(), CCValAssign::Full));

    // Allocate high register
    MCRegister Reg2 = State.AllocateReg(RetRegs[1]);
    if (!Reg2)
      return true; // Cannot return via registers

    State.addLoc(
        CCValAssign::getReg(ValNo, ValVT, Reg2, LocVT, CCValAssign::Full));
    return false;
  }

  if (ValVT.isScalarInteger() &&
      (ArgFlags.isSplit() || !PendingLocs.empty())) {
    PendingLocs.push_back(
        CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));
    PendingArgFlags.push_back(ArgFlags);
    if (!ArgFlags.isSplitEnd()) {
      return false;
    }
  }

  // Simple case: return in rax
  if (LocVT == MVT::i32 || LocVT.isInteger()) {
    // Try to allocate return register (rax first, then t0, then t1)
    MCRegister Reg = State.AllocateReg(Mcasm::rax);
    if (!Reg)
      Reg = State.AllocateReg(Mcasm::t0);
    if (!Reg)
      Reg = State.AllocateReg(Mcasm::t1);

    if (Reg) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, MVT::i32, LocInfo));
      return false;
    }
  }

  return true; // Cannot handle this return value
}
