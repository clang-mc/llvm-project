// Freestanding whole-program LTO over just the user TUs: -nostdlib skips the
// stdlib bitcode link (there is none shipped next to the test-tree clang), so
// this exercises the cross-TU merge + internalize + DCE without requiring the
// clang-mc stdlib. The entry `main` and the dllexported `helper` survive as
// out-of-line labels; the unreferenced static `dead_*` functions (in both TUs)
// are internalized across the merged module and DCE'd.
// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: %clang --target=mcasm -O2 -flto -nostdlib -S %t/main.c %t/helper.c -o %t/out.mcasm
// RUN: FileCheck %s --input-file=%t/out.mcasm

// CHECK-DAG: main:
// CHECK-DAG: helper:
// CHECK-NOT: dead_main:
// CHECK-NOT: dead_helper:

//--- main.c
static int dead_main(void) { return 7; }
extern int helper(int);
int main(void) { return helper(41); }

//--- helper.c
static int dead_helper(void) { return 9; }
__declspec(dllexport) int helper(int x) { return x + 1; }
