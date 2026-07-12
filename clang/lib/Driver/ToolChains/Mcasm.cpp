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
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
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

static bool canExecuteTool(const std::string &Path) {
  return llvm::sys::fs::can_execute(Path);
}

static const char *addOptLevelArg(const Driver &D, const ArgList &Args,
                                  ArgStringList &CmdArgs) {
  unsigned Level =
      clang::getOptimizationLevel(Args, clang::InputKind(), D.getDiags());
  std::string OptFlag = "-O" + std::to_string(Level);
  const char *Rendered = Args.MakeArgString(OptFlag);
  CmdArgs.push_back(Rendered);
  return Rendered;
}

static unsigned getMcasmOptLevel(const Driver &D, const ArgList &Args) {
  return clang::getOptimizationLevel(Args, clang::InputKind(), D.getDiags());
}

// Resolve the whole-program-LTO stdlib bitcode directory relative to clang:
// <dir of clang.exe>/../lib/mcasm/. Collect every *.bc there (glob, not hard
// coded names, so newly shipped libraries are picked up automatically) into
// StdlibBC, sorted for a deterministic link order. Returns the resolved
// directory (for diagnostics).
static std::string
collectMcasmStdlibBitcode(const Driver &D,
                          llvm::SmallVectorImpl<std::string> &StdlibBC) {
  llvm::SmallString<128> LibDir(D.Dir);
  llvm::sys::path::append(LibDir, "..", "lib", "mcasm");
  llvm::sys::path::remove_dots(LibDir, /*remove_dot_dot=*/true);

  std::error_code EC;
  for (llvm::sys::fs::directory_iterator I(LibDir, EC), End;
       I != End && !EC; I.increment(EC)) {
    llvm::StringRef Path = I->path();
    if (Path.ends_with_insensitive(".bc"))
      StdlibBC.push_back(Path.str());
  }
  llvm::sort(StdlibBC);
  return std::string(LibDir);
}

} // namespace

void mcasm::constructLLVMLinkCommand(
    Compilation &C, const Tool &T, const JobAction &JA,
    const InputInfoList &Inputs,
    llvm::ArrayRef<std::string> ExtraBitcodeInputs, const InputInfo &Output,
    const llvm::opt::ArgList &Args) {
  ArgStringList LinkerInputs;
  for (const InputInfo &Input : Inputs)
    LinkerInputs.push_back(Input.getFilename());
  // Append the stdlib bitcode (libc/libmc) so the inliner / IPSCCP / DCE can
  // cross the stdlib boundary in the merged module.
  for (const std::string &Extra : ExtraBitcodeInputs)
    LinkerInputs.push_back(Args.MakeArgString(Extra));
  tools::constructLLVMLinkCommand(C, T, JA, Inputs, LinkerInputs, Output, Args);
}

