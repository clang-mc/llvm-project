; RUN: llc -mtriple=mcasm -o - %s | FileCheck %s

@wide = global i64 72057594037927936, align 4
@neg = global i64 -1, align 4
@wide_zeros = internal global [2 x i64] zeroinitializer, align 4
@wide_array = global [2 x i64] [i64 1, i64 72057594037927936], align 4
@mixed = global { i8, i64, i16 } { i8 5, i64 72057594037927936, i16 9 }, align 4
@one = global double 1.0, align 4

define i64 @load_wide() {
entry:
  %v = load i64, ptr @wide, align 4
  ret i64 %v
}

define i64 @load_second_wide() {
entry:
  %p = getelementptr inbounds [2 x i64], ptr @wide_array, i32 0, i32 1
  %v = load i64, ptr %p, align 4
  ret i64 %v
}

define void @store_second_wide(i64 %v) {
entry:
  %p = getelementptr inbounds [2 x i64], ptr @wide_array, i32 0, i32 1
  store i64 %v, ptr %p, align 4
  ret void
}

define i64 @load_mixed_wide() {
entry:
  %p = getelementptr inbounds { i8, i64, i16 }, ptr @mixed, i32 0, i32 1
  %v = load i64, ptr %p, align 4
  ret i64 %v
}

; CHECK-LABEL: load_wide:
; CHECK: mov r0, wide
; CHECK: mov rax, [r0]
; CHECK: add r0, 4
; CHECK: mov t0, [r0]

; CHECK-LABEL: load_second_wide:
; CHECK: mov
; CHECK: add {{.*}}, 8
; CHECK: mov rax, [{{.*}}]
; CHECK: add {{.*}}, 12
; CHECK: mov t0, [{{.*}}]

; CHECK-LABEL: store_second_wide:
; CHECK: mov
; CHECK: add {{.*}}, 8
; CHECK: add {{.*}}, 12
; CHECK: mov [{{.*}}], r1
; CHECK: mov [{{.*}}], r0

; CHECK-LABEL: load_mixed_wide:
; CHECK: mov
; CHECK: add {{.*}}, 4
; CHECK: mov rax, [{{.*}}]
; CHECK: add {{.*}}, 8
; CHECK: mov t0, [{{.*}}]

; CHECK: static wide [0, 16777216]
; CHECK: static neg [-1, -1]
; CHECK: static wide_zeros [0, 0, 0, 0]
; CHECK: static wide_array [1, 0, 0, 16777216]
; CHECK: static mixed [5, 0, 16777216, 9]
; CHECK: static one [0, 1072693248]
