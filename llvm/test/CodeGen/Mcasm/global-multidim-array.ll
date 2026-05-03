; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@and4 = dso_local global [1 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 3]], align 4
@zeros = internal global [2 x [2 x i32]] zeroinitializer, align 4
@nested = internal global { [2 x i16], i8, [2 x i32] } { [2 x i16] [i16 7, i16 8], i8 9, [2 x i32] [i32 10, i32 11] }, align 4
@undefs = internal global [2 x [2 x i8]] undef, align 4

define i32 @main() {
entry:
  %v = load i32, ptr getelementptr inbounds ([3 x i32], ptr @and4, i32 0, i32 2), align 4
  ret i32 %v
}

; CHECK: static and4 [1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0]
; CHECK: static zeros [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
; CHECK: static nested [7, 0, 8, 0, 9, 0, 0, 0, 10, 0, 0, 0, 11, 0, 0, 0]
; CHECK: static undefs [0, 0, 0, 0]
