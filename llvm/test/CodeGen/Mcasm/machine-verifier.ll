; RUN: llc -mtriple=mcasm -verify-machineinstrs -o %t %s

; This test ensures we do not produce verifier-invalid machine IR for:
; 1) PTRADD over FrameIndex-backed pointers (must not become ADD32ri %stack.N, imm)
; 2) i64 call returns that use rax+t0 physical return registers.

declare i64 @ext_i64_ret(i32)

define i32 @frameindex_ptradd(i32 %idx) {
entry:
  %arr = alloca [16 x i32], align 4
  %p = getelementptr inbounds [16 x i32], ptr %arr, i32 0, i32 %idx
  store i32 7, ptr %p, align 4
  %q = getelementptr inbounds [16 x i32], ptr %arr, i32 0, i32 1
  %v = load i32, ptr %q, align 4
  ret i32 %v
}

define i64 @direct_i64_call(i32 %x) {
entry:
  %v = call i64 @ext_i64_ret(i32 %x)
  ret i64 %v
}

define i64 @indirect_i64_call(ptr %fp, i32 %x) {
entry:
  %v = call i64 %fp(i32 %x)
  ret i64 %v
}

define void @out_char(i32 %c, ptr %dst, i1 %emit) {
entry:
  br i1 %emit, label %do_emit, label %done

do_emit:
  store i32 %c, ptr %dst, align 4
  br label %done

done:
  ret void
}
