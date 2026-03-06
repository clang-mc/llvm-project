; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s --check-prefix=WRAP

define i32 @ir_inline_input_only(i32 %x) {
entry:
  call void asm sideeffect "inline return $0", "r"(i32 %x)
  ret i32 %x
}

; WRAP-LABEL: ir_inline_input_only:
; WRAP: inline data modify storage std:vm s0 set value {a: -1}
; WRAP: inline execute store result storage std:vm s0.a int 1 run scoreboard players get
; WRAP: inline function _ll_shared:z/ir_inline_input_only_0 with storage std:vm s0
; WRAP: export _ll_shared:z/ir_inline_input_only_0:
; WRAP: inline $return $(a)
