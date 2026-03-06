; RUN: not llc -mtriple=mcasm -filetype=asm %s -o - 2>&1 | FileCheck %s

@f = ifunc i32 (), ptr @f_resolver

define ptr @f_resolver() {
entry:
  ret ptr null
}

; CHECK: mcasm does not support ifunc: f

