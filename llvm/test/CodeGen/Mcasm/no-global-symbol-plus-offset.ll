; RUN: llc -mtriple=mcasm -O3 -o - %s | FileCheck %s

@g = internal global [1024 x i8] zeroinitializer, align 4

; Make sure global offset is materialized with an ADD, not as symbol+offset.
define i32 @const_gep_load() {
entry:
  %p = getelementptr inbounds [1024 x i8], ptr @g, i32 0, i32 1
  %v = load i8, ptr %p, align 1
  %x = zext i8 %v to i32
  ret i32 %x
}

; CHECK-LABEL: const_gep_load:
; CHECK: mov [[BASE:r[0-9]+]], g
; CHECK-NOT: g+
