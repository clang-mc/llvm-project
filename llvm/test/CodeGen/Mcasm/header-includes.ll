; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

target triple = "mcasm"

; CHECK: #include "_ll_std"
; CHECK-NEXT: #include "_ll_libc"
; CHECK-NEXT: #include "_ll_crt"
; CHECK-EMPTY:

define i64 @main() {
entry:
  ret i64 0
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"mcasm-libc-include", i32 1}
