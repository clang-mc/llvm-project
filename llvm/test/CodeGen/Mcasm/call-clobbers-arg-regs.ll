; RUN: llc -mtriple=mcasm -O2 -o - %s | FileCheck %s

@str = private unnamed_addr constant [14 x i8] c"Hello, World!\00"

declare i32 @puts(ptr)

define i32 @main() {
; CHECK-LABEL: main:
; CHECK:       mov r0, str
; CHECK-NEXT:  call puts
; CHECK-NEXT:  mov r0, str
; CHECK-NEXT:  call puts
  %first = call i32 @puts(ptr @str)
  %second = call i32 @puts(ptr @str)
  ret i32 0
}
