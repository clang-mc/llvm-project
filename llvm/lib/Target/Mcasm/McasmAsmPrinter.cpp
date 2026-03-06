//===-- McasmAsmPrinter.cpp - Convert Mcasm LLVM code to mcasm assembly --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to mcasm assembly language.
//
// MCASM NOTE: This is a minimal rewrite for the mcasm backend. It outputs
// mcasm-specific syntax:
// - File header: #include "_ll_std"
// - External functions: export _ll_shared:funcname:
// - Internal functions: funcname:
// - Global variables: static varname [val1, val2, ...]
//
// CRITICAL: This file does NOT perform memory offset conversion. All offsets
// are already in mcasm units from FrameLowering and RegisterInfo.
//
//===----------------------------------------------------------------------===//

#include "McasmAsmPrinter.h"
#include "TargetInfo/McasmDebug.h"
#include "MCTargetDesc/McasmMCTargetDesc.h"
#include "MCTargetDesc/McasmInstPrinter.h"
#include "MCTargetDesc/McasmBaseInfo.h"
#include "Mcasm.h"
#include "McasmMCInstLower.h"
#include "McasmSubtarget.h"
#include "McasmTargetObjectFile.h"
#include "TargetInfo/McasmTargetInfo.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cctype>

using namespace llvm;

static void printMcasmMemOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const MachineOperand &BaseReg = MI->getOperand(OpNo + Mcasm::AddrBaseReg);
  const MachineOperand &Scale = MI->getOperand(OpNo + Mcasm::AddrScaleAmt);
  const MachineOperand &IndexReg = MI->getOperand(OpNo + Mcasm::AddrIndexReg);
  const MachineOperand &Disp = MI->getOperand(OpNo + Mcasm::AddrDisp);

  O << "[";
  bool NeedPlus = false;

  if (BaseReg.isReg() && BaseReg.getReg() != 0) {
    O << McasmInstPrinter::getRegisterName(BaseReg.getReg());
    NeedPlus = true;
  }

  if (IndexReg.isReg() && IndexReg.getReg() != 0) {
    if (NeedPlus)
      O << "+";
    if (Scale.isImm() && Scale.getImm() != 1)
      O << Scale.getImm() << "*";
    O << McasmInstPrinter::getRegisterName(IndexReg.getReg());
    NeedPlus = true;
  }

  if (Disp.isImm()) {
    int64_t V = Disp.getImm();
    if (V != 0 || !NeedPlus) {
      if (NeedPlus && V >= 0)
        O << "+";
      O << V;
      NeedPlus = true;
    }
  }

  if (!NeedPlus)
    O << "0";

  O << "]";
}

static std::string makeAlphaFieldName(unsigned Index) {
  std::string Name;
  unsigned N = Index;
  do {
    Name.push_back(static_cast<char>('a' + (N % 26)));
    if (N < 26)
      break;
    N = N / 26 - 1;
  } while (true);
  std::reverse(Name.begin(), Name.end());
  return Name;
}

static bool isMacroIdentifier(StringRef Name) {
  if (Name.empty())
    return false;
  auto IsIdStart = [](char C) {
    return std::isalpha(static_cast<unsigned char>(C)) || C == '_';
  };
  auto IsIdChar = [](char C) {
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
  };
  if (!IsIdStart(Name.front()))
    return false;
  return llvm::all_of(Name.drop_front(), IsIdChar);
}

struct InlineAsmOperandDesc {
  unsigned ValIndex = 0;
  unsigned FlagOpNo = 0;
  unsigned OpNo = 0;
  InlineAsm::Flag Flag = InlineAsm::Flag(0);
};

static bool buildInlineAsmOperandDescs(const MachineInstr *MI,
                                       SmallVectorImpl<InlineAsmOperandDesc> &Descs,
                                       std::string &Err) {
  unsigned ValIndex = 0;
  for (unsigned I = InlineAsm::MIOp_FirstOperand, E = MI->getNumOperands(); I < E;) {
    const MachineOperand &MO = MI->getOperand(I);
    if (MO.isMetadata())
      break;
    if (!MO.isImm()) {
      Err = "inline asm operand descriptor is not immediate";
      return false;
    }
    InlineAsm::Flag F(MO.getImm());
    InlineAsmOperandDesc D;
    D.ValIndex = ValIndex++;
    D.FlagOpNo = I;
    D.OpNo = I + 1;
    D.Flag = F;
    Descs.push_back(D);
    I += F.getNumOperandRegisters() + 1;
  }
  return true;
}

