; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@.str = private unnamed_addr constant [15 x i8] c"Hello, World!\0A\00", align 4

declare i32 @printf(ptr, ...)
declare void @callee(i32, i32, i32, i32, i32, i32, i32, i32)
declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

define i32 @main() {
entry:
  %call = call i32 (ptr, ...) @printf(ptr @.str)
  ret i32 0
}

define void @vla_call(i32 %n) {
entry:
  %saved = call ptr @llvm.stacksave.p0()
  %a = alloca i32, i32 %n, align 4
  %v0 = load i32, ptr %a, align 4
  call void @callee(i32 %v0, i32 %v0, i32 %v0, i32 %v0,
                    i32 %v0, i32 %v0, i32 %v0, i32 %v0)
  call void @llvm.stackrestore.p0(ptr %saved)
  ret void
}

; CHECK-LABEL: main:
; CHECK: mov{{.*}} str
; CHECK: call{{.*}} printf
; CHECK-NOT: #ADJCALLSTACK

; CHECK-LABEL: vla_call:
; CHECK: call{{.*}} callee
; CHECK-NOT: #ADJCALLSTACK
