; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)

define i32 @sum_first_two(i32 %fixed, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %ap, i32
  %sum = add i32 %a, %b
  call void @llvm.va_end(ptr %ap)
  ret i32 %sum
}

define i32 @read_i64(i32 %fixed, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  %lo = trunc i64 %v to i32
  ret i32 %lo
}

define i32 @copy_and_read(i32 %fixed, ...) {
entry:
  %ap = alloca ptr, align 4
  %ap2 = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  call void @llvm.va_copy(ptr %ap2, ptr %ap)
  %v = va_arg ptr %ap2, i32
  call void @llvm.va_end(ptr %ap2)
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}

define i32 @stack_vararg_after_8_regs(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                                      i32 %a4, i32 %a5, i32 %a6, i32 %a7, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}

declare i32 @vcallee(i32, ...)

define i32 @vararg_call_shadow(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4,
                               i32 %a5, i32 %a6, i32 %a7, i32 %a8, i32 %a9) {
entry:
  %r = call i32 (i32, ...) @vcallee(i32 1, i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                                     i32 %a4, i32 %a5, i32 %a6, i32 %a7,
                                     i32 %a8, i32 %a9)
  ret i32 %r
}

; CHECK-LABEL: sum_first_two:
; CHECK: sub rsp, 32
; CHECK: mov [rsp+1], r1
; CHECK: mov [rsp+7], r0
; CHECK-LABEL: read_i64:
; CHECK: sub rsp, 32
; CHECK: mov [rsp+1], r1
; CHECK: mov [rsp+7], r0
; CHECK-LABEL: copy_and_read:
; CHECK: sub rsp, 36
; CHECK: mov [rsp+8], rsp
; CHECK: mov [rsp+7], r0
; CHECK-LABEL: stack_vararg_after_8_regs:
; CHECK: sub rsp, 4
; CHECK: add r0, 4
; CHECK: mov [rsp], r0
; CHECK: mov rax, [rsp]
; CHECK-LABEL: vararg_call_shadow:
; CHECK: sub rsp, 12
; CHECK: add r0, 8
; CHECK: mov [r0], t2
; CHECK: add t3, 4
; CHECK: mov [t3], t1
; CHECK: call vcallee
