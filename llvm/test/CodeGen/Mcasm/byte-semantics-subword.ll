; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@bytes = global [4 x i8] c"ABCD", align 4
@halves = global [4 x i16] [i16 1, i16 2, i16 3, i16 4], align 4
@mix = global { i8, i16, i8 } { i8 1, i16 515, i8 4 }, align 4
@flag = global i1 true, align 4

define i32 @load_byte_1() {
entry:
  %p = getelementptr inbounds [4 x i8], ptr @bytes, i32 0, i32 1
  %v = load i8, ptr %p, align 1
  %z = zext i8 %v to i32
  ret i32 %z
}

define i32 @load_i1() {
entry:
  %v = load i1, ptr @flag, align 1
  %z = zext i1 %v to i32
  ret i32 %z
}

define i32 @load_i1_branch() {
entry:
  %v = load i1, ptr @flag, align 1
  br i1 %v, label %true, label %false

true:
  ret i32 11

false:
  ret i32 0
}

define i32 @load_half_1() {
entry:
  %p = getelementptr inbounds [4 x i16], ptr @halves, i32 0, i32 1
  %v = load i16, ptr %p, align 2
  %z = zext i16 %v to i32
  ret i32 %z
}

define void @store_byte_1() {
entry:
  %p = getelementptr inbounds [4 x i8], ptr @bytes, i32 0, i32 1
  store i8 90, ptr %p, align 1
  ret void
}

define void @store_i1(i1 %v) {
entry:
  store i1 %v, ptr @flag, align 1
  ret void
}

define void @store_half_1() {
entry:
  %p = getelementptr inbounds [4 x i16], ptr @halves, i32 0, i32 1
  store i16 4660, ptr %p, align 2
  ret void
}

; CHECK-LABEL: load_byte_1:
; CHECK: mov [[B:r[0-9]+]], bytes
; CHECK: add [[B]], 1
; CHECK-NOT: __bit_shr
; CHECK-NOT: bytes+

; The i1 load masks with (& 1), now lowered to native arithmetic instead of a
; __bit_and libcall.
; CHECK-LABEL: load_i1:
; CHECK: mov [[F:r[0-9]+]], flag
; CHECK: mov {{[a-z0-9]+}}, {{\[}}[[F]]{{\]}}
; CHECK-NOT: call __bit_and

; CHECK-LABEL: load_i1_branch:
; CHECK: mov [[AF:r[0-9]+]], flag
; CHECK: mov {{[a-z0-9]+}}, {{\[}}[[AF]]{{\]}}
; CHECK-NOT: call __bit_and

; CHECK-LABEL: load_half_1:
; CHECK: mov [[H:r[0-9]+]], halves
; CHECK: add [[H]], 2
; CHECK-NOT: __bit_shr
; CHECK-NOT: halves+

; CHECK-LABEL: store_byte_1:
; CHECK: mov [[SB:[rx][0-9]+]], bytes
; CHECK: add [[SB]], 1
; CHECK: mov
; CHECK: mov [{{[rx][0-9]+}}], {{[a-z0-9]+}}

; CHECK-LABEL: store_i1:
; CHECK: mov [[SF:[rx][0-9]+]], flag
; CHECK: mov [{{[rx][0-9]+}}], {{[a-z0-9]+}}

; CHECK-LABEL: store_half_1:
; CHECK: mov [[SH:[rx][0-9]+]], halves
; CHECK: add [[SH]], 2
; CHECK: mov
; CHECK: mov [{{[rx][0-9]+}}], {{[a-z0-9]+}}

; CHECK: static bytes [65, 66, 67, 68]
; CHECK: static halves [1, 0, 2, 0, 3, 0, 4, 0]
; CHECK: static mix [1, 0, 515, 0, 4, 0]
; CHECK: static flag [1]
