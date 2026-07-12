; RUN: llc -mtriple=mcasm -filetype=asm %s -o - | FileCheck %s

; The int_mcasm_str_begin/append intrinsics fold a compile-time-constant string
; operand to O(1) `data modify ... set value` MC commands (task §3.1); a
; non-constant operand degrades to a call to the runtime *_rt fallback (§3.2).

@__mc_state = global i8 0
@s1f = private unnamed_addr constant [3 x i8] c"1f\00"
@sf = private unnamed_addr constant [2 x i8] c"f\00"
@sempty = private unnamed_addr constant [1 x i8] c"\00"
@squote = private unnamed_addr constant [5 x i8] c"a\22b\5C\00" ; a"b\

declare void @llvm.mcasm.str.begin(ptr, ptr)
declare void @llvm.mcasm.str.append(ptr, ptr)

; begin(const) -> one `set value` seeding scratch s1.
; CHECK-LABEL: begin_const:
; CHECK: inline data modify storage std:vm s1 set value {str: "1f", next: ""}
define void @begin_const() {
  call void @llvm.mcasm.str.begin(ptr @s1f, ptr @__mc_state)
  ret void
}

; append(const) -> stage into s1.next then merge once, length-independent.
; CHECK-LABEL: append_const:
; CHECK: inline data modify storage std:vm s1.next set value "f"
; CHECK-NEXT: inline function std:_internal/merge_string with storage std:vm s1
define void @append_const() {
  call void @llvm.mcasm.str.append(ptr @sf, ptr @__mc_state)
  ret void
}

; begin("") -> still one seeding command with an empty str.
; CHECK-LABEL: begin_empty:
; CHECK: inline data modify storage std:vm s1 set value {str: "", next: ""}
define void @begin_empty() {
  call void @llvm.mcasm.str.begin(ptr @sempty, ptr @__mc_state)
  ret void
}

; append("") -> no-op: no `set value`, no merge_string.
; CHECK-LABEL: append_empty:
; CHECK-NOT: set value
; CHECK-NOT: merge_string
define void @append_empty() {
  call void @llvm.mcasm.str.append(ptr @sempty, ptr @__mc_state)
  ret void
}

; SNBT escaping: a"b\  ->  a\"b\\
; CHECK-LABEL: begin_escape:
; CHECK: inline data modify storage std:vm s1 set value {str: "a\"b\\", next: ""}
define void @begin_escape() {
  call void @llvm.mcasm.str.begin(ptr @squote, ptr @__mc_state)
  ret void
}

; A non-constant operand degrades to the runtime fallback call.
; CHECK-LABEL: begin_var:
; CHECK: call __mc_str_begin_rt
; CHECK-NOT: set value
define void @begin_var(ptr %s) {
  call void @llvm.mcasm.str.begin(ptr %s, ptr @__mc_state)
  ret void
}

; CHECK-LABEL: append_var:
; CHECK: call __mc_str_append_rt
define void @append_var(ptr %s) {
  call void @llvm.mcasm.str.append(ptr %s, ptr @__mc_state)
  ret void
}
