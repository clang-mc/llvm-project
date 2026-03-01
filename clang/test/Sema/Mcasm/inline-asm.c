// RUN: %clang_cc1 -triple mcasm -fsyntax-only -verify %s

void bad_output(int x) {
  int y;
  __asm__("" : "=f"(y) : "0"(x)); // expected-error {{invalid output constraint '=f' in asm}}
}

void ok_clobbers(int *p) {
  int y;
  __asm__ volatile ("mov %1, %0" : "=r"(y) : "m"(*p) : "memory", "cc");
}