static std::string formatInlineAsmOperandReplacement(const MachineInstr *MI,
                                                     const InlineAsmOperandDesc &D,
                                                     StringRef RegInputField) {
  const MachineOperand &MO = MI->getOperand(D.OpNo);
  if (D.Flag.isRegUseKind()) {
    return (Twine("$(") + RegInputField + ")").str();
  }
  if (D.Flag.isRegDefKind() || D.Flag.isRegDefEarlyClobberKind()) {
    if (MO.isReg())
      return McasmInstPrinter::getRegisterName(MO.getReg());
    return "<bad-regdef>";
  }
  if (D.Flag.isImmKind()) {
    if (MO.isImm())
      return std::to_string(MO.getImm());
    return "<bad-imm>";
  }
  if (D.Flag.isMemKind()) {
    std::string S;
    raw_string_ostream OS(S);
    printMcasmMemOperand(MI, D.OpNo, OS);
    return OS.str();
  }
  return "<unsupported-op>";
}

static std::string replaceInlineAsmPlaceholders(
    StringRef AsmStr, const MachineInstr *MI,
    const SmallVectorImpl<InlineAsmOperandDesc> &Descs,
    const DenseMap<unsigned, std::string> &RegInputFieldByVal,
    std::string &Err) {
  std::string Out;
  for (size_t I = 0; I < AsmStr.size();) {
    char C = AsmStr[I];
    if (C != '$') {
      Out.push_back(C);
      ++I;
      continue;
    }

    if (I + 1 < AsmStr.size() && AsmStr[I + 1] == '$') {
      Out.push_back('$');
      I += 2;
      continue;
    }

    size_t J = I + 1;
    if (J >= AsmStr.size() || !std::isdigit(static_cast<unsigned char>(AsmStr[J]))) {
      Out.push_back('$');
      ++I;
      continue;
    }

    unsigned Val = 0;
    while (J < AsmStr.size() && std::isdigit(static_cast<unsigned char>(AsmStr[J]))) {
      Val = Val * 10 + static_cast<unsigned>(AsmStr[J] - '0');
      ++J;
    }

    if (Val >= Descs.size()) {
      Err = (Twine("inline asm placeholder out of range: $") + Twine(Val)).str();
      return {};
    }

    const InlineAsmOperandDesc &D = Descs[Val];
    auto It = RegInputFieldByVal.find(Val);
    StringRef Field = (It == RegInputFieldByVal.end()) ? StringRef() : StringRef(It->second);
    Out += formatInlineAsmOperandReplacement(MI, D, Field);
    I = J;
  }
  return Out;
}

static std::string rewriteMcasmInlineHelperBody(StringRef Body) {
  SmallVector<StringRef, 8> Lines;
  Body.split(Lines, '\n');

  std::string Out;
  for (size_t I = 0; I < Lines.size(); ++I) {
    StringRef Line = Lines[I];
    size_t FirstNonWs = Line.find_first_not_of(" \t");
    if (FirstNonWs == StringRef::npos) {
      Out += Line.str();
    } else {
      StringRef Prefix = Line.take_front(FirstNonWs);
      StringRef Core = Line.drop_front(FirstNonWs);
      if (Core.starts_with("inline ") && !Core.starts_with("inline $") &&
          Core.contains("$(")) {
        Out += Prefix.str();
        Out += (Twine("inline $") + Core.drop_front(7)).str();
      } else {
        Out += Line.str();
      }
    }

    if (I + 1 < Lines.size())
      Out += '\n';
  }

  return Out;
}

McasmAsmPrinter::McasmAsmPrinter(TargetMachine &TM,
                                 std::unique_ptr<MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), Subtarget(nullptr) {
  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter constructor completed\n");
}

