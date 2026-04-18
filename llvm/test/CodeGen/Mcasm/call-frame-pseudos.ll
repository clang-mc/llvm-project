; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@.str = private unnamed_addr constant [15 x i8] c"Hello, World!\0A\00", align 4
@.fmt_s = private unnamed_addr constant [3 x i8] c"%s\00", align 4
@.fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 4
@.hello = private unnamed_addr constant [6 x i8] c"Hello\00", align 4

declare i32 @printf(ptr, ...)
declare void @callee(i32, i32, i32, i32, i32, i32, i32, i32)
declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

define i32 @main() {
entry:
  %call = call i32 (ptr, ...) @printf(ptr @.str)
  ret i32 0
}

define i32 @main_fmt_str() {
entry:
  %call = call i32 (ptr, ...) @printf(ptr @.fmt_s, ptr @.hello)
  ret i32 0
}

define i32 @main_fmt_int() {
entry:
  %call = call i32 (ptr, ...) @printf(ptr @.fmt_d, i32 1)
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
; CHECK-NOT: sub rsp

; CHECK-LABEL: main_fmt_str:
; CHECK: mov r0, fmt_s
; CHECK: mov r1, hello
; CHECK: call{{.*}} printf
; CHECK-NOT: sub rsp

; CHECK-LABEL: main_fmt_int:
; CHECK: mov r0, fmt_d
; CHECK: mov r1, 1
; CHECK: call{{.*}} printf
; CHECK-NOT: sub rsp

; CHECK-LABEL: vla_call:
; CHECK: mov t1, rsp
; CHECK: call{{.*}} callee
; CHECK: mov rsp, t1
