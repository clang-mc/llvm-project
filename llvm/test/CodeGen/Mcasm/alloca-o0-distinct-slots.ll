; RUN: llc -mtriple=mcasm -O0 < %s | FileCheck %s

@g_result = dso_local global i32 0, align 4

define dso_local dllexport void @store_loop() {
entry:
  %x = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %x, align 4
  store i32 1, ptr %i, align 4
  br label %loop

loop:
  %iv = load i32, ptr %i, align 4
  %cmp = icmp ult i32 %iv, 5
  br i1 %cmp, label %body, label %exit

body:
  %xv = load i32, ptr %x, align 4
  %xnext = add nsw i32 %xv, 1
  store i32 %xnext, ptr %x, align 4
  %inext = add i32 %iv, 1
  store i32 %inext, ptr %i, align 4
  br label %loop

exit:
  %ret = load i32, ptr %x, align 4
  store volatile i32 %ret, ptr @g_result, align 4
  ret void
}

; CHECK-LABEL: store_loop:
; CHECK: sub rsp,
; CHECK-DAG: mov [rsp+8], 0
; CHECK-DAG: mov [rsp+4], 1
; CHECK: mov r0, [rsp+4]
; CHECK: mov r1, [rsp+8]
; CHECK: mov [rsp+8], r1
; CHECK: mov [rsp+4], r0
; CHECK: mov r1, [rsp+8]
