add_llvm_component_library(LLVMMcasmInfo
  McasmTargetInfo.cpp
  McasmDebug.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Mcasm
  )

