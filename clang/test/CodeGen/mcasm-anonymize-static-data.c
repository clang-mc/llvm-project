// RUN: %clang_cc1 -triple mcasm -emit-llvm -o - %s -fmcasm-anonymize-static-data | FileCheck %s

int and4[1][3] = {{1, 2, 3}};

// CHECK: !{i32 4, !"mcasm-anonymize-static-data", i32 1}
