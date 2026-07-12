//===-- McasmInlineAsmElim.cpp - Drop unreferenced inline asm inputs -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pre-regalloc cleanup for the mcasm backend.
//
// When an inline asm operand is declared but the asm template never references
// it (e.g. `__asm volatile("" : : "r"(a))`), generic ISel still copies the
// value into a virtual register that the INLINEASM uses, which the register
// allocator materializes as a spurious `mov rN, <var>` (plus any spill).
//
// Because the mcasm asm template references operands explicitly via `$N`, we
// can tell exactly which register-input operands are unused and strip them
// from the INLINEASM before register allocation.  The value feeding a stripped
// operand then becomes dead and is removed here, eliminating the overhead.
//
// Both unreferenced register inputs and unreferenced memory operands are
// stripped: a memory operand keeps only its compile-time ordering dependency
// (preserved by the sideeffect INLINEASM itself), matching a bare `"memory"`
// clobber.  Outputs are never touched, and an INLINEASM that has any tied
// operand is skipped entirely -- removing a group shifts the operand indices
// that tie links depend on, which we do not rewrite.
//
//===----------------------------------------------------------------------===//

#include "Mcasm.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include <cctype>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "mcasm-inline-asm-elim"

namespace {

// Scan an mcasm inline asm template and record every `$N` operand reference,
// honoring the `$$`, `$(`, `$|`, `$)` escapes used by the backend.
static void collectReferencedVals(StringRef S, SmallVectorImpl<bool> &Ref) {
  for (size_t I = 0; I < S.size();) {
    if (S[I] != '$') {
      ++I;
      continue;
    }
    if (I + 1 >= S.size()) {
      ++I;
      continue;
    }
    char Next = S[I + 1];
    if (Next == '$' || Next == '(' || Next == '|' || Next == ')') {
      I += 2;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(Next))) {
      ++I;
      continue;
    }
    size_t J = I + 1;
    unsigned Val = 0;
    while (J < S.size() && std::isdigit(static_cast<unsigned char>(S[J]))) {
      Val = Val * 10 + unsigned(S[J] - '0');
      ++J;
    }
    if (Val >= Ref.size())
      Ref.resize(Val + 1, false);
    Ref[Val] = true;
    I = J;
  }
}

// Rewrite `$N` references so kept operands keep pointing at the right value
// after earlier groups have been removed. OldToNew maps an old value index to
// its new index.
static std::string renumberTemplate(StringRef S,
                                    ArrayRef<int> OldToNew) {
  std::string Out;
  for (size_t I = 0; I < S.size();) {
    if (S[I] != '$') {
      Out.push_back(S[I]);
      ++I;
      continue;
    }
    if (I + 1 >= S.size()) {
      Out.push_back('$');
      ++I;
      continue;
    }
    char Next = S[I + 1];
    if (Next == '$' || Next == '(' || Next == '|' || Next == ')') {
      Out.push_back('$');
      Out.push_back(Next);
      I += 2;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(Next))) {
      Out.push_back('$');
      ++I;
      continue;
    }
    size_t J = I + 1;
    unsigned Val = 0;
    while (J < S.size() && std::isdigit(static_cast<unsigned char>(S[J]))) {
      Val = Val * 10 + unsigned(S[J] - '0');
      ++J;
    }
    int NewVal = (Val < OldToNew.size()) ? OldToNew[Val] : int(Val);
    Out.push_back('$');
    Out += std::to_string(NewVal < 0 ? int(Val) : NewVal);
    I = J;
  }
  return Out;
}

class McasmInlineAsmElim final : public MachineFunctionPass {
public:
  static char ID;
  McasmInlineAsmElim() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Mcasm unreferenced inline asm operand elimination";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool processInlineAsm(MachineInstr &MI,
                        SmallVectorImpl<Register> &DeadCandidates);
};

char McasmInlineAsmElim::ID = 0;

