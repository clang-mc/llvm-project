; RUN: llc -mtriple=mcasm -verify-machineinstrs -o %t %s

define i32 @fcmp_oeq(double %a, double %b) {
  %c = fcmp oeq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_olt(double %a, double %b) {
  %c = fcmp olt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ole(double %a, double %b) {
  %c = fcmp ole double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ogt(double %a, double %b) {
  %c = fcmp ogt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_oge(double %a, double %b) {
  %c = fcmp oge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_une(double %a, double %b) {
  %c = fcmp une double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ueq(double %a, double %b) {
  %c = fcmp ueq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ult(double %a, double %b) {
  %c = fcmp ult double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ule(double %a, double %b) {
  %c = fcmp ule double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ugt(double %a, double %b) {
  %c = fcmp ugt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uge(double %a, double %b) {
  %c = fcmp uge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uno(double %a, double %b) {
  %c = fcmp uno double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ord(double %a, double %b) {
  %c = fcmp ord double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}
