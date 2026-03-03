#ifndef LLVM_LIB_TARGET_Mcasm_TARGETINFO_MCASMDEBUG_H
#define LLVM_LIB_TARGET_Mcasm_TARGETINFO_MCASMDEBUG_H

namespace llvm {
namespace McasmDebug {

bool isEnabled();
void log(const char *Fmt, ...);

} // namespace McasmDebug
} // namespace llvm

#define MCASM_DEBUG_LOG(...)                                                    \
  do {                                                                          \
    if (::llvm::McasmDebug::isEnabled())                                        \
      ::llvm::McasmDebug::log(__VA_ARGS__);                                     \
  } while (false)

#endif // LLVM_LIB_TARGET_Mcasm_TARGETINFO_MCASMDEBUG_H
