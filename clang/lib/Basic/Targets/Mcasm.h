//===--- Mcasm.h - Declare Mcasm target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Mcasm TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MCASM_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MCASM_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY McasmTargetInfo : public TargetInfo {
  // Class for Mcasm (32-bit Minecraft assembly)
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  static const char *const GCCRegNames[];

public:
  McasmTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // Mcasm data layout: 32-bit pointers, 32-bit (word) alignment for all types
    // All types must be word-aligned because mcasm uses 4-byte addressing units
    // e-p:32:32-i8:32-i16:32-i32:32-i64:32-f32:32-f64:32-a:0:32-n32
    resetDataLayout("e-p:32:32-i8:32-i16:32-i32:32-i64:32-f32:32-f64:32-a:0:32-n32");

    // Mcasm uses 8 parameter registers (r0-r7)
    RegParmMax = 8;

    // All types are 32-bit aligned in mcasm
    MinGlobalAlign = 32;

    // CRITICAL: mcasm only supports 32-bit memory operations
    // Even though mcasm addresses memory in 4-byte units,
    // CharWidth/CharAlign are managed via data layout string (i8:32)
    // Width and Align are in BITS, not bytes!
    BoolWidth = 8;
    BoolAlign = 32;
    ShortWidth = 16;
    ShortAlign = 32;     // Still word-aligned

    // Pointer characteristics
    PointerWidth = 32;
    PointerAlign = 32;   // 32 bits = 4 bytes alignment
    IntWidth = 32;
    IntAlign = 32;
    LongWidth = 32;
    LongAlign = 32;
    LongLongWidth = 64;  // long long is 64-bit
    LongLongAlign = 32;  // 4-byte alignment (32 bits)

    // Float types (mcasm doesn't support float, but set for completeness)
    FloatWidth = 32;      // 32 bits = 4 bytes
    FloatAlign = 32;      // 32-bit word alignment
    DoubleWidth = 64;     // 64 bits = 8 bytes
    DoubleAlign = 32;     // 32-bit word alignment (not 64-bit!)
    LongDoubleWidth = 64; // 64 bits = 8 bytes
    LongDoubleAlign = 32; // 32-bit word alignment

    // Size types
    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool isValidCPUName(StringRef Name) const override {
    return Name == "generic";
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    Values.emplace_back("generic");
  }

  bool setCPU(const std::string &Name) override {
    return Name == "generic";
  }

  bool hasFeature(StringRef Feature) const override {
    return false;  // Mcasm has no additional features
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override {
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  bool hasBitIntType() const override { return true; }

  /// Mcasm supports dllimport/dllexport attributes
  bool shouldDLLImportComdatSymbols() const override { return true; }
};
} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MCASM_H
