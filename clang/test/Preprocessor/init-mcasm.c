/// Check predefinitions for the mcasm target.
/// REQUIRES: mcasm-registered-target

// RUN: %clang_cc1 -E -dM -ffreestanding -triple=mcasm < /dev/null | \
// RUN:   FileCheck -match-full-lines -check-prefix=MCASM %s

// MCASM: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
// MCASM: #define __LITTLE_ENDIAN__ 1
// MCASM-NOT: #define __BIG_ENDIAN__ 1
// MCASM: #define __mcasm 1
// MCASM: #define __mcasm__ 1
