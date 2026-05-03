; RUN: llc -mtriple=mcasm -O0 -o - %s | FileCheck %s

%struct.out_t = type { ptr, i32, i32 }

define i32 @main() #0 {
  %ret = alloca i32, align 4
  %buf = alloca [64 x i8], align 1
  %out = alloca %struct.out_t, align 4
  store i32 0, ptr %ret, align 4
  %buf0 = getelementptr inbounds [64 x i8], ptr %buf, i32 0, i32 0
  %out.buf = getelementptr inbounds nuw %struct.out_t, ptr %out, i32 0, i32 0
  store ptr %buf0, ptr %out.buf, align 4
  %out.cap = getelementptr inbounds nuw %struct.out_t, ptr %out, i32 0, i32 1
  store i32 64, ptr %out.cap, align 4
  %out.len = getelementptr inbounds nuw %struct.out_t, ptr %out, i32 0, i32 2
  store i32 0, ptr %out.len, align 4
  %loaded.buf = load ptr, ptr %out.buf, align 4
  store i8 0, ptr %loaded.buf, align 1
  %cap = load i32, ptr %out.cap, align 4
  ret i32 %cap
}

attributes #0 = { noinline nounwind optnone }

; CHECK-LABEL: main:
; CHECK:      sub rsp,
; CHECK:      add [[CAP:r[0-9]+]], 12
; CHECK:      mov {{.*}}, [[CAP]]
; CHECK:      mov {{\[}}[[CAP]]{{\]}}, 64
; CHECK:      mov rax, [{{.*}}]
