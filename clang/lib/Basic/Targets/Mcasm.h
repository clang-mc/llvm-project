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
    // Mcasm preserves C byte semantics in IR/object layout even though the
    // backend can only directly issue 32-bit word memory operations.
    // Sub-word loads/stores are lowered later as read-modify-write sequences.
    //
    // Keep the preprocessor view aligned with the LLVM datalayout: mcasm uses
    // little-endian object layout, so compiler-rt and system headers must see
    // little-endian predefined macros as well.
    BigEndian = false;
    resetDataLayout("e-p:32:32-i8:8:8-i16:16:16-i32:32:32-i64:32-f32:32-f64:32-a:0:32-n32");

    // Mcasm uses 8 parameter registers (r0-r7)
    RegParmMax = 8;

    // mcasm has a single inline-asm syntax, so treat {|} as ordinary
    // characters rather than asm-variant delimiters.
    NoAsmVariants = true;

    // All types are 32-bit aligned in mcasm
    MinGlobalAlign = 32;

    // Mcasm only supports 32-bit memory operations in hardware, but the C ABI
    // still uses standard byte-granular object layout.
    BoolWidth = 8;
    BoolAlign = 8;
    ShortWidth = 16;
    ShortAlign = 16;

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

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override;

  std::string_view getClobbers() const override { return ""; }

  bool hasBitIntType() const override { return true; }

  /// Mcasm supports dllimport/dllexport attributes
  bool shouldDLLImportComdatSymbols() const override { return true; }
};
} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MCASM_H
