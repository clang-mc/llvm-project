//===--- Mcasm.cpp - Mcasm toolchain for clang -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Mcasm.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Job.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace llvm::opt;

namespace {

static std::string resolveClangMcPath(const Driver &D, const ToolChain &TC) {
  llvm::SmallString<128> LocalPath(D.Dir);
  llvm::sys::path::append(LocalPath, "clang-mc");
  if (llvm::sys::fs::can_execute(LocalPath))
    return std::string(LocalPath);

#ifdef _WIN32
  llvm::SmallString<128> LocalExePath(D.Dir);
  llvm::sys::path::append(LocalExePath, "clang-mc.exe");
  if (llvm::sys::fs::can_execute(LocalExePath))
    return std::string(LocalExePath);
#endif

  return TC.GetProgramPath("clang-mc");
}

static bool hasBuildDirOverride(const ArgList &Args) {
  for (const Arg *A : Args.filtered(clang::options::OPT_Xclang_mc)) {
    llvm::StringRef V = A->getValue();
    if (V == "--build-dir" || V == "-B" || V.starts_with("--build-dir=") ||
        V.starts_with("-B="))
      return true;
  }
  return false;
}

static std::string getClangMcOutputBase(llvm::StringRef OutputName) {
  if (OutputName.ends_with_insensitive(".zip"))
    return OutputName.drop_back(4).str();
  return OutputName.str();
}

} // namespace

void mcasm::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  std::string ExecPath = resolveClangMcPath(D, TC);
  if (!llvm::sys::fs::can_execute(ExecPath) &&
      !Args.hasArg(clang::options::OPT__HASH_HASH_HASH)) {
    D.Diag(clang::diag::err_drv_no_such_file) << "clang-mc";
    return;
  }

  ArgStringList CmdArgs;
  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  for (Arg *A : Args.filtered(clang::options::OPT_Xclang_mc)) {
    A->claim();
    CmdArgs.push_back(A->getValue());
  }

  if (!hasBuildDirOverride(Args)) {
    std::string TmpDir = D.GetTemporaryDirectory("mcasm");
    CmdArgs.push_back("--build-dir");
    CmdArgs.push_back(Args.MakeArgString(TmpDir));
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Args.MakeArgString(
      getClangMcOutputBase(Output.getFilename())));

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileUTF8(),
      Args.MakeArgString(ExecPath), CmdArgs, Inputs, Output));
}

Mcasm::Mcasm(const Driver &D, const llvm::Triple &Triple,
             const llvm::opt::ArgList &Args)
    : ToolChain(D, Triple, Args) {}

Tool *Mcasm::buildLinker() const { return new tools::mcasm::Linker(*this); }
