; RUN: llc -mtriple=mcasm -filetype=asm %s -o - | FileCheck %s

declare dllimport void @"\E5\A4\96\E9\83\A8"()

define dllexport void @"A\E5\87\BD\E6\95\B09"() {
entry:
  ret void
}

; CHECK: extern _ll_shared:-e5-a4-96-e9-83-a8:
; CHECK: api _ll_shared:-41-e5-87-bd-e6-95-b09:
