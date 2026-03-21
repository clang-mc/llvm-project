//===-- McasmTargetObjectFile.cpp - Mcasm Object Files --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements custom object file handling for the Mcasm target.
// It provides symbol name customization to match mcasm assembly format.
//
//===----------------------------------------------------------------------===//

#include "McasmTargetObjectFile.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include <array>

using namespace llvm;

static cl::opt<bool> McasmAnonymizeStaticData(
    "mcasm-anonymize-static-data", cl::Hidden, cl::init(false),
    cl::desc("Rewrite mcasm static-data symbol names to randomized identifiers"));

std::string llvm::rewriteMcasmSharedName(StringRef Name) {
  static constexpr char HexDigits[] = "0123456789abcdef";

  std::string Encoded;
  Encoded.reserve(Name.size() * 3);
  for (char C : Name) {
    unsigned char Byte = static_cast<unsigned char>(C);
    if ((Byte >= 'a' && Byte <= 'z') || (Byte >= '0' && Byte <= '9') ||
        Byte == '_' || Byte == '/') {
      Encoded.push_back(static_cast<char>(Byte));
      continue;
    }
    if (Byte == '-') {
      Encoded += "--";
      continue;
    }
    Encoded.push_back('-');
    Encoded.push_back(HexDigits[Byte >> 4]);
    Encoded.push_back(HexDigits[Byte & 0x0f]);
  }
  return Encoded;
}

bool llvm::shouldAnonymizeMcasmStaticData(const GlobalValue *GV) {
  if (isa<Function>(GV))
    return false;
  const Module *M = GV->getParent();
  if (M) {
    if (auto *MD =
            cast_or_null<ConstantAsMetadata>(M->getModuleFlag("mcasm-anonymize-static-data"))) {
      if (auto *Flag = dyn_cast<ConstantInt>(MD->getValue()))
        return Flag->getZExtValue() != 0;
    }
  }
  return McasmAnonymizeStaticData;
}

static std::string sanitizeMcasmDataSymbolName(StringRef RawName) {
  SmallString<128> NameStr;
  if (!RawName.empty() && RawName[0] == '.')
    NameStr = RawName.substr(1);
  else
    NameStr = RawName;

  for (char &C : NameStr) {
    if (C == '.')
      C = '_';
  }
  return std::string(NameStr);
}

static std::string createHiddenStaticSymbolName() {
  static constexpr char HexDigits[] = "0123456789abcdef";
  std::array<uint8_t, 16> UUIDBytes{};
  if (std::error_code EC = getRandomBytes(UUIDBytes.data(), UUIDBytes.size()))
    report_fatal_error(Twine("unable to generate mcasm static-data UUID: ") +
                       EC.message());

  SmallString<48> Name("mcasm_static_");
  for (uint8_t Byte : UUIDBytes) {
    Name.push_back(HexDigits[Byte >> 4]);
    Name.push_back(HexDigits[Byte & 0x0f]);
  }
  return std::string(Name);
}

MCSymbol *McasmTargetObjectFile::getTargetSymbol(const GlobalValue *GV,
                                                  const TargetMachine &TM) const {
  SmallString<128> NameStr;

  // Check if this is a function with DLL storage class
  if (const auto *F = dyn_cast<Function>(GV)) {
    // For functions with dllexport or dllimport, add _ll_shared: prefix
    if (GV->hasDLLExportStorageClass() || GV->hasDLLImportStorageClass()) {
      NameStr = "_ll_shared:";
      NameStr += rewriteMcasmSharedName(F->getName());
      return getContext().getOrCreateSymbol(NameStr);
    }
    // For regular functions without DLL storage class, use plain name
    return getContext().getOrCreateSymbol(F->getName());
  }

  if (shouldAnonymizeMcasmStaticData(GV)) {
    auto [It, Inserted] =
        AnonymizedStaticSymbols.try_emplace(GV, std::string());
    if (Inserted)
      It->second = createHiddenStaticSymbolName();
    return getContext().getOrCreateSymbol(It->second);
  }

  NameStr = sanitizeMcasmDataSymbolName(GV->getName());
  if (NameStr != GV->getName())
    return getContext().getOrCreateSymbol(NameStr);
  return nullptr;
}
