; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

target triple = "mcasm"

; CHECK: #include "_ll_std"
; CHECK-NEXT: #include "_ll_libc"
; CHECK-NOT: #include "_ll_crt"
; CHECK-EMPTY:

define i64 @foo() {
entry:
  ret i64 1
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"mcasm-libc-include", i32 1}
