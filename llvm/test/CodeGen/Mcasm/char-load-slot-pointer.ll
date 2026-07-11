; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@str = private unnamed_addr constant [2 x i8] c"A\00", align 4

define i32 @main() {
entry:
  %v = load i8, ptr @str, align 1
  %z = zext i8 %v to i32
  ret i32 %z
}

; CHECK-LABEL: main:
; CHECK: mov [[P:r[0-9]+]], str
; CHECK-NEXT: mov {{[a-z0-9]+}}, {{\[}}[[P]]{{\]}}
; The byte is extracted with native arithmetic (mul/div), never a __bit_shr
; libcall.
; CHECK-NOT: __bit_shr
; CHECK: static str [65, 0]
