//===-- McasmCallingConv.td - Calling Conventions for Mcasm --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is now a stub - the actual calling convention is implemented
// in C++ in McasmCallingConv.cpp (following RISC-V's approach).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Callee-Saved Registers
//===----------------------------------------------------------------------===//

// No callee-saved registers (for leaf functions or special cases)
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// Standard callee-saved registers: x0-x15
// These registers must be preserved across function calls
def CSR_Mcasm_32 : CalleeSavedRegs<(add x0, x1, x2, x3, x4, x5, x6, x7,
                                        x8, x9, x10, x11, x12, x13, x14, x15)>;

// All registers saved (for special cases like interrupt handlers)
def CSR_Mcasm_32_AllRegs : CalleeSavedRegs<(add r0, r1, r2, r3, r4, r5, r6, r7,
                                                t0, t1, t2, t3, t4, t5, t6, t7,
                                                x0, x1, x2, x3, x4, x5, x6, x7,
                                                x8, x9, x10, x11, x12, x13, x14, x15,
                                                rax)>;
