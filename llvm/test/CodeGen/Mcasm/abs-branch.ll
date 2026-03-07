; RUN: llc -mtriple=mcasm -O3 -o - %s | FileCheck %s

define i32 @abs_i32(i32 %x) {
entry:
  %r = tail call i32 @llvm.abs.i32(i32 %x, i1 false)
  ret i32 %r
}

declare i32 @llvm.abs.i32(i32, i1 immarg)

; CHECK-LABEL: abs_i32:
; CHECK: j
; CHECK: sub
; CHECK-NOT: __bit_
