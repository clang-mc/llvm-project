/*
 * Mcasm soft-bit runtime.
 *
 * These helpers intentionally use only +, -, *, /, % so they can be used
 * as the software fallback for i32 bit operations.
 */

typedef unsigned int u32;
typedef int i32;

#define U32_ONES 4294967295u
#define U32_MSBIT 2147483648u

u32 __bit_not(u32 a) { return U32_ONES - a; }

u32 __bit_and(u32 a, u32 b) {
  u32 result = 0u;
  u32 p = 1u;
  int i;
  for (i = 0; i < 32; i++) {
    u32 ai = (a / p) % 2u;
    u32 bi = (b / p) % 2u;
    result = result + ai * bi * p;
    p = p + p;
  }
  return result;
}

u32 __bit_or(u32 a, u32 b) { return a + b - __bit_and(a, b); }

u32 __bit_xor(u32 a, u32 b) {
  u32 ab = __bit_and(a, b);
  return a + b - (ab + ab);
}

u32 __bit_shl(u32 a, u32 b) {
  i32 sb = (i32)b;
  if (sb < 0 || sb >= 32)
    return 0u;
  return a * __builtin_pow2u(b);
}

u32 __bit_shr(u32 a, u32 b) {
  i32 sb = (i32)b;
  if (sb < 0 || sb >= 32)
    return 0u;
  return a / __builtin_pow2u(b);
}

i32 __bit_sar(i32 a, u32 b) {
  i32 sb = (i32)b;
  u32 sign = __bit_shr((u32)a, 31u);
  if (sb == 0)
    return a;
  if (sb < 0 || sb >= 32)
    return (i32)(0u - sign);

  u32 ua = (u32)a;
  u32 logical = ua / __builtin_pow2u(b);
  u32 low_mask = __builtin_pow2u(32u - b);
  u32 sign_mask = (U32_ONES - low_mask) + 1u;
  return (i32)(logical + sign * sign_mask);
}
