; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s
;
; Constant-operand bitwise ops must lower to native mul/div/add/sub instead of
; __bit_* software libcalls (see TASK-backend-bitwise-lowering).  Variable
; shift amounts still use the libcalls.

; A constant left shift is a native multiply by 2^n.
; CHECK-LABEL: shl_const:
; CHECK: mul {{r[a-z0-9]+}}, 8
; CHECK-NOT: __bit_shl
define i32 @shl_const(i32 %x) {
  %r = shl i32 %x, 3
  ret i32 %r
}

; A constant logical right shift uses a native divide, no __bit_shr.
; CHECK-LABEL: shr_const:
; CHECK: div {{r[a-z0-9]+}}, 8
; CHECK-NOT: __bit_shr
define i32 @shr_const(i32 %x) {
  %r = lshr i32 %x, 3
  ret i32 %r
}

; A constant arithmetic right shift uses a native divide, no __bit_sar.
; CHECK-LABEL: sra_const:
; CHECK: div {{r[a-z0-9]+}}, 8
; CHECK-NOT: __bit_sar
define i32 @sra_const(i32 %x) {
  %r = ashr i32 %x, 3
  ret i32 %r
}

; A constant low-bit mask lowers to shift+arithmetic, no __bit_and.
; CHECK-LABEL: and_lowmask:
; CHECK-NOT: __bit_and
define i32 @and_lowmask(i32 %x) {
  %r = and i32 %x, 2047
  ret i32 %r
}

; A constant OR lowers via native arithmetic, no __bit_or.
; CHECK-LABEL: or_const:
; CHECK-NOT: __bit_or
define i32 @or_const(i32 %x) {
  %r = or i32 %x, 1792
  ret i32 %r
}

; A constant XOR lowers via native arithmetic, no __bit_xor.
; CHECK-LABEL: xor_const:
; CHECK-NOT: __bit_xor
define i32 @xor_const(i32 %x) {
  %r = xor i32 %x, 240
  ret i32 %r
}

; ~x lowers to -1 - x inline, no __bit_not.
; CHECK-LABEL: bit_not:
; CHECK: mov {{r[a-z0-9]+}}, -1
; CHECK: sub {{r[a-z0-9]+}}, {{r[a-z0-9]+}}
; CHECK-NOT: __bit_not
define i32 @bit_not(i32 %x) {
  %r = xor i32 %x, -1
  ret i32 %r
}

; A variable shift amount keeps using the software libcall.
; CHECK-LABEL: shl_var:
; CHECK: call __bit_shl
define i32 @shl_var(i32 %x, i32 %n) {
  %r = shl i32 %x, %n
  ret i32 %r
}

; A variable-variable AND keeps using the software libcall.
; CHECK-LABEL: and_var:
; CHECK: call __bit_and
define i32 @and_var(i32 %x, i32 %y) {
  %r = and i32 %x, %y
  ret i32 %r
}
