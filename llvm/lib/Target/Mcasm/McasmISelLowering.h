//===-- McasmISelLowering.h - Mcasm DAG Lowering Interface ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Mcasm uses to lower LLVM code into a
// selection DAG.
//
// MCASM NOTE: This is a minimal rewrite for the mcasm backend, which is a
// simplified 32-bit integer-only architecture. Most x86-specific optimizations
// and features have been removed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCASM_MCASMISELLOWERING_H
#define LLVM_LIB_TARGET_MCASM_MCASMISELLOWERING_H

#include "Mcasm.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class McasmSubtarget;
class McasmTargetMachine;

namespace McasmISD {
// Mcasm Specific DAG Nodes
enum NodeType : unsigned {
  // Start the numbering where the builtin ops leave off.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  /// Mcasm call instruction.
  CALL,

  /// Mcasm tail call (optimized as JMP).
  TC_RETURN,

  /// Return with a glue operand. Operand 0 is the chain operand.
  RET_GLUE,

  /// Mcasm compare instruction.
  CMP,

  /// Mcasm conditional branches. Operand 0 is the chain operand, operand 1
  /// is the block to branch if condition is true, operand 2 is the
  /// condition code, and operand 3 is the flag operand produced by a CMP
  /// instruction.
  BRCOND,

  /// Mcasm conditional branch with condition code. Operands: chain, lhs, rhs,
  /// condition code, destination block.
  /// This is used for mcasm's direct comparison conditional jumps.
  BR_CC,

  /// Wrapper for a global address.
  Wrapper,

  /// Wrapper for a global address - used for PIC.
  WrapperPIC,

  /// Wrapper for a function address - requires MOVD instead of MOV.
  FunctionWrapper,

  /// NEG_BOOL_MASK: Takes a boolean (0 or 1) i32, returns 0 or 0xFFFFFFFF.
  /// Used in branchless SELECT to prevent DAGCombine from re-recognizing the
  /// arithmetic as SELECT (which would cause an infinite re-legalization loop).
  NEG_BOOL_MASK,

  /// SETCC_DIA: (lhs, rhs, cc) → 0 or 1 via conditional-jump diamond.
  /// Lowered by EmitInstrWithCustomInserter to SETCC32rri pseudo.
  SETCC_DIA
};
} // namespace McasmISD

class McasmTargetLowering : public TargetLowering {
public:
  explicit McasmTargetLowering(const McasmTargetMachine &TM,
                               const McasmSubtarget &STI);

  /// getTargetNodeName - This method returns the name of a target specific
  /// DAG node.
  const char *getTargetNodeName(unsigned Opcode) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  MachineBasicBlock *EmitInstrWithCustomInserter(MachineInstr &MI,
                                                  MachineBasicBlock *MBB) const override;

  /// mcasm is word-addressed and has no narrow (byte/halfword) load
  /// instructions, so narrowing AND(load, mask) to a sub-word zextload is never
  /// profitable.  More importantly, it would reintroduce a re-legalization loop:
  ///   AND(wordload, mask) -> zextloadi8 (Custom) -> lowerByteSemanticLoad ->
  ///   AND(wordload, mask) -> ...
  /// Disabling load-width reduction breaks that cycle at its source.
  bool shouldReduceLoadWidth(
      SDNode *Load, ISD::LoadExtType ExtTy, EVT NewVT,
      std::optional<unsigned> ByteOffset = std::nullopt) const override {
    return false;
  }

  ConstraintType getConstraintType(StringRef Constraint) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  /// LowerFormalArguments - This hook must be implemented to lower the
  /// incoming (formal) arguments, described by the Ins array, into the
  /// specified DAG. The implementation should fill in the InVals array
  /// with legal-type argument values, and return the resulting token
  /// chain value.
  SDValue LowerFormalArguments(
      SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
      const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
      SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const override;

  /// LowerCall - This hook must be implemented to lower calls into the
  /// specified DAG.
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  /// LowerReturn - This hook must be implemented to lower outgoing
  /// return values, described by the Outs array, into the specified
  /// DAG. The implementation should return the resulting token chain
  /// value.
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  /// Decide whether a call marked as a tail call in the IR can actually be
  /// lowered as a mcasm tail call (JMP/JMPD to the callee). \p ArgLocs and
  /// \p CCInfo are the already-analyzed outgoing argument locations for the
  /// call. Returns false to fall back to a normal CALL+RET.
  bool IsEligibleForTailCallOptimization(
      TargetLowering::CallLoweringInfo &CLI,
      const SmallVectorImpl<CCValAssign> &ArgLocs, CCState &CCInfo) const;

  /// Prevent LLVM from transforming division to shift
  /// mcasm does not support shift operations
  bool shouldAvoidTransformToShift(EVT VT, unsigned Amount) const override {
    return true;
  }

  /// Force LLVM to use PTRADD for pointer arithmetic
  /// This allows us to convert byte offsets to mcasm's 4-byte address units
  bool shouldPreservePtrArith(const Function &F, EVT PtrVT) const override;

  /// mcasm assembler has no .rodata concept, so jump tables are unsupported.
  /// Force switch lowering to use branch trees/bit-tests instead of JTI data.
  bool areJTsAllowed(const Function *Fn) const override { return false; }

  /// mcasm assembly does not support symbol+offset operands for globals.
  /// Force global base materialization plus explicit arithmetic in registers.
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override {
    return false;
  }

  bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AS, Align Alignment,
                                      MachineMemOperand::Flags Flags,
                                      unsigned *Fast) const override;

private:
  const McasmSubtarget &Subtarget;

  SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSHLParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSRLParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSRAParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerCTTZ(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerCTLZ(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerCTPOP(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVACOPY(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVAARG(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerByteSemanticLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerByteSemanticStore(SDValue Op, SelectionDAG &DAG) const;
  // MC scratch-string build primitives (int_mcasm_str_begin/append): fold a
  // constant string operand to O(1) `set value` commands, else call the
  // runtime *_rt fallback. See TASK-const-string-fold.md.
  SDValue lowerINTRINSIC_VOID(SDValue Op, SelectionDAG &DAG) const;

  // i64 support - custom lowering to libcalls
  SDValue LowerI64LibCall(SDValue Op, SelectionDAG &DAG, RTLIB::Libcall LC) const;
  SDValue LowerI32BitLibCall(SDValue Op, SelectionDAG &DAG, StringRef Name,
                             ArrayRef<SDValue> Args) const;

  // Native lowering of i32 bit-ops with a constant operand (no libcall).
  // These emit native mul/div/add/sub sequences so that constant masks and
  // constant shift amounts do not incur a __bit_* libcall.  See
  // McasmISelLowering.cpp for the exact arithmetic identities.
  SDValue lowerI32Shift(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerI32AndOrXor(SDValue Op, SelectionDAG &DAG) const;
};

} // namespace llvm

#endif
