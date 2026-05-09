; RUN: llc -mtriple=mcasm -O0 -o - %s | FileCheck %s

%struct.TextHolder = type { ptr }

@holder = internal global %struct.TextHolder { ptr @.str }, align 4
@holder_off = internal global %struct.TextHolder {
  ptr getelementptr inbounds ([4 x i8], ptr @.str, i32 0, i32 1)
}, align 4
@.str = private unnamed_addr constant [4 x i8] c"abc\00", align 4

define i32 @main() {
entry:
  %p0 = load ptr, ptr @holder, align 4
  %c0 = load i8, ptr %p0, align 1
  %z0 = zext i8 %c0 to i32
  %p1 = load ptr, ptr @holder_off, align 4
  %c1 = load i8, ptr %p1, align 1
  %z1 = zext i8 %c1 to i32
  %sum = add i32 %z0, %z1
  ret i32 %sum
}

; CHECK-LABEL: _start:
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: mov	r0, holder
; CHECK-NEXT: mov	r1, str
; CHECK-NEXT: mov	[r0], r1
; CHECK-NEXT: mov	r0, holder_off
; CHECK-NEXT: mov	r1, str
; CHECK-NEXT: add	r1, 1
; CHECK-NEXT: mov	[r0], r1
; CHECK-NEXT: mov	r0, 0
; CHECK-NEXT: mov	r1, 0
; CHECK-NEXT: call	main

; CHECK: static holder [0, 0, 0, 0]
; CHECK: static holder_off [0, 0, 0, 0]
; CHECK: static str [97, 98, 99, 0]
