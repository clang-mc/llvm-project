// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: %clang --target=mcasm -O2 -flto -S %t/main.c %t/helper.c -o %t/out.mcasm
// RUN: FileCheck %s --input-file=%t/out.mcasm

// CHECK: main:
// CHECK: helper:
// CHECK-NOT: dead_main:
// CHECK-NOT: dead_helper:

//--- main.c
static int dead_main(void) { return 7; }
extern int helper(int);
int main(void) { return helper(41); }

//--- helper.c
static int dead_helper(void) { return 9; }
int helper(int x) { return x + 1; }