void mcasm::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  auto AddClangMcCommand = [&](const InputInfoList &McInputs,
                               const InputInfo &McOutput) {
    std::string ExecPath = resolveClangMcPath(D, TC);
    if (!canExecuteTool(ExecPath) &&
        !Args.hasArg(clang::options::OPT__HASH_HASH_HASH)) {
      D.Diag(clang::diag::err_drv_no_such_file) << "clang-mc";
      return false;
    }

    ArgStringList CmdArgs;
    AddLinkerInputs(TC, McInputs, Args, CmdArgs, JA);

    // Forward the optimization level and debug flags to clang-mc, mapping the
    // rich set of clang -O flags onto clang-mc's -O0/-O1/-O2/-g. Mapping:
    //   -O0                     -> -O0
    //   -O/-O1/-Os/-Oz          -> -O1
    //   -O2/-O3/-O4/-Ofast      -> -O2
    //   -Og                     -> -O1 -g
    //   -g                      -> -g
    bool WantDebug = Args.hasArg(clang::options::OPT_g_Flag);
    if (Arg *A = Args.getLastArg(
            clang::options::OPT_O0, clang::options::OPT_O,
            clang::options::OPT_O4, clang::options::OPT_Ofast)) {
      const Option &Opt = A->getOption();
      if (Opt.matches(clang::options::OPT_O0)) {
        CmdArgs.push_back("-O0");
      } else if (Opt.matches(clang::options::OPT_O4) ||
                 Opt.matches(clang::options::OPT_Ofast)) {
        CmdArgs.push_back("-O2");
      } else {
        // Joined -O<value>: 1/s/z -> -O1, 2/3 -> -O2, g -> -O1 -g, 0 -> -O0.
        llvm::StringRef Level = A->getValue();
        if (Level == "0") {
          CmdArgs.push_back("-O0");
        } else if (Level == "1" || Level == "s" || Level == "z") {
          CmdArgs.push_back("-O1");
        } else if (Level == "2" || Level == "3") {
          CmdArgs.push_back("-O2");
        } else if (Level == "g") {
          CmdArgs.push_back("-O1");
          WantDebug = true;
        }
      }
    }
    if (WantDebug)
      CmdArgs.push_back("-g");

    if (Arg *A = Args.getLastArg(clang::options::OPT_enable_obf)) {
      A->claim();
      CmdArgs.push_back("--enable-obf");
    }

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
    CmdArgs.push_back(
        Args.MakeArgString(getClangMcOutputBase(McOutput.getFilename())));

    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::AtFileUTF8(),
        Args.MakeArgString(ExecPath), CmdArgs, McInputs, McOutput));
    return true;
  };

  bool InputsAreLLVMIR =
      llvm::all_of(Inputs, [](const InputInfo &II) { return types::isLLVMIR(II.getType()); });
  if (!InputsAreLLVMIR) {
    AddClangMcCommand(Inputs, Output);
    return;
  }

  if (D.getLTOMode() == LTOK_Thin) {
    D.Diag(clang::diag::err_drv_unsupported_opt_for_target)
        << Args.getLastArg(options::OPT_flto, options::OPT_flto_EQ)
               ->getAsString(Args)
        << TC.getTripleString();
    return;
  }

  std::string LlvmLinkPath = TC.GetProgramPath("llvm-link");
  std::string OptPath = TC.GetProgramPath("opt");
  if ((!canExecuteTool(LlvmLinkPath) || !canExecuteTool(OptPath)) &&
      !Args.hasArg(clang::options::OPT__HASH_HASH_HASH)) {
    D.Diag(clang::diag::err_drv_no_linker_llvm_support)
        << TC.getTripleString();
    return;
  }

  // Whole-program LTO: pull in the stdlib bitcode shipped next to clang so the
  // inliner / IPSCCP / DCE can cross the stdlib boundary. Without these, -flto
  // is a no-op (the stdlib is only stitched in as a precompiled bundle at the
  // clang-mc assembly stage, which LLVM never sees).
  //
  // -nostdlib requests a freestanding link: whole-program LTO over just the
  // user TUs, with no stdlib bitcode (and hence no bundle-include suppression).
  const bool LinkStdlib = !Args.hasArg(clang::options::OPT_nostdlib);
  llvm::SmallVector<std::string, 4> StdlibBC;
  if (LinkStdlib) {
    std::string StdlibDir = collectMcasmStdlibBitcode(D, StdlibBC);
    if (StdlibBC.empty() &&
        !Args.hasArg(clang::options::OPT__HASH_HASH_HASH)) {
      D.Diag(clang::diag::err_drv_mcasm_lto_no_stdlib_bitcode) << StdlibDir;
      return;
    }
  }

  const char *LinkedBCPath = D.CreateTempFile(C, "mcasm-linked", "bc");
  const char *OptimizedBCPath = D.CreateTempFile(C, "mcasm-opt", "bc");
  const bool EmitAsmOnly = JA.getType() == types::TY_McAsm;
  const char *FinalAsmPath = EmitAsmOnly
                                 ? Output.getFilename()
                                 : D.CreateTempFile(C, "mcasm-final", "mcasm");

  InputInfo LinkedBC(types::TY_LLVM_BC, LinkedBCPath, Output.getBaseInput());
  InputInfo OptimizedBC(types::TY_LLVM_BC, OptimizedBCPath,
                        Output.getBaseInput());
  InputInfo FinalAsm(types::TY_McAsm, FinalAsmPath, Output.getBaseInput());

  constructLLVMLinkCommand(C, *this, JA, Inputs, StdlibBC, LinkedBC, Args);

  {
    // internalize: strip everything not reachable from an export root, but keep
    //   - dllexported user symbols (internalize preserves these implicitly),
    //   - main, the program entry (the backend wires it to _ll_crt), and
    //   - the runtime libcalls the backend synthesizes without an IR caller
    //     (llvm.trap -> abort; llvm.mem* -> memcpy/memmove/memset; struct
    //     compares -> memcmp; llvm.mcasm.str.* with a non-constant operand ->
    //     __mc_str_begin_rt/__mc_str_append_rt). Their definitions live in
    //     libc.opt.bc, so keeping them public leaves them in the module for the
    //     backend's synthesized calls to resolve. External (declaration-only)
    //     libcalls resolved via the _ll_std bundle are preserved automatically.
    // default<O>: whole-program optimization across the merged module.
    // globaldce: drop the now-unreachable stdlib bodies.
    unsigned Level = getMcasmOptLevel(D, Args);
    ArgStringList OptArgs{
        LinkedBC.getFilename(),
        Args.MakeArgString(Twine("-passes=internalize,default<O") +
                           Twine(Level) + ">,globaldce"),
        "-internalize-public-api-list=main,abort,memcpy,memmove,memset,memcmp,"
        "__mc_str_begin_rt,__mc_str_append_rt",
        "-o", OptimizedBC.getFilename()};
    for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
      OptArgs.push_back(Args.MakeArgString(Twine("-") + A->getValue()));
      A->claim();
    }
    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::None(),
        Args.MakeArgString(OptPath), OptArgs, LinkedBC, OptimizedBC));
  }

  {
    ArgStringList BackendArgs{
        "-cc1", "-triple", Args.MakeArgString(TC.getTripleString()), "-S",
        "-x",    "ir",     OptimizedBC.getFilename(), "-o",
        FinalAsm.getFilename()};
    addOptLevelArg(D, Args, BackendArgs);
    // The library bodies are now in the module; suppress the bundle includes so
    // we don't redefine every library label (task §5.1). Only relevant when we
    // actually linked the stdlib bitcode; a freestanding (-nostdlib) LTO link
    // leaves include behavior to the frontend flags.
    if (LinkStdlib) {
      BackendArgs.push_back("-mllvm");
      BackendArgs.push_back("-mcasm-no-stdlib-include");
    }
    for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
      BackendArgs.push_back("-mllvm");
      BackendArgs.push_back(A->getValue());
      A->claim();
    }

    const char *Exec = D.getClangProgramPath();
    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::None(), Exec, BackendArgs, OptimizedBC,
        FinalAsm, D.getPrependArg()));
  }

  if (EmitAsmOnly)
    return;

  InputInfoList McInputs;
  McInputs.push_back(FinalAsm);
  AddClangMcCommand(McInputs, Output);
}

Mcasm::Mcasm(const Driver &D, const llvm::Triple &Triple,
             const llvm::opt::ArgList &Args)
    : ToolChain(D, Triple, Args), NativeLLVMSupport(D.isUsingLTO()) {}

Tool *Mcasm::buildLinker() const { return new tools::mcasm::Linker(*this); }
