; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

define i32 @ult(i32 %a, i32 %b) {
; CHECK-LABEL: ult:
; CHECK: call{{.*}}__bit_not
; CHECK: call{{.*}}__bit_and
; CHECK: call{{.*}}__bit_xor
; CHECK: call{{.*}}__bit_not
; CHECK: sub
; CHECK: call{{.*}}__bit_and
; CHECK: call{{.*}}__bit_or
; CHECK: call{{.*}}__bit_shr
entry:
  %cmp = icmp ult i32 %a, %b
  %z = zext i1 %cmp to i32
  ret i32 %z
}
