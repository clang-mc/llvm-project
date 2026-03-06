// RUN: %clang_builtins %s %librt -o %t && %run %t
// REQUIRES: librt_has_subdi3

#include "int_lib.h"
#include <stdio.h>

// Returns: a - b

COMPILER_RT_ABI di_int __subdi3(di_int a, di_int b);

int test__subdi3(di_int a, di_int b, di_int expected) {
  di_int x = __subdi3(a, b);
  if (x != expected)
    printf("error in __subdi3: 0x%llX - 0x%llX = 0x%llX, expected 0x%llX\n", a,
           b, x, expected);
  return x != expected;
}

char assumption_1[sizeof(di_int) == 2 * sizeof(si_int)] = {0};

int main() {
  if (test__subdi3(0, 0, 0))
    return 1;
  if (test__subdi3(5, 3, 2))
    return 1;
  if (test__subdi3(3, 5, -2))
    return 1;
  if (test__subdi3(0, 1, 0xFFFFFFFFFFFFFFFFLL))
    return 1;
  if (test__subdi3(0x8000000000000000LL, 1, 0x7FFFFFFFFFFFFFFFLL))
    return 1;
  if (test__subdi3(0x7FFFFFFFFFFFFFFFLL, -1, 0x8000000000000000LL))
    return 1;
  if (test__subdi3(0x0000000100000000LL, 1, 0x00000000FFFFFFFFLL))
    return 1;
  if (test__subdi3(0x0000000000000000LL, 0xFFFFFFFFFFFFFFFFLL,
                   0x0000000000000001LL))
    return 1;
  if (test__subdi3(0xFFFFFFFF00000000LL, 0x00000000FFFFFFFFLL,
                   0xFFFFFFFE00000001LL))
    return 1;
  if (test__subdi3(0x00000000FFFFFFFFLL, 0xFFFFFFFF00000000LL,
                   0x00000001FFFFFFFFLL))
    return 1;

  return 0;
}
