// RUN: %clang --target=mcasm -S -O0 %s -o - | FileCheck %s --check-prefix=ASM

int inline_input_only(int ret) {
  __asm__ volatile ("inline return %0" : : "r"(ret));
  return ret;
}

int inline_reg_output(int x) {
  int y;
  __asm__ volatile ("mov %1, %0" : "=r"(y) : "r"(x) : "memory");
  return y;
}

int inline_output_only(void) {
  int y;
  __asm__ volatile ("mov 1, %0" : "=r"(y));
  return y;
}

// ASM-LABEL: inline_input_only:
// ASM: inline data modify storage std:vm s0 set value {a: -1}
// ASM: inline execute store result storage std:vm s0.a.ptr int 1 run scoreboard players get
// ASM: inline function _ll_shared:z/inline_input_only_0 with storage std:vm s0

// ASM-LABEL: inline_reg_output:
// ASM: inline data modify storage std:vm s0 set value {a: -1}
// ASM: inline function _ll_shared:z/inline_reg_output_0 with storage std:vm s0

// ASM-LABEL: inline_output_only:
// ASM: mov 1, r0
// ASM-NOT: inline function _ll_shared:z/inline_output_only_0 with storage std:vm s0

// ASM: export _ll_shared:z/inline_input_only_0:
// ASM: inline $return $(a)
// ASM: export _ll_shared:z/inline_reg_output_0:
// ASM: mov $(a),
// ASM-NOT: export _ll_shared:z/inline_output_only_0:
