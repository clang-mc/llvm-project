; RUN: llc -mtriple=mcasm -filetype=asm %s -o - | FileCheck %s

declare dllimport void @"foo/bar"()

define dllexport void @"z/qux"() {
entry:
  ret void
}

; CHECK: extern _ll_shared:foo/bar:
; CHECK: export _ll_shared:z/qux:
