//===-- McasmTargetObjectFile.h - Mcasm Object Info -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines custom object file handling for the Mcasm target.
// It customizes symbol names to match mcasm assembly format (_ll_shared: prefix).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCASM_MCASMTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_MCASM_MCASMTARGETOBJECTFILE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include <string>

namespace llvm {

class StringRef;
class GlobalValue;

/// Encode a shared mcasm symbol suffix into the restricted character set used
/// after `_ll_shared:`.
std::string rewriteMcasmSharedName(StringRef Name);
bool shouldAnonymizeMcasmStaticData(const GlobalValue *GV);

// Use ELF as base since mcasm primarily targets ELF-like output
class McasmTargetObjectFile : public TargetLoweringObjectFileELF {
public:
  McasmTargetObjectFile() = default;
  ~McasmTargetObjectFile() override = default;

  /// Override to customize symbol names for mcasm format
  MCSymbol *getTargetSymbol(const GlobalValue *GV,
                            const TargetMachine &TM) const override;

private:
  mutable DenseMap<const GlobalValue *, std::string> AnonymizedStaticSymbols;
};

} // end namespace llvm

#endif
