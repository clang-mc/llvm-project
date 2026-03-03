; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

define i64 @test_sdiv_i64(i64 %a, i64 %b) {
; CHECK-LABEL: test_sdiv_i64:
; CHECK: call{{.*}}__divdi3
entry:
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @test_udiv_i64(i64 %a, i64 %b) {
; CHECK-LABEL: test_udiv_i64:
; CHECK: call{{.*}}__udivdi3
entry:
  %r = udiv i64 %a, %b
  ret i64 %r
}

define i64 @test_srem_i64(i64 %a, i64 %b) {
; CHECK-LABEL: test_srem_i64:
; CHECK: call{{.*}}__moddi3
entry:
  %r = srem i64 %a, %b
  ret i64 %r
}

define i64 @test_urem_i64(i64 %a, i64 %b) {
; CHECK-LABEL: test_urem_i64:
; CHECK: call{{.*}}__umoddi3
entry:
  %r = urem i64 %a, %b
  ret i64 %r
}
