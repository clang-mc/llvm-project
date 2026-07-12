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
#include "llvm/ADT/DenseSet.h"
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cctype>

using namespace llvm;

// Whole-program LTO path: when the user program is linked together with the
// stdlib bitcode (libc/libmc) into a single module, the library function
// bodies are already present in this module. Re-emitting the `_ll_libc` /
// `_ll_libmc` bundle includes on top of that would redefine every library
// label. This flag suppresses those two includes (but keeps `_ll_std`, which
// provides external MC runtime primitives + compiler-rt). See
// clang-mc/tools/foo-benchmark/TASK-driver-wholeprogram-lto.md §5.1.
static cl::opt<bool> McasmNoStdlibInclude(
    "mcasm-no-stdlib-include",
    cl::desc("Suppress the automatic #include \"_ll_libc\"/\"_ll_libmc\" "
             "bundle lines (used by the whole-program LTO path where the "
             "library bodies are linked into the module)."),
    cl::init(false), cl::Hidden);

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

struct McasmInlineAsmOptions {
  bool DirectArgs = false;
};

static std::string unescapeInlineAsmDollarPairs(StringRef S) {
  std::string Out;
  for (size_t I = 0; I < S.size(); ++I) {
    Out.push_back(S[I]);
    if (S[I] == '$' && I + 1 < S.size() && S[I + 1] == '$')
      ++I;
  }
  return Out;
}

static std::string parseMcasmInlineAsmOptions(StringRef AsmStr,
                                              McasmInlineAsmOptions &Options,
                                              std::string &Err) {
  SmallVector<StringRef, 8> Lines;
  AsmStr.split(Lines, '\n');

  std::string Out;
  bool InOptionBlock = true;
  for (size_t I = 0; I < Lines.size(); ++I) {
    StringRef Line = Lines[I];
    StringRef Trimmed = Line.trim();
    std::string UnescapedStorage;
    StringRef OptionLine = Trimmed;
    if (Trimmed.starts_with("$$$$")) {
      UnescapedStorage = unescapeInlineAsmDollarPairs(Trimmed);
      OptionLine = UnescapedStorage;
    }

    if (InOptionBlock) {
      if (Trimmed.empty())
        continue;

      if (OptionLine.starts_with("$$")) {
        if (OptionLine == "$$direct_args") {
          Options.DirectArgs = true;
          continue;
        }
        Err = (Twine("unsupported mcasm inline asm option: ") + OptionLine)
                  .str();
        return {};
      }

      InOptionBlock = false;
    } else if (OptionLine.starts_with("$$")) {
      Err = (Twine("mcasm inline asm option must appear before asm body: ") +
             OptionLine)
                .str();
      return {};
    }

    Out += Line.str();
    if (I + 1 < Lines.size())
      Out += '\n';
  }

  return Out;
}

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
    std::string &Err, DenseSet<unsigned> *ReferencedVals = nullptr) {
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

    if (I + 1 < AsmStr.size()) {
      switch (AsmStr[I + 1]) {
      case '(':
        Out.push_back('{');
        I += 2;
        continue;
      case '|':
        Out.push_back('|');
        I += 2;
        continue;
      case ')':
        Out.push_back('}');
        I += 2;
        continue;
      default:
        break;
      }
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

    if (ReferencedVals)
      ReferencedVals->insert(Val);
    const InlineAsmOperandDesc &D = Descs[Val];
    auto It = RegInputFieldByVal.find(Val);
    StringRef Field = (It == RegInputFieldByVal.end()) ? StringRef() : StringRef(It->second);
    Out += formatInlineAsmOperandReplacement(MI, D, Field);
    I = J;
  }
  return Out;
}

