// RUN: not %clang --target=mcasm -S -O0 %s -DUNSUPPORTED -o - 2>&1 | FileCheck %s --check-prefix=UNSUPPORTED
// RUN: not %clang --target=mcasm -S -O0 %s -DLATE -o - 2>&1 | FileCheck %s --check-prefix=LATE

#ifdef UNSUPPORTED
void unsupported_option(int x) {
  __asm__ volatile (
      "$$unknown\n"
      "inline return %0"
      :
      : "r"(x));
}
#endif

#ifdef LATE
void late_option(int x) {
  __asm__ volatile (
      "inline const 1\n"
      "$$direct_args\n"
      "inline return %0"
      :
      : "r"(x));
}
#endif

// UNSUPPORTED: mcasm inline asm wrapper: unsupported mcasm inline asm option: $$unknown
// LATE: mcasm inline asm wrapper: mcasm inline asm option must appear before asm body: $$direct_args
