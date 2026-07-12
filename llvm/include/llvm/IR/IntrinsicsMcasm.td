//===- IntrinsicsMcasm.td - Defines Mcasm intrinsics -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the Mcasm-specific (Minecraft target) intrinsics.
//
//===----------------------------------------------------------------------===//

// MC scratch-string build primitives. The clang builtins __builtin_mcf_str_*
// lower to these unconditionally; the folding of a compile-time-constant
// string operand to O(1) `data modify ... set value` commands happens in the
// backend (the operand only becomes constant after LTO/SCCP). See
// clang-mc/tools/foo-benchmark/TASK-const-string-fold.md.
//
// Memory effects: memory(argmem: readwrite). arg0 (the string) is read-only;
// arg1 (&__mc_state) is the coarse MC-state ordering shadow, read+written, so
// these intrinsics order precisely against the surrounding scratch inline asm
// that also touches __mc_state -- without a blunt full-memory clobber.
let TargetPrefix = "mcasm" in {
  def int_mcasm_str_begin
      : Intrinsic<[], [llvm_ptr_ty /*s*/, llvm_ptr_ty /*mc_state*/],
                  [IntrArgMemOnly, IntrWillReturn, ReadOnly<ArgIndex<0>>]>;
  def int_mcasm_str_append
      : Intrinsic<[], [llvm_ptr_ty /*s*/, llvm_ptr_ty /*mc_state*/],
                  [IntrArgMemOnly, IntrWillReturn, ReadOnly<ArgIndex<0>>]>;
}
