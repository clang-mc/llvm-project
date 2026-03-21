#include "_ll_std"

// -- Begin function __pow2u
__pow2u:                                // @__pow2u
// %bb.0:                               // %entry
	mov	r1, 0
	jl	r0, r1, .LBB0_6
	jmp	.LBB0_1
.LBB0_1:                                // %check_hi
	mov	r1, 31
	jg	r0, r1, .LBB0_6
	jmp	.LBB0_2
.LBB0_2:                                // %loop.preheader
	mov	rax, 1
	mov	r1, 0
	jmp	.LBB0_3
.LBB0_3:                                // %loop
                                        // =>This Inner Loop Header: Depth=1
	jge	r1, r0, .LBB0_5
	jmp	.LBB0_4
.LBB0_4:                                // %body
                                        //   in Loop: Header=BB0_3 Depth=1
	add	rax, rax
	add	r1, 1
	jmp	.LBB0_3
.LBB0_5:                                // %retp
	ret
.LBB0_6:                                // %ret0
	mov	rax, 0
	ret
// -- End function
// -- Begin function __bit_mod2
__bit_mod2:                             // @__bit_mod2
// %bb.0:                               // %entry
	mov	r1, 1
	call	__bit_shr
	add	rax, rax
	jne	rax, r0, .LBB1_2
	jmp	.LBB1_1
.LBB1_1:                                // %ret0
	mov	rax, 0
	ret
.LBB1_2:                                // %ret1
	mov	rax, 1
	ret
// -- End function
// -- Begin function __bit_not
__bit_not:                              // @__bit_not
// %bb.0:                               // %entry
	mov	r1, -1
	call	__bit_xor
	ret
// -- End function
// -- Begin function __bit_and
__bit_and:                              // @__bit_and
// %bb.0:                               // %entry
	mov	r2, r1
	mov	r3, r0
	mov	r1, 1
	mov	r4, 0
	mov	r6, 0
	jmp	.LBB3_1
.LBB3_1:                                // %loop
                                        // =>This Inner Loop Header: Depth=1
	mov	r0, 31
	jg	r6, r0, .LBB3_3
	jmp	.LBB3_2
.LBB3_2:                                // %body
                                        //   in Loop: Header=BB3_1 Depth=1
	mov	r0, r3
	call	__udivsi3
	mov	r0, rax
	call	__bit_mod2
	mov	r5, rax
	mov	r0, r2
	call	__udivsi3
	mov	r0, rax
	call	__bit_mod2
	mul	r5, rax
	mul	r5, r1
	add	r4, r5
	add	r1, r1
	add	r6, 1
	jmp	.LBB3_1
.LBB3_3:                                // %exit
	mov	rax, r4
	ret
// -- End function
// -- Begin function __bit_or
__bit_or:                               // @__bit_or
// %bb.0:                               // %entry
	call	__bit_and
	add	r0, r1
	sub	r0, rax
	mov	rax, r0
	ret
// -- End function
// -- Begin function __bit_xor
__bit_xor:                              // @__bit_xor
// %bb.0:                               // %entry
	call	__bit_and
	add	rax, rax
	add	r0, r1
	sub	r0, rax
	mov	rax, r0
	ret
// -- End function
// -- Begin function __bit_shl
__bit_shl:                              // @__bit_shl
// %bb.0:                               // %entry
	mov	r2, 0
	jl	r1, r2, .LBB6_3
	jmp	.LBB6_1
.LBB6_1:                                // %check_hi
	mov	r2, 31
	jg	r1, r2, .LBB6_3
	jmp	.LBB6_2
.LBB6_2:                                // %doit
	mov	r2, r0
	mov	r0, r1
	call	__pow2u
	mul	r2, rax
	mov	rax, r2
	ret
.LBB6_3:                                // %ret0
	mov	rax, 0
	ret
// -- End function
// -- Begin function __bit_shr
__bit_shr:                              // @__bit_shr
// %bb.0:                               // %entry
	mov	r2, 0
	jl	r1, r2, .LBB7_3
	jmp	.LBB7_1
.LBB7_1:                                // %check_hi
	mov	r2, 31
	jg	r1, r2, .LBB7_3
	jmp	.LBB7_2
.LBB7_2:                                // %doit
	mov	r2, r0
	mov	r0, r1
	call	__pow2u
	mov	r0, r2
	mov	r1, rax
	call	__udivsi3
	ret
.LBB7_3:                                // %ret0
	mov	rax, 0
	ret
// -- End function
// -- Begin function __bit_sar
__bit_sar:                              // @__bit_sar
// %bb.0:                               // %entry
	mov	r2, r1
	mov	r3, r0
	mov	r0, r3
	mov	r1, 31
	call	__bit_shr
	jz	r2, .LBB8_3
	jmp	.LBB8_1
.LBB8_1:                                // %check_range
	mov	r0, 0
	jl	r2, r0, .LBB8_4
	jmp	.LBB8_2
.LBB8_2:                                // %check_hi
	mov	r0, 31
	jg	r2, r0, .LBB8_4
	jmp	.LBB8_5
.LBB8_3:                                // %ret_a
	mov	rax, r3
	ret
.LBB8_4:                                // %ret_sat
	mov	r0, 0
	sub	r0, rax
	mov	rax, r0
	ret
.LBB8_5:                                // %main
	mov	r0, r2
	mov	r4, rax
	call	__pow2u
	mov	r0, r3
	mov	r1, rax
	call	__udivsi3
	mov	r1, rax
	mov	r0, 32
	sub	r0, r2
	call	__pow2u
	mov	r0, 0
	sub	r0, rax
	mul	r4, r0
	add	r1, r4
	mov	rax, r1
	ret
// -- End function
