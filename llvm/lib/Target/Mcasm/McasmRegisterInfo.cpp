//===-- McasmRegisterInfo.cpp - Mcasm Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mcasm implementation of the TargetRegisterInfo class.
//
// MCASM NOTE: This is a minimal rewrite for the mcasm backend, which is a
// simplified 32-bit integer-only architecture. The key responsibility here
// is eliminateFrameIndex, which converts BYTE offsets to mcasm UNITS.
//
//===----------------------------------------------------------------------===//

#include "McasmRegisterInfo.h"
#include "Mcasm.h"
#include "McasmFrameLowering.h"
#include "McasmInstrInfo.h"
#include "McasmSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_ENUM
#include "McasmGenRegisterInfo.inc"

#define GET_REGINFO_TARGET_DESC
#include "McasmGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "McasmGenInstrInfo.inc"

McasmRegisterInfo::McasmRegisterInfo(const Triple &TT)
    : McasmGenRegisterInfo(Mcasm::rax) {}

const TargetRegisterClass *
McasmRegisterInfo::getPointerRegClass(unsigned Kind) const {
  // mcasm uses 32-bit pointers (GR32)
  return &Mcasm::GR32RegClass;
}

const MCPhysReg *
McasmRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  // Callee-saved registers: x0-x15 (defined in McasmCallingConv.td)
  // This is handled by CSR_Mcasm_32 in TableGen
  return CSR_Mcasm_32_SaveList;
}

BitVector
McasmRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // Reserve stack pointer
  Reserved.set(Mcasm::rsp);

  // Reserve compiler-reserved registers s0-s4
  Reserved.set(Mcasm::s0);
  Reserved.set(Mcasm::s1);
  Reserved.set(Mcasm::s2);
  Reserved.set(Mcasm::s3);
  Reserved.set(Mcasm::s4);

  return Reserved;
}

