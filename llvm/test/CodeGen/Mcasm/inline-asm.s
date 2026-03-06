#include "_ll_std"

// -- Begin function ir_inline_input_only
ir_inline_input_only:                   // @ir_inline_input_only
// %bb.0:                               // %entry
	mov	rax, r0
	inline data modify storage std:vm s0 set value {a: -1}
	inline execute store result storage std:vm s0.a int 1 run scoreboard players get rax vm_regs
	inline function _ll_shared:z-2fir_inline_input_only_0 with storage std:vm s0
	ret
// -- End function

export _ll_shared:z-2fir_inline_input_only_0:
    inline $return $(a)
	ret
