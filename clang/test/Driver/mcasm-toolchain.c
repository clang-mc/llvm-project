// RUN: %clang --target=mcasm -S %s -### 2>&1 | FileCheck %s --check-prefix=S
// RUN: %clang --target=mcasm -c %s -### 2>&1 | FileCheck %s --check-prefix=C
// RUN: %clang --target=mcasm %s -### 2>&1 | FileCheck %s --check-prefix=LINK
// RUN: %clang --target=mcasm %s -o demo -### 2>&1 | FileCheck %s --check-prefix=OUT
// RUN: %clang --target=mcasm %s -Xclang-mc --namespace -Xclang-mc ns:test -### 2>&1 | FileCheck %s --check-prefix=XMC --implicit-check-not=unused-command-line-argument
// RUN: %clang --target=mcasm -nostdlib -S %s -### 2>&1 | FileCheck %s --check-prefix=NOSTDLIB
// RUN: %clang --target=mcasm -flto %s -### 2>&1 | FileCheck %s --check-prefix=LTO
// RUN: %clang --target=mcasm -flto %s -o demo -### 2>&1 | FileCheck %s --check-prefix=LTO-OUT
// RUN: %clang --target=mcasm -flto %s -Xclang-mc --namespace -Xclang-mc ns:test -### 2>&1 | FileCheck %s --check-prefix=LTO-XMC --implicit-check-not=unused-command-line-argument
// RUN: %clang --target=mcasm -flto -S %s %s -### 2>&1 | FileCheck %s --check-prefix=LTO-S
// RUN: %clang --target=mcasm -flto -nostdlib %s -### 2>&1 | FileCheck %s --check-prefix=LTO-NOSTDLIB
// RUN: not %clang --target=mcasm -flto=thin %s 2>&1 | FileCheck %s --check-prefix=THIN-ERR

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

// LTO: "-cc1"
// LTO-SAME: "-triple" "mcasm"
// LTO-SAME: "-emit-llvm-bc"
// LTO-SAME: "-flto=full"
// LTO: "-o" "{{.*\.bc}}"
// LTO: llvm-link{{(\.exe)?}}"
// LTO-SAME: "-o" "{{.*mcasm-linked.*\.bc}}"
// LTO: opt{{(\.exe)?}}" "{{.*mcasm-linked.*\.bc}}" "-o" "{{.*mcasm-opt.*\.bc}}"
// LTO: "-cc1"
// LTO-SAME: "-triple" "mcasm"
// LTO-SAME: "-S"
// LTO-SAME: "-x" "ir"
// LTO-SAME: "{{.*mcasm-opt.*\.bc}}"
// LTO-SAME: "-o" "{{.*mcasm-final.*\.mcasm}}"
// LTO: clang-mc
// LTO-SAME: "{{.*mcasm-final.*\.mcasm}}"
// LTO-NOT: "ld"
// LTO-NOT: "gcc"

// LTO-OUT: clang-mc
// LTO-OUT: "-o" "demo"

// LTO-XMC: clang-mc
// LTO-XMC: "--namespace" "ns:test"
// LTO-XMC-NOT: "llvm-link" {{.*}} "--namespace"
// LTO-XMC-NOT: "opt" {{.*}} "--namespace"
// LTO-XMC-NOT: "-cc1" {{.*}} "--namespace"

// LTO-S: llvm-link{{(\.exe)?}}"
// LTO-S: opt{{(\.exe)?}}"
// LTO-S: "-cc1"
// LTO-S-SAME: "-triple" "mcasm"
// LTO-S-SAME: "-S"
// LTO-S-SAME: "-x" "ir"
// LTO-S-SAME: "-o" "{{.*\.mcasm}}"
// LTO-S-NOT: clang-mc

// LTO-NOSTDLIB: "-fmcasm-no-ll-libc"

// THIN-ERR: error: unsupported option '{{.*}}-flto=thin' for target 'mcasm'

int foo(void) { return 1; }