void McasmAsmPrinter::emitStartOfAsmFile(Module &M) {
  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter::emitStartOfAsmFile called\n");
  // mcasm requires #include "_ll_std" at the start of every file
  OutStreamer->emitRawText("#include \"_ll_std\"");
  OutStreamer->emitRawText("");  // Blank line

  // mcasm has no native symbol alias syntax (e.g. "a = b").
  // Materialize direct aliases as preprocessor defines.
  MacroAliases.clear();
  bool EmittedAliasDefines = false;
  for (const GlobalAlias &GA : M.aliases()) {
    if (GA.hasAvailableExternallyLinkage())
      continue;
    const auto *Aliasee =
        dyn_cast<GlobalValue>(GA.getAliasee()->stripPointerCasts());
    if (!Aliasee)
      continue;
    MCSymbol *AliasSym = getSymbol(&GA);
    MCSymbol *TargetSym = getSymbol(Aliasee);
    StringRef AliasName = AliasSym->getName();
    StringRef TargetName = TargetSym->getName();
    if (AliasName == TargetName)
      continue;
    if (!isMacroIdentifier(AliasName) || !isMacroIdentifier(TargetName))
      continue;
    OutStreamer->emitRawText(
        (Twine("#define ") + AliasName + " " + TargetName).str());
    MacroAliases.insert(&GA);
    EmittedAliasDefines = true;
  }
  if (EmittedAliasDefines)
    OutStreamer->emitRawText("");

  // Emit extern declarations for dllimport functions
  for (const Function &F : M) {
    if (F.isDeclaration() && F.hasDLLImportStorageClass()) {
      // Generate: extern _ll_shared:funcname:
      std::string ExternDecl = "extern ";
      ExternDecl += getSymbol(&F)->getName();
      ExternDecl += ":";
      MCASM_DEBUG_LOG("DEBUG:   Emitting extern declaration: %s\n", ExternDecl.c_str());
      OutStreamer->emitRawText(ExternDecl);
    }
  }

  if (std::any_of(M.begin(), M.end(), [](const Function &F) {
        return F.isDeclaration() && F.hasDLLImportStorageClass();
      })) {
    OutStreamer->emitRawText("");  // Blank line after extern declarations
  }

  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter::emitStartOfAsmFile completed\n");
}

void McasmAsmPrinter::emitEndOfAsmFile(Module &M) {
  for (const InlineAsmHelperRecord &R : InlineAsmHelpers) {
    OutStreamer->emitRawText("");
    OutStreamer->emitRawText((Twine("export ") + R.Label + ":").str());
    SmallVector<StringRef, 8> BodyLines;
    StringRef(R.Body).split(BodyLines, '\n');
    for (StringRef Line : BodyLines)
      OutStreamer->emitRawText((Twine("    ") + Line).str());
    OutStreamer->emitRawText("\tret");
  }
}

bool McasmAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  // Set subtarget
  Subtarget = &MF.getSubtarget<McasmSubtarget>();

  // Call base class implementation
  return AsmPrinter::runOnMachineFunction(MF);
}

bool McasmAsmPrinter::emitInlineAsmCustom(const MachineInstr *MI) {
  return emitMcasmInlineAsmWrapper(MI);
}

bool McasmAsmPrinter::emitMcasmInlineAsmWrapper(const MachineInstr *MI) {
  const Function &F = MF->getFunction();
  const char *AsmStr = MI->getOperand(InlineAsm::MIOp_AsmString).getSymbolName();
  if (!AsmStr) {
    report_fatal_error("mcasm inline asm wrapper: missing asm string");
  }

  SmallVector<InlineAsmOperandDesc, 8> Descs;
  std::string Err;
  if (!buildInlineAsmOperandDescs(MI, Descs, Err))
    report_fatal_error(Twine("mcasm inline asm wrapper: ") + Err);

  DenseMap<unsigned, std::string> RegInputFieldByVal;
  SmallVector<unsigned, 8> RegInputVals;
  bool HasAnyInput = false;
  for (const InlineAsmOperandDesc &D : Descs) {
    if (D.Flag.isRegUseKind() || D.Flag.isImmKind() || D.Flag.isMemKind())
      HasAnyInput = true;
    if (!D.Flag.isRegUseKind())
      continue;
    const MachineOperand &MO = MI->getOperand(D.OpNo);
    if (!MO.isReg())
      report_fatal_error("mcasm inline asm wrapper: reg input is not a register");
    std::string Field = makeAlphaFieldName(static_cast<unsigned>(RegInputVals.size()));
    RegInputVals.push_back(D.ValIndex);
    RegInputFieldByVal.try_emplace(D.ValIndex, Field);
  }

  std::string Replaced = replaceInlineAsmPlaceholders(StringRef(AsmStr), MI, Descs,
                                                      RegInputFieldByVal, Err);
  if (!Err.empty())
    report_fatal_error(Twine("mcasm inline asm wrapper: ") + Err);

  // No-input asm can be emitted inline directly; helper wrapping is only
  // needed for storage-based argument passing.
  if (!HasAnyInput) {
    OutStreamer->emitRawText("\t" + Replaced);
    return true;
  }

  std::string HelperBody = rewriteMcasmInlineHelperBody(Replaced);

  unsigned Seq = InlineAsmCounter[&F]++;
  std::string HelperName =
      rewriteMcasmSharedName((Twine("z/") + F.getName() + "_" + Twine(Seq)).str());
  std::string Label = (Twine("_ll_shared:") + HelperName).str();
  InlineAsmHelpers.push_back({Label, HelperBody});

  std::string Init = "inline data modify storage std:vm s0 set value {";
  for (unsigned I = 0; I < RegInputVals.size(); ++I) {
    if (I)
      Init += ", ";
    Init += RegInputFieldByVal.lookup(RegInputVals[I]);
    Init += ": -1";
  }
  Init += "}";
  OutStreamer->emitRawText("\t" + Init);

  for (unsigned Val : RegInputVals) {
    const InlineAsmOperandDesc &D = Descs[Val];
    const MachineOperand &MO = MI->getOperand(D.OpNo);
    std::string Reg = McasmInstPrinter::getRegisterName(MO.getReg());
    std::string Field = RegInputFieldByVal.lookup(Val);
    std::string Line =
        (Twine("inline execute store result storage std:vm s0.") + Field +
         " int 1 run scoreboard players get " + Reg + " vm_regs")
            .str();
    OutStreamer->emitRawText("\t" + Line);
  }

  OutStreamer->emitRawText(
      (Twine("\tinline function ") + Label + " with storage std:vm s0").str());
  return true;
}

bool McasmAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true;
    switch (ExtraCode[0]) {
    case 'a':
      return PrintAsmMemoryOperand(MI, OpNo, nullptr, O);
    case 'c': {
      const MachineOperand &MO = MI->getOperand(OpNo);
      if (MO.isImm()) {
        O << MO.getImm();
        return false;
      }
      if (MO.isGlobal()) {
        PrintSymbolOperand(MO, O);
        return false;
      }
      if (MO.isReg()) {
        O << McasmInstPrinter::getRegisterName(MO.getReg());
        return false;
      }
      return true;
    }
    case 'n': {
      const MachineOperand &MO = MI->getOperand(OpNo);
      if (!MO.isImm())
        return true;
      O << -MO.getImm();
      return false;
    }
    default:
      return true;
    }
  }

  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << McasmInstPrinter::getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, O);
    return false;
  case MachineOperand::MO_ExternalSymbol:
    GetExternalSymbolSymbol(MO.getSymbolName())->print(O, MAI);
    return false;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    return false;
  case MachineOperand::MO_BlockAddress:
    GetBlockAddressSymbol(MO.getBlockAddress())->print(O, MAI);
    return false;
  default:
    return true;
  }
}

bool McasmAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo,
                                            const char *ExtraCode,
                                            raw_ostream &O) {
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true;
    if (ExtraCode[0] != 'a')
      return true;
  }

  const MachineOperand &MO = MI->getOperand(OpNo);
  if (MO.isReg()) {
    O << "[" << McasmInstPrinter::getRegisterName(MO.getReg()) << "]";
    return false;
  }

  printMcasmMemOperand(MI, OpNo, O);
  return false;
}

void McasmAsmPrinter::emitLinkage(const GlobalValue *GV, MCSymbol *Sym) const {
  // mcasm uses "export _ll_shared:name:" syntax for external linkage
  // Don't emit .globl or other ELF-style linkage directives
  // The actual export label is emitted in emitFunctionEntryLabel()
  // So this function is intentionally empty
}

void McasmAsmPrinter::emitFunctionBodyStart() {
  // mcasm doesn't need CFI directives (.cfi_startproc)
  // Override to prevent base class from emitting them
}

void McasmAsmPrinter::emitFunctionBodyEnd() {
  // mcasm doesn't need CFI directives (.cfi_endproc) or .size directives
  // Override to prevent base class from emitting them
}

void McasmAsmPrinter::emitFunctionEntryLabel() {
  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter::emitFunctionEntryLabel called\n");

  MCSymbol *FnSym = CurrentFnSym;
  MCASM_DEBUG_LOG("DEBUG:   FunctionName = %s\n", FnSym->getName().str().c_str());

  // Determine DLL storage class
  const Function &F = MF->getFunction();
  bool IsDLLExport = F.hasDLLExportStorageClass();
  bool IsDLLImport = F.hasDLLImportStorageClass();
  MCASM_DEBUG_LOG("DEBUG:   IsDLLExport = %d, IsDLLImport = %d\n",
          (int)IsDLLExport, (int)IsDLLImport);

  if (IsDLLExport) {
    // __declspec(dllexport): export _ll_shared:funcname:
    // NOTE: The symbol name already includes _ll_shared: prefix (added by getTargetSymbol)
    std::string Label = "export ";
    Label += FnSym->getName();
    Label += ":";
    MCASM_DEBUG_LOG("DEBUG:   Emitting export label: %s\n", Label.c_str());
    OutStreamer->emitRawText(Label);
  } else if (IsDLLImport) {
    // __declspec(dllimport): extern _ll_shared:funcname:
    // NOTE: The symbol name already includes _ll_shared: prefix (added by getTargetSymbol)
    std::string Label = "extern ";
    Label += FnSym->getName();
    Label += ":";
    MCASM_DEBUG_LOG("DEBUG:   Emitting extern label: %s\n", Label.c_str());
    OutStreamer->emitRawText(Label);
  } else {
    // Regular function: funcname:
    MCASM_DEBUG_LOG("DEBUG:   Emitting regular label\n");
    OutStreamer->emitLabel(FnSym);
  }

  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter::emitFunctionEntryLabel completed\n");
}

