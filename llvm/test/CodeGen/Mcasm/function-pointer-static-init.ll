; RUN: llc -mtriple=mcasm -O0 -o - %s | FileCheck %s

@table = global [2 x ptr] [ptr @inc, ptr @dbl], align 4

define i32 @inc(i32 %x) {
entry:
  %add = add nsw i32 %x, 1
  ret i32 %add
}

define i32 @dbl(i32 %x) {
entry:
  %mul = shl nsw i32 %x, 1
  ret i32 %mul
}

define i32 @main() {
entry:
  %p0 = load ptr, ptr @table, align 4
  %r0 = call i32 %p0(i32 3)
  %p1.addr = getelementptr inbounds [2 x ptr], ptr @table, i32 0, i32 1
  %p1 = load ptr, ptr %p1.addr, align 4
  %r1 = call i32 %p1(i32 4)
  %sum = add nsw i32 %r0, %r1
  ret i32 %sum
}

; CHECK-LABEL: _start:
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: mov	r0, table
; CHECK-NEXT: movd	[r0], inc
; CHECK-NEXT: movd	[r0+4], dbl
; CHECK-NEXT: mov	r0, 0
; CHECK-NEXT: mov	r1, 0
; CHECK-NEXT: call	main

; CHECK: static table [0, 0, 0, 0, 0, 0, 0, 0]
