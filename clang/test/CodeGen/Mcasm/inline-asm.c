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

int inline_multiline(int ret) {
  __asm__ volatile ("inline const 1\ninline return %0" : : "r"(ret));
  return ret;
}

int inline_duplicate_same_function(int x) {
  __asm__ volatile ("inline return %0" : : "r"(x));
  __asm__ volatile ("inline return %0" : : "r"(x));
  return x;
}

int inline_duplicate_cross_function(int x) {
  __asm__ volatile ("inline return %0" : : "r"(x));
  return x;
}

int inline_distinct_helper(int x) {
  __asm__ volatile ("inline const 2\ninline return %0" : : "r"(x));
  return x;
}

int inline_literal_braces(int x) {
  __asm__ volatile (
      "inline data modify storage std:vm s1 set value {str: \"\", next: \"\"}\n"
      "inline data modify storage std:vm s1.str set from storage std:vm char2str_map[%0]"
      :
      : "r"(x));
  return x;
}

int inline_direct_args(int x) {
  __asm__ volatile (
      "$$direct_args\n"
      "inline const 3\n"
      "inline return %0"
      :
      : "r"(x));
  return x;
}

// ASM-LABEL: inline_input_only:
// ASM: inline data modify storage std:vm ls0 set value {a: -1}
// ASM: inline execute store result storage std:vm ls0.a int 1 run scoreboard players get
// ASM: inline function _ll_shared:z/inline_input_only_0 with storage std:vm ls0

// ASM-LABEL: inline_reg_output:
// ASM: inline data modify storage std:vm ls0 set value {a: -1}
// ASM: inline function _ll_shared:z/inline_reg_output_0 with storage std:vm ls0

// ASM-LABEL: inline_output_only:
// ASM: mov 1, r0
// ASM-NOT: inline function _ll_shared:z/inline_output_only_0 with storage std:vm ls0

// ASM-LABEL: inline_multiline:
// ASM: inline function _ll_shared:z/inline_multiline_0 with storage std:vm ls0

// ASM-LABEL: inline_duplicate_same_function:
// ASM: inline function _ll_shared:z/inline_input_only_0 with storage std:vm ls0
// ASM: inline function _ll_shared:z/inline_input_only_0 with storage std:vm ls0
// ASM-NOT: inline function _ll_shared:z/inline_duplicate_same_function_0 with storage std:vm ls0

// ASM-LABEL: inline_duplicate_cross_function:
// ASM: inline function _ll_shared:z/inline_input_only_0 with storage std:vm ls0
// ASM-NOT: inline function _ll_shared:z/inline_duplicate_cross_function_0 with storage std:vm ls0

// ASM-LABEL: inline_distinct_helper:
// ASM: inline function _ll_shared:z/inline_distinct_helper_0 with storage std:vm ls0

// ASM-LABEL: inline_literal_braces:
// ASM: inline function _ll_shared:z/inline_literal_braces_0 with storage std:vm ls0

// ASM-LABEL: inline_direct_args:
// ASM-NOT: inline data modify storage std:vm ls0 set value {a: -1}
// ASM: inline execute store result storage std:vm ls0.a int 1 run scoreboard players get
// ASM: inline function _ll_shared:z/inline_direct_args_0 with storage std:vm ls0

// ASM: export _ll_shared:z/inline_input_only_0:
// ASM: inline $return $(a)
// ASM-NOT: export _ll_shared:z/inline_duplicate_same_function_0:
// ASM-NOT: export _ll_shared:z/inline_duplicate_cross_function_0:
// ASM: export _ll_shared:z/inline_reg_output_0:
// ASM: mov $(a),
// ASM-NOT: export _ll_shared:z/inline_output_only_0:
// ASM: export _ll_shared:z/inline_multiline_0:
// ASM: inline const 1
// ASM-NOT: inline $const 1
// ASM: inline $return $(a)
// ASM: export _ll_shared:z/inline_distinct_helper_0:
// ASM: inline const 2
// ASM: inline $return $(a)
// ASM: export _ll_shared:z/inline_literal_braces_0:
// ASM: inline data modify storage std:vm s1 set value {str: "", next: ""}
// ASM: inline $data modify storage std:vm s1.str set from storage std:vm char2str_map[$(a)]
// ASM-NOT: $(str:
// ASM-NOT: next: ""$)
// ASM: export _ll_shared:z/inline_direct_args_0:
// ASM-NOT: $$direct_args
// ASM: inline const 3
// ASM: inline $return $(a)
