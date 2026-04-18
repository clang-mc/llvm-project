//===-- McasmFrameLowering.cpp - Mcasm Frame Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mcasm implementation of TargetFrameLowering.
//
// MCASM NOTE: Minimal stub implementation for mcasm backend.
//
//===----------------------------------------------------------------------===//

#include "McasmFrameLowering.h"
#include "TargetInfo/McasmDebug.h"
#include "McasmSubtarget.h"
#include "McasmInstrInfo.h"
#include "McasmRegisterInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"

#define GET_REGINFO_ENUM
#include "McasmGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "McasmGenInstrInfo.inc"

using namespace llvm;

static int64_t normalizeFrameOffset(int64_t Offset) {
  if (Offset > std::numeric_limits<int32_t>::max())
    return static_cast<int32_t>(Offset);
  return Offset;
}

static uint64_t getMcasmFrameSize(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t ExtraFixedArea = 0;
  for (int I = MFI.getObjectIndexBegin(); I != 0; ++I) {
    if (!MFI.isFixedObjectIndex(I))
      continue;
    int64_t Offset = normalizeFrameOffset(MFI.getObjectOffset(I));
    ExtraFixedArea = std::max<int64_t>(ExtraFixedArea, -Offset);
  }
  return MFI.getStackSize() + ExtraFixedArea;
}

McasmFrameLowering::McasmFrameLowering(const McasmSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4), 0),
      Subtarget(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()) {}

void McasmFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MCASM_DEBUG_LOG("DEBUG emitPrologue: function=%s\n", MF.getName().str().c_str());
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = getMcasmFrameSize(MF);
  if (StackSize == 0) {
    MCASM_DEBUG_LOG("DEBUG emitPrologue: empty frame\n");
    return;
  }

  MachineBasicBlock::iterator InsertPt = MBB.begin();
  DebugLoc DL;
  if (InsertPt != MBB.end())
    DL = InsertPt->getDebugLoc();

  BuildMI(MBB, InsertPt, DL, TII.get(Mcasm::SUB32ri), Mcasm::rsp)
      .addReg(Mcasm::rsp)
      .addImm(StackSize)
      .setMIFlag(MachineInstr::FrameSetup);
  MCASM_DEBUG_LOG("DEBUG emitPrologue: completed\n");
}

void McasmFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MCASM_DEBUG_LOG("DEBUG emitEpilogue: function=%s\n", MF.getName().str().c_str());
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = getMcasmFrameSize(MF);
  if (StackSize == 0) {
    MCASM_DEBUG_LOG("DEBUG emitEpilogue: empty frame\n");
    return;
  }

  MachineBasicBlock::iterator InsertPt = MBB.getFirstTerminator();
  DebugLoc DL;
  if (InsertPt != MBB.end())
    DL = InsertPt->getDebugLoc();

  BuildMI(MBB, InsertPt, DL, TII.get(Mcasm::ADD32ri), Mcasm::rsp)
      .addReg(Mcasm::rsp)
      .addImm(StackSize)
      .setMIFlag(MachineInstr::FrameDestroy);
  MCASM_DEBUG_LOG("DEBUG emitEpilogue: completed\n");
}

bool McasmFrameLowering::needsFrameIndexResolution(const MachineFunction &MF) const {
  return true;  // We need frame index resolution for stack slots
}

bool McasmFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return false;  // Mcasm doesn't use frame pointer for now
}

StackOffset McasmFrameLowering::getFrameIndexReference(
    const MachineFunction &MF, int FI, Register &FrameReg) const {
  FrameReg = Mcasm::rsp;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t ByteOffset =
      normalizeFrameOffset(MFI.getObjectOffset(FI)) - getOffsetOfLocalArea();
  return StackOffset::getFixed(ByteOffset + getMcasmFrameSize(MF));
}

MachineBasicBlock::iterator McasmFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  if (hasReservedCallFrame(MF))
    return MBB.erase(MI);

  int64_t Amount = MI->getOperand(0).getImm();
  if (Amount != 0) {
    unsigned Opc = MI->getOpcode() == Mcasm::ADJCALLSTACKDOWN32
                       ? Mcasm::SUB32ri
                       : Mcasm::ADD32ri;
    MachineInstr::MIFlag Flag =
        MI->getOpcode() == Mcasm::ADJCALLSTACKDOWN32
            ? MachineInstr::FrameSetup
            : MachineInstr::FrameDestroy;
    BuildMI(MBB, MI, MI->getDebugLoc(), TII.get(Opc), Mcasm::rsp)
        .addReg(Mcasm::rsp)
        .addImm(Amount)
        .setMIFlag(Flag);
  }
  return MBB.erase(MI);
}

void McasmFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  // TODO: Implement when we have callee-saved register support
  // For now, use parent class default implementation
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
}

bool McasmFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // TODO: Implement when we have MOV and stack instructions
  // For now, return false (use default spilling)
  return false;
}

bool McasmFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI,
    const TargetRegisterInfo *TRI) const {
  // TODO: Implement when we have MOV and stack instructions
  // For now, return false (use default restoring)
  return false;
}

bool McasmFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // Return true if we always reserve space for the call frame
  return !MF.getFrameInfo().hasVarSizedObjects();
}

bool McasmFrameLowering::canSimplifyCallFramePseudos(
    const MachineFunction &MF) const {
  // We can simplify call frame pseudos if we have a reserved call frame
  return hasReservedCallFrame(MF);
}

