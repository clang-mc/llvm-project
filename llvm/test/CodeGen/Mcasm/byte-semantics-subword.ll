; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@bytes = global [4 x i8] c"ABCD", align 4
@halves = global [4 x i16] [i16 1, i16 2, i16 3, i16 4], align 4
@mix = global { i8, i16, i8 } { i8 1, i16 515, i8 4 }, align 4

define i32 @load_byte_1() {
entry:
  %p = getelementptr inbounds [4 x i8], ptr @bytes, i32 0, i32 1
  %v = load i8, ptr %p, align 1
  %z = zext i8 %v to i32
  ret i32 %z
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

define void @store_half_1() {
entry:
  %p = getelementptr inbounds [4 x i16], ptr @halves, i32 0, i32 1
  store i16 4660, ptr %p, align 2
  ret void
}

; CHECK-LABEL: load_byte_1:
; CHECK: mov [[B:r[0-9]+]], bytes
; CHECK: add [[B]], 1
; CHECK-NOT: bytes+

; CHECK-LABEL: load_half_1:
; CHECK: mov [[H:r[0-9]+]], halves
; CHECK: add [[H]], 2
; CHECK-NOT: halves+

; CHECK-LABEL: store_byte_1:
; CHECK: mov [[SB:r[0-9]+]], bytes
; CHECK: add [[SB]], 1
; CHECK: mov
; CHECK: mov [{{r[0-9]+}}], rax

; CHECK-LABEL: store_half_1:
; CHECK: mov [[SH:r[0-9]+]], halves
; CHECK: add [[SH]], 2
; CHECK: mov
; CHECK: mov [{{r[0-9]+}}], rax

; CHECK: static bytes [1145258561]
; CHECK: static halves [131073, 262147]
; CHECK: static mix [33751041, 4]
