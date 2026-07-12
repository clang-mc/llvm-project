; RUN: llc -mtriple=mcasm -verify-machineinstrs -o - %s | FileCheck %s --check-prefix=WRAP

define i32 @ir_inline_input_only(i32 %x) {
entry:
  call void asm sideeffect "inline return $0", "r"(i32 %x)
  ret i32 %x
}

; WRAP-LABEL: ir_inline_input_only:
; WRAP: inline data modify storage std:vm [[S:ls[0-9]+]] set value {a: -1}
; WRAP: inline execute store result storage std:vm [[S]].a int 1 run scoreboard players get
; WRAP: inline function _ll_shared:z/ir_inline_input_only_0 with storage std:vm [[S]]

; A memory operand declared but never referenced in the template needs no
; argument-passing machinery: no storage init, no helper wrapper.
define void @unref_mem(ptr %p) {
entry:
  call void asm sideeffect "", "=*m,*m"(ptr elementtype(i32) %p, ptr elementtype(i32) %p)
  ret void
}

; WRAP-LABEL: unref_mem:
; WRAP-NOT: inline data modify storage
; WRAP-NOT: inline function _ll_shared:z/unref_mem

; An unreferenced memory operand keeps only its compile-time ordering
; dependency (like a bare "memory" clobber); its address must not be
; materialized. This mirrors `__asm__ volatile("" : "+m"(g))`, which used to
; emit a spurious `mov rN, g`.
@g = global i8 0

define void @unref_mem_global() {
entry:
  call void asm sideeffect "", "=*m,*m"(ptr elementtype(i8) @g, ptr elementtype(i8) @g)
  ret void
}

; WRAP-LABEL: unref_mem_global:
; WRAP-NOT: mov
; WRAP: ret

; Likewise a register input that is never referenced needs no marshalling.
define void @unref_reg(i32 %x) {
entry:
  call void asm sideeffect "", "r"(i32 %x)
  ret void
}

; WRAP-LABEL: unref_reg:
; WRAP-NOT: inline data modify storage
; WRAP-NOT: inline function _ll_shared:z/unref_reg

; An unreferenced register input must not force the value into a register:
; the materializing `mov` (and any spill) is dropped before regalloc.
define void @unref_reg_const() {
entry:
  call void asm sideeffect "", "r"(i32 5)
  ret void
}

; WRAP-LABEL: unref_reg_const:
; WRAP-NOT: mov
; WRAP: ret

; A tied (matching-constraint) operand must never have a preceding group
; stripped: doing so would corrupt the tie link. The pass leaves such an
; INLINEASM untouched, so this must survive -verify-machineinstrs (enabled on
; the RUN line above) and still reference the tied register.
define i32 @tied_unref(i32 %x, i32 %y) {
entry:
  %r = call i32 asm sideeffect "inline use $0", "=r,r,0"(i32 %y, i32 %x)
  ret i32 %r
}

; WRAP-LABEL: tied_unref:
; WRAP: inline use rax

; When an earlier input is unreferenced but a later one is used, the surviving
; `$N` reference is renumbered and only the used value is materialized.
define i32 @renumber(i32 %a, i32 %b) {
entry:
  %r = call i32 asm sideeffect "inline yield $2", "=r,r,r"(i32 %a, i32 %b)
  ret i32 %r
}

; WRAP-LABEL: renumber:
; WRAP: inline execute store result storage std:vm [[R:ls[0-9]+]].a int 1 run scoreboard players get r1 vm_regs
; WRAP-NOT: scoreboard players get r0 vm_regs
; WRAP: inline function _ll_shared:z/renumber_0 with storage std:vm [[R]]

; Helper bodies are emitted together at end of file, in definition order.
; WRAP: export _ll_shared:z/ir_inline_input_only_0:
; WRAP: inline $return $(a)
; The surviving $2 reference is renumbered to $1 -> field a in the helper.
; WRAP: export _ll_shared:z/renumber_0:
; WRAP: inline $yield $(a)