// Rewrite `mov rN, $(param)` -> `inline $scoreboard players set rN vm_regs
// $(param)`. Returns true (and fills Out) when Core matches that exact shape.
// Core is the leading-whitespace-stripped line; Prefix is that whitespace.
static bool rewriteMcasmMacroRegLoad(StringRef Prefix, StringRef Core,
                                     std::string &Out) {
  if (!Core.starts_with("mov"))
    return false;
  StringRef Rest = Core.drop_front(3);
  size_t WsLen = Rest.find_first_not_of(" \t");
  if (WsLen == 0 || WsLen == StringRef::npos)
    return false;
  Rest = Rest.drop_front(WsLen);
  // Register operand: r<digits>
  if (Rest.empty() || Rest.front() != 'r')
    return false;
  size_t RegEnd = 1;
  while (RegEnd < Rest.size() && std::isdigit((unsigned char)Rest[RegEnd]))
    ++RegEnd;
  if (RegEnd == 1)
    return false;
  StringRef Reg = Rest.take_front(RegEnd);
  Rest = Rest.drop_front(RegEnd);
  // Separating comma (optionally surrounded by spaces).
  Rest = Rest.ltrim(" \t");
  if (!Rest.consume_front(","))
    return false;
  Rest = Rest.ltrim(" \t");
  // Macro-parameter source: $(...) spanning the remainder of the line.
  if (!Rest.starts_with("$(") || !Rest.ends_with(")"))
    return false;
  Out = (Prefix + "inline $scoreboard players set " + Reg + " vm_regs " + Rest)
            .str();
  return true;
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
      std::string Rewritten;
      if (Core.starts_with("inline ") && !Core.starts_with("inline $") &&
          Core.contains("$(")) {
        Out += Prefix.str();
        Out += (Twine("inline $") + Core.drop_front(7)).str();
      } else if (rewriteMcasmMacroRegLoad(Prefix, Core, Rewritten)) {
        // Defensive: `mov rN, $(param)` is illegal inside a macro mcfunction
        // body; it must be materialized via a scoreboard set. The current
        // backend already emits inline asm inputs through scoreboard ops so
        // this normally matches nothing, but the whole-program LTO path relies
        // on it never leaking (task §5.2).
        Out += Rewritten;
      } else {
        Out += Line.str();
      }
    }

    if (I + 1 < Lines.size())
      Out += '\n';
  }

  return Out;
}

static void appendMcasmInitAtom(std::string &Output, bool &NeedComma,
                                StringRef Value) {
  if (NeedComma)
    Output += ", ";
  Output += Value;
  NeedComma = true;
}

static void appendMcasmInitWord(std::string &Output, bool &NeedComma,
                                uint32_t Word) {
  appendMcasmInitAtom(Output, NeedComma,
                      std::to_string(static_cast<int32_t>(Word)));
}

static void appendMcasmInitBits(const APInt &Bits, uint64_t StorageBits,
                                std::string &Output, bool &NeedComma) {
  unsigned NumWords = std::max<uint64_t>(1, divideCeil(StorageBits, 32ULL));
  APInt Padded = Bits.zextOrTrunc(NumWords * 32);
  for (unsigned I = 0; I != NumWords; ++I) {
    uint32_t Word = Padded.lshr(I * 32).trunc(32).getZExtValue();
    appendMcasmInitWord(Output, NeedComma, Word);
  }
}

static void ensureMcasmInitSlot(SmallVectorImpl<uint32_t> &Slots,
                                uint64_t Offset) {
  if (Slots.size() <= Offset)
    Slots.resize(Offset + 1, 0);
}

static void ensureMcasmInitRange(SmallVectorImpl<uint32_t> &Slots,
                                 uint64_t Offset, uint64_t Size) {
  if (Size == 0)
    Size = 1;
  ensureMcasmInitSlot(Slots, Offset + Size - 1);
}