void McasmAsmPrinter::emitInstruction(const MachineInstr *MI) {
  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter::emitInstruction called, Opcode=%u\n", MI->getOpcode());

  // Skip pseudo instructions
  if (MI->isPseudo()) {
    MCASM_DEBUG_LOG("DEBUG:   Skipping pseudo instruction\n");
    return;
  }

  // Lower MachineInstr to MCInst
  MCInst TmpInst;
  if (!MCInstLowering) {
    MCInstLowering = std::make_unique<McasmMCInstLower>(OutContext, *MF, *this);
  }
  MCInstLowering->Lower(MI, TmpInst);

  MCASM_DEBUG_LOG("DEBUG:   About to emit MCInst\n");

  // Emit the MCInst
  // NOTE: Memory offsets in TmpInst are already in mcasm units.
  // Do NOT perform any offset conversion here!
  EmitToStreamer(*OutStreamer, TmpInst);
}

void McasmAsmPrinter::emitGlobalAlias(const Module &, const GlobalAlias &GA) {
  if (!MacroAliases.contains(&GA)) {
    report_fatal_error(Twine("mcasm does not support this alias form: ") +
                       GA.getName());
  }
  // Handled in emitStartOfAsmFile() as #define lines.
  // Suppress default AsmPrinter alias assignment emission.
}

void McasmAsmPrinter::emitGlobalIFunc(Module &, const GlobalIFunc &GI) {
  report_fatal_error(Twine("mcasm does not support ifunc: ") + GI.getName());
}

void McasmAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  // Only emit initialized global variables
  if (!GV->hasInitializer()) {
    return;
  }

  // mcasm syntax: static varname [val1, val2, ...]
  std::string Output = "static ";

  // Get the variable name and sanitize it for mcasm
  std::string VarName = GV->getName().str();

  // Remove leading dot if present (e.g., .str -> str)
  if (!VarName.empty() && VarName[0] == '.') {
    VarName = VarName.substr(1);
  }

  // Replace remaining dots with underscores (e.g., str.1 -> str_1)
  for (char &C : VarName) {
    if (C == '.') {
      C = '_';
    }
  }

  Output += VarName;
  Output += " [";

  const Constant *C = GV->getInitializer();

  // Emit initializer values
  if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(C)) {
    // Array of integers
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      if (i > 0) Output += ", ";
      Output += std::to_string(CDS->getElementAsInteger(i));
    }
  } else if (const ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
    // Single integer
    Output += std::to_string(CI->getSExtValue());
  } else if (const ConstantAggregateZero *CAZ = dyn_cast<ConstantAggregateZero>(C)) {
    // Zero initializer
    Type *Ty = CAZ->getType();
    if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
      unsigned NumElems = ATy->getNumElements();
      for (unsigned i = 0; i < NumElems; ++i) {
        if (i > 0) Output += ", ";
        Output += "0";
      }
    } else {
      Output += "0";
    }
  } else if (const ConstantArray *CA = dyn_cast<ConstantArray>(C)) {
    // Array of constants
    for (unsigned i = 0, e = CA->getNumOperands(); i != e; ++i) {
      if (i > 0) Output += ", ";
      if (const ConstantInt *CI = dyn_cast<ConstantInt>(CA->getOperand(i))) {
        Output += std::to_string(CI->getSExtValue());
      } else {
        Output += "0"; // Fallback for non-integer elements
      }
    }
  } else {
    // Unknown initializer type, emit zero
    Output += "0";
  }

  Output += "]";
  OutStreamer->emitRawText(Output);
}

// Register the AsmPrinter
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMcasmAsmPrinter() {
  MCASM_DEBUG_LOG("DEBUG: LLVMInitializeMcasmAsmPrinter called\n");
  RegisterAsmPrinter<McasmAsmPrinter> X(getTheMcasm_32Target());
  MCASM_DEBUG_LOG("DEBUG: LLVMInitializeMcasmAsmPrinter completed\n");
}

