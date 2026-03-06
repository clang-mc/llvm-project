; RUN: llc -mtriple=mcasm -filetype=asm %s -o - | FileCheck %s

define i32 @__ledf2(i32 %a, i32 %b) {
entry:
  ret i32 0
}

@__cmpdf2 = alias i32 (i32, i32), ptr @__ledf2

define i32 @foo(i32 %x, i32 %y) {
entry:
  %r = call i32 @__cmpdf2(i32 %x, i32 %y)
  ret i32 %r
}

; CHECK: #define __cmpdf2 __ledf2
; CHECK-NOT: __cmpdf2 = __ledf2

