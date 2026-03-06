; RUN: llc -mtriple=mcasm -O3 -o - %s | FileCheck %s

declare i32 @f0()
declare i32 @f1()
declare i32 @f2()
declare i32 @f3()
declare i32 @f4()
declare i32 @f5()
declare i32 @f6()
declare i32 @f7()
declare i32 @f8()
declare i32 @f9()
declare i32 @f10()
declare i32 @f11()
declare i32 @f12()
declare i32 @f13()
declare i32 @f14()
declare i32 @f15()
declare i32 @fdefault()

define i32 @no_jumptable_switch(i32 %x) {
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
    i32 3, label %case3
    i32 4, label %case4
    i32 5, label %case5
    i32 6, label %case6
    i32 7, label %case7
    i32 8, label %case8
    i32 9, label %case9
    i32 10, label %case10
    i32 11, label %case11
    i32 12, label %case12
    i32 13, label %case13
    i32 14, label %case14
    i32 15, label %case15
  ]

case0:
  %r0 = call i32 @f0()
  ret i32 %r0
case1:
  %r1 = call i32 @f1()
  ret i32 %r1
case2:
  %r2 = call i32 @f2()
  ret i32 %r2
case3:
  %r3 = call i32 @f3()
  ret i32 %r3
case4:
  %r4 = call i32 @f4()
  ret i32 %r4
case5:
  %r5 = call i32 @f5()
  ret i32 %r5
case6:
  %r6 = call i32 @f6()
  ret i32 %r6
case7:
  %r7 = call i32 @f7()
  ret i32 %r7
case8:
  %r8 = call i32 @f8()
  ret i32 %r8
case9:
  %r9 = call i32 @f9()
  ret i32 %r9
case10:
  %r10 = call i32 @f10()
  ret i32 %r10
case11:
  %r11 = call i32 @f11()
  ret i32 %r11
case12:
  %r12 = call i32 @f12()
  ret i32 %r12
case13:
  %r13 = call i32 @f13()
  ret i32 %r13
case14:
  %r14 = call i32 @f14()
  ret i32 %r14
case15:
  %r15 = call i32 @f15()
  ret i32 %r15
default:
  %rd = call i32 @fdefault()
  ret i32 %rd
}

; CHECK-LABEL: no_jumptable_switch:
; CHECK-NOT: JTI
; CHECK-NOT: .rodata
; CHECK-NOT: .long
