; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@x = thread_local global i32 0, align 4

define void @increment() {
entry:
  %v = load i32, ptr @x, align 4
  %inc = add i32 %v, 1
  store i32 %inc, ptr @x, align 4
  ret void
}

; CHECK-LABEL: increment:
; CHECK: mov r0, x
; CHECK: add [r0], 1
