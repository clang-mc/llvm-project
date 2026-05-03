; RUN: llc -mtriple=mcasm -O0 -o - %s | FileCheck %s

%struct.out_t = type { ptr, i32, i32 }

define internal void @bump(ptr noundef %o) #0 {
  %lenp = getelementptr inbounds nuw %struct.out_t, ptr %o, i32 0, i32 2
  %old = load i32, ptr %lenp, align 4
  %new = add i32 %old, 1
  store i32 %new, ptr %lenp, align 4
  ret void
}

define i32 @mini2(ptr noundef %fmt) #0 {
entry:
  %fmt.addr = alloca ptr, align 4
  %buf = alloca [8 x i8], align 1
  %out = alloca %struct.out_t, align 4
  %p = alloca ptr, align 4
  store ptr %fmt, ptr %fmt.addr, align 4
  %fmt.reload = load ptr, ptr %fmt.addr, align 4
  store ptr %fmt.reload, ptr %p, align 4
  %buf0 = getelementptr inbounds [8 x i8], ptr %buf, i32 0, i32 0
  %out.buf = getelementptr inbounds nuw %struct.out_t, ptr %out, i32 0, i32 0
  store ptr %buf0, ptr %out.buf, align 4
  %out.cap = getelementptr inbounds nuw %struct.out_t, ptr %out, i32 0, i32 1
  store i32 8, ptr %out.cap, align 4
  %out.len = getelementptr inbounds nuw %struct.out_t, ptr %out, i32 0, i32 2
  store i32 0, ptr %out.len, align 4
  call void @bump(ptr noundef %out)
  %len = load i32, ptr %out.len, align 4
  ret i32 %len
}

attributes #0 = { noinline nounwind optnone }

; CHECK-LABEL: mini2:
; CHECK:      mov [[BUF:r[0-9]+]], rsp
; CHECK-NEXT: add [[BUF]], 20
; CHECK-NEXT: mov [rsp+[[OUT:[0-9]+]]], [[BUF]]
; CHECK:      add {{r[0-9]+}}, 16
; CHECK:      mov r0, rsp
; CHECK-NEXT: add r0, [[OUT]]
; CHECK-NEXT: call bump
