; RUN: llc -mtriple=mcasm -O3 -o - %s | FileCheck %s

define i32 @sel_i32(i32 %x, i32 %a, i32 %b) {
entry:
  %c = icmp slt i32 %x, 0
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; CHECK-LABEL: sel_i32:
; CHECK: jl
; CHECK-NOT: __bit_
