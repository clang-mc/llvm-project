// RUN: %clang_cc1 -triple mcasm -emit-llvm -o - %s | FileCheck %s

// The mcasm __builtin_mcf_ftoa builtin folds a compile-time-constant double to
// its shortest round-trippable decimal string (fixed-point, no scientific
// notation, so Minecraft's SNBT parser accepts it), and degrades to a call to
// the runtime helper __mcf_ftoa for a non-constant argument.

// CHECK: @[[STR1:.*]] = private unnamed_addr constant [2 x i8] c"1\00"
// CHECK: @[[STRPI:.*]] = private unnamed_addr constant [5 x i8] c"3.14\00"
// CHECK: @[[STRHALF:.*]] = private unnamed_addr constant [4 x i8] c"0.5\00"
// CHECK: @[[STRNEG:.*]] = private unnamed_addr constant [3 x i8] c"-2\00"
// CHECK: @[[STRBIG:.*]] = private unnamed_addr constant [7 x i8] c"100000\00"
// CHECK: @[[STRNAN:.*]] = private unnamed_addr constant [4 x i8] c"nan\00"
// CHECK: @[[STRINF:.*]] = private unnamed_addr constant [4 x i8] c"inf\00"
// CHECK: @[[STRNINF:.*]] = private unnamed_addr constant [5 x i8] c"-inf\00"

// CHECK-LABEL: @fold_one
// CHECK: ret ptr @[[STR1]]
const char *fold_one(void) { return __builtin_mcf_ftoa(1.0); }

// An integer literal is converted to double by the prototype and still folds.
// CHECK-LABEL: @fold_int
// CHECK: ret ptr @[[STR1]]
const char *fold_int(void) { return __builtin_mcf_ftoa(1); }

// CHECK-LABEL: @fold_pi
// CHECK: ret ptr @[[STRPI]]
const char *fold_pi(void) { return __builtin_mcf_ftoa(3.14); }

// CHECK-LABEL: @fold_half
// CHECK: ret ptr @[[STRHALF]]
const char *fold_half(void) { return __builtin_mcf_ftoa(0.5); }

// CHECK-LABEL: @fold_neg
// CHECK: ret ptr @[[STRNEG]]
const char *fold_neg(void) { return __builtin_mcf_ftoa(-2.0); }

// CHECK-LABEL: @fold_big
// CHECK: ret ptr @[[STRBIG]]
const char *fold_big(void) { return __builtin_mcf_ftoa(100000.0); }

// CHECK-LABEL: @fold_nan
// CHECK: ret ptr @[[STRNAN]]
const char *fold_nan(void) { return __builtin_mcf_ftoa(__builtin_nan("")); }

// CHECK-LABEL: @fold_inf
// CHECK: ret ptr @[[STRINF]]
const char *fold_inf(void) { return __builtin_mcf_ftoa(__builtin_inf()); }

// CHECK-LABEL: @fold_neg_inf
// CHECK: ret ptr @[[STRNINF]]
const char *fold_neg_inf(void) { return __builtin_mcf_ftoa(-__builtin_inf()); }

// A non-constant argument must degrade to a runtime __mcf_ftoa call, never a
// compile error (so a function mixing constant and variable uses still builds).
// CHECK-LABEL: @degrade_runtime
// CHECK: call ptr @__mcf_ftoa(double
const char *degrade_runtime(double x) { return __builtin_mcf_ftoa(x); }

// CHECK-LABEL: @mixed
// CHECK-DAG: store ptr @[[STR1]], ptr %retval
// CHECK-DAG: call ptr @__mcf_ftoa(double
const char *mixed(double x, int pick) {
  if (pick)
    return __builtin_mcf_ftoa(1.0);
  return __builtin_mcf_ftoa(x);
}
