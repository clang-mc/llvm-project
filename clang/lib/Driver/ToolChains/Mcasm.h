//===--- Mcasm.h - Mcasm toolchain for clang -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MCASM_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MCASM_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace mcasm {

void constructLLVMLinkCommand(Compilation &C, const Tool &T,
                              const JobAction &JA,
                              const InputInfoList &Inputs,
                              const InputInfo &Output,
                              const llvm::opt::ArgList &Args);

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  explicit Linker(const ToolChain &TC) : Tool("mcasm::Linker", "clang-mc", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &Args,
                    const char *LinkingOutput) const override;
};

} // namespace mcasm
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Mcasm final : public ToolChain {
  bool NativeLLVMSupport;

public:
  Mcasm(const Driver &D, const llvm::Triple &Triple,
        const llvm::opt::ArgList &Args);

  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  bool isCrossCompiling() const override { return true; }
  bool IsMathErrnoDefault() const override { return false; }
  bool HasNativeLLVMSupport() const override { return NativeLLVMSupport; }

  const char *getDefaultLinker() const override { return "clang-mc"; }
  Tool *buildLinker() const override;
};

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif
