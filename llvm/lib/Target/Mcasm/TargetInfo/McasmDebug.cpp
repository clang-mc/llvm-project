#include "TargetInfo/McasmDebug.h"
#include "llvm/Support/CommandLine.h"
#include <cstdarg>
#include <cstdio>

using namespace llvm;

static cl::opt<bool> McasmDebugLog(
    "mcasm-debug-log", cl::Hidden,
    cl::desc("Enable Mcasm backend debug logs"), cl::init(false));

bool llvm::McasmDebug::isEnabled() { return McasmDebugLog; }

void llvm::McasmDebug::log(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  vfprintf(stderr, Fmt, Args);
  va_end(Args);
  fflush(stderr);
}
