// RUN: %clang_cc1 -triple mcasm -emit-llvm -o - %s | FileCheck %s

// The mcasm __builtin_mcf_str_begin/append builtins lower unconditionally to
// the int_mcasm_str_begin/append intrinsics (the string operand only becomes a
// compile-time constant after LTO/SCCP, so the fold happens in the backend, not
// here). The second argument is &__mc_state, the coarse MC-state ordering
// shadow. The intrinsics carry argmem read/write side effects (arg0 readonly),
// so the calls are never optimized away despite the void result.

extern char __mc_state;

// CHECK-LABEL: @begin_const
// CHECK: call void @llvm.mcasm.str.begin(ptr {{.*}}, ptr @__mc_state)
void begin_const(void) { __builtin_mcf_str_begin("1f", &__mc_state); }

// CHECK: declare void @llvm.mcasm.str.begin(ptr readonly, ptr) [[ATTR:#[0-9]+]]

// CHECK-LABEL: @append_const
// CHECK: call void @llvm.mcasm.str.append(ptr {{.*}}, ptr @__mc_state)
void append_const(void) { __builtin_mcf_str_append("f", &__mc_state); }

// A non-constant string operand is still lowered to the intrinsic here; the
// backend degrades it to the runtime *_rt fallback.
// CHECK-LABEL: @begin_var
// CHECK: call void @llvm.mcasm.str.begin(ptr %{{.*}}, ptr @__mc_state)
void begin_var(const char *s) { __builtin_mcf_str_begin(s, &__mc_state); }

// CHECK: attributes [[ATTR]] = {{.*}}memory(argmem: readwrite)