bool McasmRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  fprintf(stderr, "DEBUG eliminateFrameIndex: called, FIOperandNum=%u\n", FIOperandNum);
  fflush(stderr);

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const McasmFrameLowering *TFI = static_cast<const McasmFrameLowering *>(
      MF.getSubtarget().getFrameLowering());

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  unsigned NumOperands = MI.getNumOperands();
  fprintf(stderr, "DEBUG eliminateFrameIndex: FrameIndex=%d, Opcode=%u, NumOperands=%u, FIOperandNum=%u\n",
          FrameIndex, MI.getOpcode(), NumOperands, FIOperandNum);
  fprintf(stderr, "DEBUG eliminateFrameIndex: Instruction: ");
  MI.print(llvm::errs());
  fprintf(stderr, "\n");
  fflush(stderr);

  // Get the frame index reference (in mcasm units)
  Register FrameReg;
  StackOffset Offset = TFI->getFrameIndexReference(MF, FrameIndex, FrameReg);

  // CRITICAL: Offset is already in mcasm units from getFrameIndexReference
  // mcasm uses 4-byte units: [rsp+1] means 4 bytes offset
  int64_t McasmOffset = Offset.getFixed();

  // Check if this is an ALU instruction (like ADD) with FrameIndex as source
  // Memory operands have format: [BaseReg + Scale*IndexReg + Disp + Segment]
  // This requires 5 operands starting from FIOperandNum
  // If we don't have 5 operands, it's likely an ALU instruction
  if (FIOperandNum + 4 >= NumOperands) {
    // Not a memory operand format (doesn't have 5 operands after FI)
    // This is likely an ALU instruction like ADD32ri with FrameIndex
    // For computing addresses like &stack_var + offset
    fprintf(stderr, "DEBUG eliminateFrameIndex: Special case - ALU with FrameIndex (address calculation)\n");
    fflush(stderr);

    // For ADD32ri with FrameIndex, we need to:
    // 1. Insert MOV dst, FrameReg
    // 2. Then ADD dst, (McasmOffset + imm)
    // But since two-address form requires dst == src1, we can:
    // - Replace FrameIndex with the destination register
    // - Insert a MOV before this instruction to initialize dst = FrameReg

    MachineBasicBlock &MBB = *MI.getParent();
    const McasmInstrInfo *TII = MF.getSubtarget<McasmSubtarget>().getInstrInfo();
    DebugLoc DL = MI.getDebugLoc();

    // Get destination register from the ADD instruction
    Register DstReg = MI.getOperand(0).getReg();

    // Insert: MOV DstReg, FrameReg
    BuildMI(MBB, II, DL, TII->get(Mcasm::MOV32rr), DstReg)
        .addReg(FrameReg);
    fprintf(stderr, "DEBUG eliminateFrameIndex: Inserted MOV %u, %u\n",
            DstReg.id(), FrameReg.id());

    // Now replace FrameIndex with DstReg (for two-address form)
    MI.getOperand(FIOperandNum).ChangeToRegister(DstReg, false);

    // Adjust the immediate by McasmOffset
    if (FIOperandNum + 1 < NumOperands && MI.getOperand(FIOperandNum + 1).isImm()) {
      int64_t OldImm = MI.getOperand(FIOperandNum + 1).getImm();
      MI.getOperand(FIOperandNum + 1).setImm(OldImm + McasmOffset);
      fprintf(stderr, "DEBUG eliminateFrameIndex: Adjusted immediate from %lld to %lld\n",
              (long long)OldImm, (long long)(OldImm + McasmOffset));
    }

    fprintf(stderr, "DEBUG eliminateFrameIndex: ALU case completed\n");
    fflush(stderr);
    return false;
  }

  // Normal memory operand case
  // Replace FrameIndex with FrameReg + McasmOffset
  // The instruction format is: [BaseReg + Scale*IndexReg + Disp + Segment]
  // For mcasm: [rsp + 0*0 + McasmOffset + 0]
  fprintf(stderr, "DEBUG eliminateFrameIndex: Normal memory operand case\n");
  fprintf(stderr, "DEBUG eliminateFrameIndex: About to replace with FrameReg=%u, McasmOffset=%lld\n",
          FrameReg.id(), (long long)McasmOffset);
  fprintf(stderr, "DEBUG eliminateFrameIndex: Operand types: FI=%d, FI+1=%d, FI+2=%d, FI+3=%d, FI+4=%d\n",
          MI.getOperand(FIOperandNum).getType(),
          MI.getOperand(FIOperandNum + 1).getType(),
          MI.getOperand(FIOperandNum + 2).getType(),
          MI.getOperand(FIOperandNum + 3).getType(),
          MI.getOperand(FIOperandNum + 4).getType());
  fflush(stderr);

  MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
  fprintf(stderr, "DEBUG eliminateFrameIndex: Changed operand 0\n");
  fflush(stderr);

  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(1);           // Scale = 1
  fprintf(stderr, "DEBUG eliminateFrameIndex: Changed operand 1\n");
  fflush(stderr);

  MI.getOperand(FIOperandNum + 2).ChangeToRegister(0, false);     // IndexReg = 0
  fprintf(stderr, "DEBUG eliminateFrameIndex: Changed operand 2\n");
  fflush(stderr);

  MI.getOperand(FIOperandNum + 3).ChangeToImmediate(McasmOffset); // Displacement
  fprintf(stderr, "DEBUG eliminateFrameIndex: Changed operand 3\n");
  fflush(stderr);

  MI.getOperand(FIOperandNum + 4).ChangeToRegister(0, false);     // Segment = 0
  fprintf(stderr, "DEBUG eliminateFrameIndex: Changed operand 4\n");
  fflush(stderr);

  fprintf(stderr, "DEBUG eliminateFrameIndex: Normal case completed\n");
  fflush(stderr);
  return false;
}

Register
McasmRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // mcasm always uses rsp (no frame pointer)
  return Mcasm::rsp;
}
