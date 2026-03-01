//===-- McasmAsmPrinter.h - Mcasm Assembly Printer --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mcasm implementation of AsmPrinter.
//
// MCASM NOTE: This is a minimal rewrite for the mcasm backend, which outputs
// mcasm assembly syntax with special headers, function labels, and static data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCASM_MCASMASMPRINTER_H
#define LLVM_LIB_TARGET_MCASM_MCASMASMPRINTER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {

class MCStreamer;
class McasmSubtarget;
class McasmMCInstLower;

class LLVM_LIBRARY_VISIBILITY McasmAsmPrinter : public AsmPrinter {
  const McasmSubtarget *Subtarget;
  std::unique_ptr<McasmMCInstLower> MCInstLowering;
  struct InlineAsmHelperRecord {
    std::string Label;
    std::string Body;
  };
  DenseMap<const Function *, unsigned> InlineAsmCounter;
  SmallVector<InlineAsmHelperRecord, 16> InlineAsmHelpers;

public:
  explicit McasmAsmPrinter(TargetMachine &TM,
                           std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override { return "Mcasm Assembly Printer"; }

  const McasmSubtarget &getSubtarget() const { return *Subtarget; }

  void emitStartOfAsmFile(Module &M) override;
  void emitEndOfAsmFile(Module &M) override;
  void emitLinkage(const GlobalValue *GV, MCSymbol *Sym) const override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyStart() override;
  void emitFunctionBodyEnd() override;
  void emitInstruction(const MachineInstr *MI) override;
  void emitGlobalVariable(const GlobalVariable *GV) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &O) override;

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool emitInlineAsmCustom(const MachineInstr *MI) override;
  bool emitMcasmInlineAsmWrapper(const MachineInstr *MI);
};

} // namespace llvm

#endif