static void writeBitsToMcasmSlots(const APInt &Bits, uint64_t StorageBits,
                                  SmallVectorImpl<uint32_t> &Slots,
                                  uint64_t Offset) {
  uint64_t NumBytes = std::max<uint64_t>(1, divideCeil(StorageBits, 8ULL));
  APInt Padded = Bits.zextOrTrunc(NumBytes * 8);
  for (uint64_t I = 0; I < NumBytes; I += 4) {
    uint32_t Word = 0;
    for (unsigned Byte = 0; Byte != 4 && I + Byte < NumBytes; ++Byte)
      Word |= Padded.lshr((I + Byte) * 8).trunc(8).getZExtValue()
              << (Byte * 8);
    Slots[Offset + I] = Word;
  }
}

static bool collectMcasmPointerInit(
    const Constant *C, const DataLayout &DL, uint64_t Offset,
    McasmAsmPrinter &AP, SmallVectorImpl<McasmRuntimeStaticInitEntry> &Entries) {
  const auto *Stripped = C->stripPointerCasts();
  if (const auto *GV = dyn_cast<GlobalValue>(Stripped)) {
    Entries.push_back(
        {Offset, AP.getSymbol(GV)->getName().str(), 0, isa<Function>(GV)});
    return true;
  }

  const auto *GEP = dyn_cast<GEPOperator>(C);
  if (!GEP)
    return false;

  APInt AddendBits(DL.getPointerTypeSizeInBits(C->getType()), 0, true);
  if (!GEP->accumulateConstantOffset(DL, AddendBits))
    return false;

  const auto *BaseGV =
      dyn_cast<GlobalValue>(GEP->getPointerOperand()->stripPointerCasts());
  if (!BaseGV)
    return false;

  Entries.push_back({Offset, AP.getSymbol(BaseGV)->getName().str(),
                     AddendBits.getSExtValue(), isa<Function>(BaseGV)});
  return true;
}

static void serializeMcasmInitializer(const Constant *C, const DataLayout &DL,
                                      SmallVectorImpl<uint32_t> &Slots,
                                      uint64_t Offset, McasmAsmPrinter &AP,
                                      SmallVectorImpl<McasmRuntimeStaticInitEntry> &Entries) {
  Type *Ty = C->getType();
  uint64_t AllocSize = DL.getTypeAllocSize(Ty).getFixedValue();
  ensureMcasmInitRange(Slots, Offset, AllocSize);

  if (isa<UndefValue>(C) || isa<ConstantAggregateZero>(C) || C->isNullValue()) {
    return;
  }

  if (const auto *CI = dyn_cast<ConstantInt>(C)) {
    writeBitsToMcasmSlots(CI->getValue(), DL.getTypeStoreSizeInBits(Ty), Slots,
                          Offset);
    return;
  }

  if (const auto *CFP = dyn_cast<ConstantFP>(C)) {
    writeBitsToMcasmSlots(CFP->getValueAPF().bitcastToAPInt(),
                          DL.getTypeStoreSizeInBits(Ty), Slots, Offset);
    return;
  }

  if (collectMcasmPointerInit(C, DL, Offset, AP, Entries)) {
    return;
  }

  if (const auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
    uint64_t ElementSize =
        DL.getTypeAllocSize(CDS->getElementType()).getFixedValue();
    for (unsigned I = 0, E = CDS->getNumElements(); I != E; ++I)
      serializeMcasmInitializer(CDS->getElementAsConstant(I), DL, Slots,
                                Offset + I * ElementSize, AP, Entries);
    return;
  }

  if (const auto *CA = dyn_cast<ConstantArray>(C)) {
    uint64_t ElementSize =
        DL.getTypeAllocSize(CA->getType()->getElementType()).getFixedValue();
    for (unsigned I = 0, E = CA->getNumOperands(); I != E; ++I)
      serializeMcasmInitializer(cast<Constant>(CA->getOperand(I)), DL, Slots,
                                Offset + I * ElementSize, AP, Entries);
    return;
  }

  if (const auto *CS = dyn_cast<ConstantStruct>(C)) {
    const StructLayout *SL = DL.getStructLayout(CS->getType());
    for (unsigned I = 0, E = CS->getNumOperands(); I != E; ++I) {
      serializeMcasmInitializer(cast<Constant>(CS->getOperand(I)), DL,
                                Slots, Offset + SL->getElementOffset(I), AP,
                                Entries);
    }
    return;
  }

  if (const auto *CV = dyn_cast<ConstantVector>(C)) {
    uint64_t ElementSize =
        DL.getTypeAllocSize(CV->getType()->getElementType()).getFixedValue();
    for (unsigned I = 0, E = CV->getNumOperands(); I != E; ++I)
      serializeMcasmInitializer(cast<Constant>(CV->getOperand(I)), DL, Slots,
                                Offset + I * ElementSize, AP, Entries);
    return;
  }
}

