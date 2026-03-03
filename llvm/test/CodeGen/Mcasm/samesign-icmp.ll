; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

define i32 @br_ugt(i32 %a, i32 %b) {
entry:
  %c = icmp ugt i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

define i32 @br_samesign_ugt(i32 %a, i32 %b) {
entry:
  %c = icmp samesign ugt i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: br_ugt:
; CHECK: call __bit_xor
; CHECK: call __bit_xor
;
; CHECK-LABEL: br_samesign_ugt:
; CHECK-NOT: call __bit_xor
