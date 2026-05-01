; RUN: llc -mtriple=mcasm -O0 -o - %s | FileCheck %s
; RUN: llc -mtriple=mcasm -O0 -verify-machineinstrs -o - %s > NUL

%struct.Pair = type { i32, i32 }

define void @touch(ptr noundef %p) #0 {
  %pb = getelementptr inbounds nuw %struct.Pair, ptr %p, i32 0, i32 1
  %old = load i32, ptr %pb, align 4
  %new = add nsw i32 %old, 1
  store i32 %new, ptr %pb, align 4
  ret void
}

define i32 @main() #0 {
  %ret = alloca i32, align 4
  %guard = alloca i32, align 4
  %pair = alloca %struct.Pair, align 4
  store i32 0, ptr %ret, align 4
  store i32 77, ptr %guard, align 4
  %pair.a = getelementptr inbounds nuw %struct.Pair, ptr %pair, i32 0, i32 0
  store i32 11, ptr %pair.a, align 4
  %pair.b = getelementptr inbounds nuw %struct.Pair, ptr %pair, i32 0, i32 1
  store i32 0, ptr %pair.b, align 4
  call void @touch(ptr noundef %pair)
  %b = load i32, ptr %pair.b, align 4
  %is_one = icmp eq i32 %b, 1
  br i1 %is_one, label %check_guard, label %fail

check_guard:
  %g = load i32, ptr %guard, align 4
  %guard_ok = icmp eq i32 %g, 77
  br i1 %guard_ok, label %ok, label %fail

ok:
  ret i32 0

fail:
  %b2 = load i32, ptr %pair.b, align 4
  %err = add nsw i32 %b2, 1000
  ret i32 %err
}

attributes #0 = { noinline nounwind optnone }

; CHECK-LABEL: main:
; CHECK:      add r0, 4
; CHECK-NEXT: call touch
; CHECK:      ret