bool McasmInlineAsmElim::processInlineAsm(
    MachineInstr &MI, SmallVectorImpl<Register> &DeadCandidates) {
  const MachineOperand &StrMO = MI.getOperand(InlineAsm::MIOp_AsmString);
  if (!StrMO.isSymbol())
    return false;
  StringRef AsmStr(StrMO.getSymbolName());

  SmallVector<bool, 8> Referenced;
  collectReferencedVals(AsmStr, Referenced);

  // Walk operand groups. For each, remember its value index, the MI operand
  // index of its flag word, and how many operands it spans.
  struct Group {
    unsigned ValIdx;
    unsigned FlagOpIdx;
    unsigned NumOperands; // flag + registers
    bool Removable;
  };
  SmallVector<Group, 8> Groups;
  unsigned ValIdx = 0;
  unsigned NumOps = MI.getNumOperands();
  for (unsigned I = InlineAsm::MIOp_FirstOperand; I < NumOps;) {
    const MachineOperand &FlagMO = MI.getOperand(I);
    if (!FlagMO.isImm())
      break; // trailing metadata / other operands
    InlineAsm::Flag F(FlagMO.getImm());
    unsigned NumRegs = F.getNumOperandRegisters();
    unsigned TiedTo;
    bool Tied = F.isUseOperandTiedToDef(TiedTo);
    // Tie links reference operand positions we would invalidate by removing a
    // group; we do not rewrite them, so leave such INLINEASMs untouched.
    if (Tied)
      return false;
    bool Ref = ValIdx < Referenced.size() && Referenced[ValIdx];
    // A register input the template never uses only exists to feed a value the
    // asm ignores -> drop it (and the value materialization). A memory operand
    // the template never uses only needs its compile-time ordering dependency,
    // which the sideeffect INLINEASM itself preserves, so drop its address
    // materialization too (matching a bare `"memory"` clobber).
    bool Removable = false;
    if (!Ref) {
      if (F.isRegUseKind())
        Removable = NumRegs == 1 && MI.getOperand(I + 1).isReg() &&
                    MI.getOperand(I + 1).getReg().isVirtual();
      else if (F.isMemKind())
        Removable = true;
    }
    Groups.push_back({ValIdx, I, NumRegs + 1, Removable});
    ++ValIdx;
    I += NumRegs + 1;
  }

  bool AnyRemovable = false;
  for (const Group &G : Groups)
    AnyRemovable |= G.Removable;
  if (!AnyRemovable)
    return false;

  // Build old->new value index remapping for the survivors.
  SmallVector<int, 8> OldToNew(Groups.size(), -1);
  unsigned NewIdx = 0;
  for (const Group &G : Groups) {
    if (G.Removable)
      continue;
    OldToNew[G.ValIdx] = int(NewIdx++);
  }

  // Renumber the template and install the new string.
  std::string NewStr = renumberTemplate(AsmStr, OldToNew);
  const char *NewSym = MI.getMF()->createExternalSymbolName(NewStr);

  // Collect operand index ranges (high to low) to erase, and the virtual
  // registers whose only remaining use we are dropping.
  SmallVector<unsigned, 8> OpsToRemove;
  for (const Group &G : Groups) {
    if (!G.Removable)
      continue;
    // The flag word is at FlagOpIdx; operands the group carries (a register
    // input, or the address components of a memory operand) follow it. Any
    // virtual register among them may become dead once the group is gone.
    for (unsigned K = 0; K < G.NumOperands; ++K) {
      unsigned Idx = G.FlagOpIdx + K;
      const MachineOperand &MO = MI.getOperand(Idx);
      if (MO.isReg() && MO.getReg().isVirtual())
        DeadCandidates.push_back(MO.getReg());
      OpsToRemove.push_back(Idx);
    }
  }

  MI.getOperand(InlineAsm::MIOp_AsmString).ChangeToES(NewSym);

  llvm::sort(OpsToRemove);
  for (unsigned I = OpsToRemove.size(); I-- > 0;)
    MI.removeOperand(OpsToRemove[I]);

  return true;
}

bool McasmInlineAsmElim::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  if (!MRI.isSSA())
    return false;

  bool Changed = false;
  SmallVector<Register, 16> DeadCandidates;

  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      // Restrict to plain INLINEASM. INLINEASM_BR (asm goto) carries trailing
      // MBB target operands and is a terminator; leave it untouched.
      if (MI.getOpcode() == TargetOpcode::INLINEASM)
        Changed |= processInlineAsm(MI, DeadCandidates);

  // Delete instructions that only defined values we just stopped using. Follow
  // simple copy chains so `%1 = COPY %0; INLINEASM ..., %1` cleans up both.
  SmallVector<Register, 16> Worklist(DeadCandidates.begin(),
                                     DeadCandidates.end());
  while (!Worklist.empty()) {
    Register Reg = Worklist.pop_back_val();
    if (!Reg.isVirtual() || !MRI.use_nodbg_empty(Reg))
      continue;
    MachineInstr *Def = MRI.getUniqueVRegDef(Reg);
    if (!Def || Def->isInlineAsm() || Def->mayStore() ||
        Def->hasUnmodeledSideEffects())
      continue;
    // Only remove instructions whose sole purpose was defining Reg.
    bool OnlyDefIsReg = true;
    for (const MachineOperand &MO : Def->operands())
      if (MO.isReg() && MO.isDef() && MO.getReg() != Reg)
        OnlyDefIsReg = false;
    if (!OnlyDefIsReg)
      continue;
    for (const MachineOperand &MO : Def->operands())
      if (MO.isReg() && MO.isUse() && MO.getReg().isVirtual())
        Worklist.push_back(MO.getReg());
    // Any remaining uses are debug values (use_nodbg_empty held); drop them to
    // undef so we don't leave a DBG_VALUE pointing at an erased def.
    MRI.markUsesInDebugValueAsUndef(Reg);
    Def->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

} // namespace

INITIALIZE_PASS(McasmInlineAsmElim, DEBUG_TYPE,
                "Mcasm unreferenced inline asm operand elimination", false,
                false)

FunctionPass *llvm::createMcasmInlineAsmElimPass() {
  return new McasmInlineAsmElim();
}
