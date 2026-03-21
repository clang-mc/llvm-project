// RUN: %clang --target=mcasm -S %s -### 2>&1 | FileCheck %s --check-prefix=S
// RUN: %clang --target=mcasm -c %s -### 2>&1 | FileCheck %s --check-prefix=C
// RUN: %clang --target=mcasm %s -### 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: %clang --target=mcasm %s -o demo -### 2>&1 | FileCheck %s --check-prefix=OUT
// RUN: %clang --target=mcasm %s -Xclang-mc --namespace -Xclang-mc ns:test -### 2>&1 | FileCheck %s --check-prefix=XMC --implicit-check-not=unused-command-line-argument
// RUN: %clang --target=mcasm -nostdlib -S %s -### 2>&1 | FileCheck %s --check-prefix=NOSTDLIB

// S: "-cc1"
// S-SAME: "-triple" "mcasm"
// S-SAME: "-S"
// S: "-o" "{{.*\.mcasm}}"
// S-NOT: "clang-mc"

// C: "-cc1"
// C-SAME: "-triple" "mcasm"
// C-SAME: "-S"
// C: "-o" "{{.*\.mcasm}}"
// C-NOT: "clang-mc"

// LINK: "-cc1"
// LINK-SAME: "-triple" "mcasm"
// LINK-SAME: "-S"
// LINK: "-o" "{{.*\.mcasm}}"
// LINK: clang-mc
// LINK-SAME: "{{.*\.mcasm}}"
// LINK-SAME: "--build-dir" "[[BUILDDIR:[^"]+]]"
// LINK-SAME: "-o" "a"
// LINK-NOT: "gcc"
// LINK-NOT: "ld"

// OUT: clang-mc
// OUT: "-o" "demo"

// XMC: clang-mc
// XMC: "--namespace" "ns:test"

// NOSTDLIB: "-fmcasm-no-ll-libc"

int foo(void) { return 1; }
