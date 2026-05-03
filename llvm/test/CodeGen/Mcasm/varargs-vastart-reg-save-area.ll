; RUN: llc -mtriple=mcasm -O0 -o - %s | FileCheck %s

define i32 @take_int(i32 noundef %tag, ...) #0 {
entry:
  %tag.addr = alloca i32, align 4
  %ap = alloca ptr, align 4
  %ret = alloca i32, align 4
  store i32 %tag, ptr %tag.addr, align 4
  call void @llvm.va_start.p0(ptr %ap)
  %v = va_arg ptr %ap, i32
  store i32 %v, ptr %ret, align 4
  call void @llvm.va_end.p0(ptr %ap)
  %r = load i32, ptr %ret, align 4
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

attributes #0 = { noinline nounwind optnone }

; CHECK-LABEL: take_int:
; CHECK:      mov [rsp+[[FIRST:[0-9]+]]], r1
; CHECK:      mov [[AP:r[0-9]+]], rsp
; CHECK-NEXT: add [[AP]], [[FIRST]]
; CHECK-NEXT: mov [rsp+{{[0-9]+}}], [[AP]]
; CHECK:      mov [[CUR:r[0-9]+]], [rsp+{{[0-9]+}}]
; CHECK:      mov [[VAL:r[0-9]+]], {{\[}}[[CUR]]{{\]}}