static SmallVector<McasmRuntimeStaticInitEntry, 4>
appendMcasmInitializer(const Constant *C, const DataLayout &DL,
                      std::string &Output, bool &NeedComma,
                      McasmAsmPrinter &AP) {
  SmallVector<uint32_t, 64> Slots;
  SmallVector<McasmRuntimeStaticInitEntry, 4> Entries;
  serializeMcasmInitializer(C, DL, Slots, 0, AP, Entries);
  if (Slots.empty())
    Slots.push_back(0);

  for (uint32_t Slot : Slots)
    appendMcasmInitWord(Output, NeedComma, Slot);
  return Entries;
}

McasmAsmPrinter::McasmAsmPrinter(TargetMachine &TM,
                                 std::unique_ptr<MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), Subtarget(nullptr) {
  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter constructor completed\n");
}

void McasmAsmPrinter::emitStartOfAsmFile(Module &M) {
  MCASM_DEBUG_LOG("DEBUG: McasmAsmPrinter::emitStartOfAsmFile called\n");
  InlineAsmHelpers.clear();
  InlineAsmHelperIndexByKey.clear();
  InlineAsmCounter.clear();
  RuntimeStaticInits.clear();
  for (const GlobalVariable &GV : M.globals()) {
    if (!GV.hasInitializer())
      continue;
    std::string Scratch;
    bool NeedComma = false;
    auto RuntimeEntries =
        appendMcasmInitializer(GV.getInitializer(), M.getDataLayout(), Scratch,
                               NeedComma, *this);
    if (RuntimeEntries.empty())
      continue;
    RuntimeStaticInits.push_back({&GV, std::move(RuntimeEntries)});
  }
  // mcasm requires #include "_ll_std" at the start of every file
  OutStreamer->emitRawText("#include \"_ll_std\"");
  if (!McasmNoStdlibInclude) {
    if (M.getModuleFlag("mcasm-libc-include"))
      OutStreamer->emitRawText("#include \"_ll_libc\"");
    if (M.getModuleFlag("mcasm-libmc-include"))
      OutStreamer->emitRawText("#include \"_ll_libmc\"");
  }
  bool HasMainDefinition = llvm::any_of(M, [](const Function &F) {
    return !F.isDeclaration() && F.getName() == "main";
  });
  bool NeedCustomStart = HasMainDefinition && !RuntimeStaticInits.empty();
  if (HasMainDefinition && !NeedCustomStart)
    OutStreamer->emitRawText("#include \"_ll_crt\"");
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

  if (NeedCustomStart) {
    OutStreamer->emitRawText("// -- Begin function _start");
    OutStreamer->emitRawText("_start:                                 // @_start");
    OutStreamer->emitRawText("// %bb.0:");
    for (const McasmRuntimeStaticInitRecord &Record : RuntimeStaticInits) {
      OutStreamer->emitRawText(
          (Twine("\tmov\tr0, ") + getSymbol(Record.GV)->getName()).str());
      for (const McasmRuntimeStaticInitEntry &Entry : Record.Entries) {
        std::string Dest = "[r0]";
        if (Entry.Offset != 0)
          Dest = (Twine("[r0+") + Twine(Entry.Offset) + "]").str();
        if (Entry.IsFunctionRef) {
          OutStreamer->emitRawText(
              (Twine("\tmovd\t") + Dest + ", " + Entry.Target).str());
          continue;
        }

        OutStreamer->emitRawText((Twine("\tmov\tr1, ") + Entry.Target).str());
        if (Entry.Addend != 0)
          OutStreamer->emitRawText(
              (Twine("\tadd\tr1, ") + Twine(Entry.Addend)).str());
        OutStreamer->emitRawText((Twine("\tmov\t") + Dest + ", r1").str());
      }
    }
    OutStreamer->emitRawText("\tmov\tr0, 0");
    OutStreamer->emitRawText("\tmov\tr1, 0");
    OutStreamer->emitRawText("\tcall\tmain");
    OutStreamer->emitRawText(
        "\tinline data modify storage std:vm ls0 set value {a: -1}");
    OutStreamer->emitRawText(
        "\tinline execute store result storage std:vm ls0.a int 1 run "
        "scoreboard players get rax vm_regs");
    OutStreamer->emitRawText(
        "\tinline function _ll_shared:z/_start_0 with storage std:vm ls0");
    OutStreamer->emitRawText("// -- End function");
    OutStreamer->emitRawText("");
    OutStreamer->emitRawText("export _ll_shared:z/_start_0:");
    OutStreamer->emitRawText("    inline $return $(a)");
    OutStreamer->emitRawText("\tret");
    OutStreamer->emitRawText("");
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

  McasmInlineAsmOptions Options;
  std::string Err;
  std::string AsmBody =
      parseMcasmInlineAsmOptions(StringRef(AsmStr), Options, Err);
  if (!Err.empty())
    report_fatal_error(Twine("mcasm inline asm wrapper: ") + Err);

  SmallVector<InlineAsmOperandDesc, 8> Descs;
  if (!buildInlineAsmOperandDescs(MI, Descs, Err))
    report_fatal_error(Twine("mcasm inline asm wrapper: ") + Err);

  // Assign a storage field name to every register-input operand up front so
  // the placeholder replacement can map `$N` -> `$(field)` consistently.
  DenseMap<unsigned, std::string> RegInputFieldByVal;
  SmallVector<unsigned, 8> AllRegInputVals;
  for (const InlineAsmOperandDesc &D : Descs) {
    if (!D.Flag.isRegUseKind())
      continue;
    const MachineOperand &MO = MI->getOperand(D.OpNo);
    if (!MO.isReg())
      report_fatal_error("mcasm inline asm wrapper: reg input is not a register");
    std::string Field =
        makeAlphaFieldName(static_cast<unsigned>(AllRegInputVals.size()));
    AllRegInputVals.push_back(D.ValIndex);
    RegInputFieldByVal.try_emplace(D.ValIndex, Field);
  }

  DenseSet<unsigned> ReferencedVals;
  std::string Replaced = replaceInlineAsmPlaceholders(
      StringRef(AsmBody), MI, Descs, RegInputFieldByVal, Err, &ReferencedVals);
  if (!Err.empty())
    report_fatal_error(Twine("mcasm inline asm wrapper: ") + Err);

  // Only register inputs actually referenced by the asm template need to be
  // marshalled through storage. Operands that are declared but never mentioned
  // in the template (e.g. `__asm("" : "+m"(a))`) require no argument passing,
  // and mem/imm operands are substituted directly into `Replaced`.
  SmallVector<unsigned, 8> RegInputVals;
  for (unsigned Val : AllRegInputVals)
    if (ReferencedVals.count(Val))
      RegInputVals.push_back(Val);

  // When no register input is referenced, the body carries no `$(field)` macro
  // parameters, so it can be emitted inline directly without a helper wrapper.
  if (RegInputVals.empty()) {
    OutStreamer->emitRawText("\t" + Replaced);
    return true;
  }

  std::string HelperBody = rewriteMcasmInlineHelperBody(Replaced);

  StringRef HelperKey = HelperBody;
  std::string Label;
  auto It = InlineAsmHelperIndexByKey.find(HelperKey);
  if (It != InlineAsmHelperIndexByKey.end()) {
    Label = InlineAsmHelpers[It->second].Label;
  } else {
    unsigned Seq = InlineAsmCounter[&F]++;
    std::string HelperName =
        rewriteMcasmSharedName((Twine("z/") + F.getName() + "_" + Twine(Seq)).str());
    Label = (Twine("_ll_shared:") + HelperName).str();
    InlineAsmHelperIndexByKey[HelperBody] = InlineAsmHelpers.size();
    InlineAsmHelpers.push_back({Label, HelperBody, HelperBody});
  }

  if (!Options.DirectArgs) {
    std::string Init = "inline data modify storage std:vm ls0 set value {";
    for (unsigned I = 0; I < RegInputVals.size(); ++I) {
      if (I)
        Init += ", ";
      Init += RegInputFieldByVal.lookup(RegInputVals[I]);
      Init += ": -1";
    }
    Init += "}";
    OutStreamer->emitRawText("\t" + Init);
  }

  for (unsigned Val : RegInputVals) {
    const InlineAsmOperandDesc &D = Descs[Val];
    const MachineOperand &MO = MI->getOperand(D.OpNo);
    std::string Reg = McasmInstPrinter::getRegisterName(MO.getReg());
    std::string Field = RegInputFieldByVal.lookup(Val);
    std::string Line =
        (Twine("inline execute store result storage std:vm ls0.") + Field +
         " int 1 run scoreboard players get " + Reg + " vm_regs")
            .str();
    OutStreamer->emitRawText("\t" + Line);
  }

  OutStreamer->emitRawText(
      (Twine("\tinline function ") + Label + " with storage std:vm ls0").str());
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
    // __declspec(dllexport): api _ll_shared:funcname:
    // NOTE: The symbol name already includes _ll_shared: prefix (added by getTargetSymbol)
    std::string Label = "api ";
    Label += FnSym->getName();
    Label += ":";
    MCASM_DEBUG_LOG("DEBUG:   Emitting api label: %s\n", Label.c_str());
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

  // MC scratch-string command carrier: emit the folded `set value` command text
  // verbatim. Its single ExternalSymbol operand holds one or more '\n'-separated
  // lines (see McasmTargetLowering::lowerINTRINSIC_VOID). Handled before the
  // generic pseudo/MCInst path, which cannot lower an arbitrary-text operand.
  if (MI->getOpcode() == Mcasm::STR_CMD) {
    StringRef Cmd = MI->getOperand(0).getSymbolName();
    SmallVector<StringRef, 2> Lines;
    Cmd.split(Lines, '\n');
    for (StringRef Line : Lines)
      OutStreamer->emitRawText(Twine("\t") + Line);
    return;
  }

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

  std::string VarName;
  if (shouldAnonymizeMcasmStaticData(GV))
    VarName = getSymbol(GV)->getName().str();
  else {
    VarName = GV->getName().str();
    if (!VarName.empty() && VarName[0] == '.')
      VarName.erase(0, 1);
    for (char &C : VarName) {
      if (C == '.')
        C = '_';
    }
  }

  Output += VarName;
  Output += " [";

  const Constant *C = GV->getInitializer();
  bool NeedComma = false;
  auto RuntimeEntries =
      appendMcasmInitializer(C, GV->getParent()->getDataLayout(), Output,
                             NeedComma, *this);

  Output += "]";
  OutStreamer->emitRawText(Output);
}

// Register the AsmPrinter
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMcasmAsmPrinter() {
  MCASM_DEBUG_LOG("DEBUG: LLVMInitializeMcasmAsmPrinter called\n");
  RegisterAsmPrinter<McasmAsmPrinter> X(getTheMcasm_32Target());
  MCASM_DEBUG_LOG("DEBUG: LLVMInitializeMcasmAsmPrinter completed\n");
}

