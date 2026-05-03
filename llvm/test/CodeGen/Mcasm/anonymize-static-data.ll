; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s --check-prefix=PLAIN
; RUN: llc -mtriple=mcasm -mcasm-anonymize-static-data -o - %s | FileCheck %s --check-prefix=HIDE

@and4 = dso_local global [2 x [2 x [2 x i32]]] [[2 x [2 x i32]] [[2 x i32] [i32 1, i32 2], [2 x i32] [i32 3, i32 4]], [2 x [2 x i32]] [[2 x i32] [i32 5, i32 6], [2 x i32] [i32 7, i32 8]]], align 4

define i32 @main() {
entry:
  %v = load i32, ptr getelementptr inbounds ([2 x [2 x [2 x i32]]], ptr @and4, i32 0, i32 1, i32 1, i32 1), align 4
  ret i32 %v
}

; PLAIN-LABEL: main:
; PLAIN: mov{{.*}} and4
; PLAIN: static and4 [1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0, 8, 0, 0, 0]

; HIDE-LABEL: main:
; HIDE: mov{{.*}} [[HIDDEN:mcasm_static_[0-9a-f]+]]
; HIDE: static [[HIDDEN]] [1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0, 8, 0, 0, 0]
; HIDE-NOT: static and4 [
