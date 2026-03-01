; Mcasm soft-bit runtime IR.
; Keep this module arithmetic-only: no and/or/xor/not/shl/lshr/ashr instructions.

target triple = "mcasm"

define dso_local i32 @__pow2u(i32 %b) #0 {
entry:
  %neg = icmp slt i32 %b, 0
  br i1 %neg, label %ret0, label %check_hi

check_hi:
  %too_big = icmp sge i32 %b, 32
  br i1 %too_big, label %ret0, label %loop

loop:
  %i = phi i32 [ 0, %check_hi ], [ %inext, %body ]
  %p = phi i32 [ 1, %check_hi ], [ %pnext, %body ]
  %done = icmp slt i32 %i, %b
  br i1 %done, label %body, label %retp

body:
  %pnext = add i32 %p, %p
  %inext = add i32 %i, 1
  br label %loop

retp:
  ret i32 %p

ret0:
  ret i32 0
}

define dso_local i32 @__bit_mod2(i32 %x) #0 {
entry:
  %q = udiv i32 %x, 2
  %twice = add i32 %q, %q
  %even = icmp eq i32 %twice, %x
  br i1 %even, label %ret0, label %ret1

ret0:
  ret i32 0

ret1:
  ret i32 1
}

define dso_local i32 @__bit_not(i32 %a) #0 {
entry:
  %r = call i32 @__bit_xor(i32 %a, i32 -1)
  ret i32 %r
}

define dso_local i32 @__bit_and(i32 %a, i32 %b) #0 {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %inext, %body ]
  %p = phi i32 [ 1, %entry ], [ %pnext, %body ]
  %acc = phi i32 [ 0, %entry ], [ %accnext, %body ]
  %keep = icmp slt i32 %i, 32
  br i1 %keep, label %body, label %exit

body:
  %adiv = udiv i32 %a, %p
  %ai = call i32 @__bit_mod2(i32 %adiv)
  %bdiv = udiv i32 %b, %p
  %bi = call i32 @__bit_mod2(i32 %bdiv)
  %ab = mul i32 %ai, %bi
  %term = mul i32 %ab, %p
  %accnext = add i32 %acc, %term
  %pnext = add i32 %p, %p
  %inext = add i32 %i, 1
  br label %loop

exit:
  ret i32 %acc
}

define dso_local i32 @__bit_or(i32 %a, i32 %b) #0 {
entry:
  %ab = call i32 @__bit_and(i32 %a, i32 %b)
  %sum = add i32 %a, %b
  %r = sub i32 %sum, %ab
  ret i32 %r
}

define dso_local i32 @__bit_xor(i32 %a, i32 %b) #0 {
entry:
  %ab = call i32 @__bit_and(i32 %a, i32 %b)
  %twice = add i32 %ab, %ab
  %sum = add i32 %a, %b
  %r = sub i32 %sum, %twice
  ret i32 %r
}

define dso_local i32 @__bit_shl(i32 %a, i32 %b) #0 {
entry:
  %neg = icmp slt i32 %b, 0
  br i1 %neg, label %ret0, label %check_hi

check_hi:
  %too_big = icmp sge i32 %b, 32
  br i1 %too_big, label %ret0, label %doit

doit:
  %pow = call i32 @__pow2u(i32 %b)
  %r = mul i32 %a, %pow
  ret i32 %r

ret0:
  ret i32 0
}

define dso_local i32 @__bit_shr(i32 %a, i32 %b) #0 {
entry:
  %neg = icmp slt i32 %b, 0
  br i1 %neg, label %ret0, label %check_hi

check_hi:
  %too_big = icmp sge i32 %b, 32
  br i1 %too_big, label %ret0, label %doit

doit:
  %pow = call i32 @__pow2u(i32 %b)
  %r = udiv i32 %a, %pow
  ret i32 %r

ret0:
  ret i32 0
}

define dso_local i32 @__bit_sar(i32 %a, i32 %b) #0 {
entry:
  %sign_entry = call i32 @__bit_shr(i32 %a, i32 31)
  %bzero = icmp eq i32 %b, 0
  br i1 %bzero, label %ret_a, label %check_range

check_range:
  %bneg = icmp slt i32 %b, 0
  br i1 %bneg, label %ret_sat, label %check_hi

check_hi:
  %bhi = icmp sge i32 %b, 32
  br i1 %bhi, label %ret_sat, label %main

ret_a:
  ret i32 %a

ret_sat:
  %sat = sub i32 0, %sign_entry
  ret i32 %sat

main:
  %pow_b = call i32 @__pow2u(i32 %b)
  %logical = udiv i32 %a, %pow_b
  %invb = sub i32 32, %b
  %low_mask = call i32 @__pow2u(i32 %invb)
  %ones_minus = sub i32 -1, %low_mask
  %sign_mask = add i32 %ones_minus, 1
  %fill = mul i32 %sign_entry, %sign_mask
  %r = add i32 %logical, %fill
  ret i32 %r
}

attributes #0 = { noinline nounwind optnone memory(none) mustprogress nofree norecurse nosync willreturn }
