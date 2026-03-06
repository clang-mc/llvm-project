; RUN: opt -passes=instcombine -S %s | FileCheck %s
; RUN: opt -passes=instsimplify -S %s | FileCheck %s --check-prefix=SIMPLIFY

target datalayout = "e-p:32:32-i8:32-i16:32-i32:32-i64:32-f32:32-f64:32-a:0:32-n32"

@g = global [1024 x i8] zeroinitializer, align 4

define i8 @f() {
entry:
  %p = getelementptr inbounds i8, ptr @g, i32 1024
  %v = load i8, ptr %p, align 4
  ret i8 %v
}

; CHECK-LABEL: @f(
; CHECK: getelementptr inbounds{{( nuw)?}} (i8, ptr @g, i32 1024)
; CHECK-NOT: i32 4096
; CHECK-NOT: i32 16384

; SIMPLIFY-LABEL: @f(
; SIMPLIFY: getelementptr inbounds{{( nuw)?}} (i8, ptr @g, i32 1024)
; SIMPLIFY-NOT: i32 4096
