//===-- McasmISelLowering.cpp - Mcasm DAG Lowering Implementation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the McasmTargetLowering class.
//
// MCASM NOTE: Minimal stub implementation for mcasm backend.
//
//===----------------------------------------------------------------------===//

#include "McasmISelLowering.h"
#include "Mcasm.h"
#include "McasmCallingConv.h"
#include "McasmSubtarget.h"
#include "McasmTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"

using namespace llvm;

#define DEBUG_TYPE "mcasm-lower"

// Include TableGen-generated files
#define GET_REGINFO_ENUM
#include "McasmGenRegisterInfo.inc"

// NOTE: We use C++ custom calling convention instead of TableGen
// #include "McasmGenCallingConv.inc"

McasmTargetLowering::McasmTargetLowering(const McasmTargetMachine &TM,
                                         const McasmSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {

  // Set up the register classes
  addRegisterClass(MVT::i32, &Mcasm::GR32RegClass);

  // Floating point - mcasm does not have FP hardware
  // f32 and f64 are NOT registered - LLVM will use TypeSoftenFloat to convert:
  //   f32 → i32 (bit representation), f64 → i64 → two i32 halves
  // All FP operations become software library calls (__addsf3, __adddf3, etc.)

  // i64 type handling - mcasm is 32-bit, so i64 operations need special handling
  // Tell LLVM that i64 is not a native type and should be promoted/expanded
  setOperationPromotedToType(ISD::LOAD, MVT::i1, MVT::i32);
  setOperationPromotedToType(ISD::STORE, MVT::i1, MVT::i32);

  // Compute derived properties
  computeRegisterProperties(STI.getRegisterInfo());

  // CRITICAL: Explicitly mark i64 as needing integer promotion/expansion
  // This tells LLVM's type legalizer how to handle i64 on our 32-bit target
  setOperationAction(ISD::ATOMIC_LOAD, MVT::i64, Expand);
  setOperationAction(ISD::ATOMIC_STORE, MVT::i64, Expand);

  // Pointer arithmetic - PTRADD must be customized for mcasm's 4-byte addressing
  setOperationAction(ISD::PTRADD, MVT::i32, Custom);

  // CRITICAL: mcasm only supports 32-bit (4-byte) memory operations
  // In mcasm, char, short, int all are 32-bit (defined in Clang target)
  // So Clang will generate i32 operations directly, no need for type promotion here

  // Stack configuration
  setStackPointerRegisterToSaveRestore(Mcasm::rsp);
  setMinFunctionAlignment(Align(4));
  setMinStackArgumentAlignment(Align(4));

  // Basic arithmetic operations - legal by default for i32
  setOperationAction(ISD::ADD, MVT::i32, Legal);
  setOperationAction(ISD::SUB, MVT::i32, Legal);
  setOperationAction(ISD::MUL, MVT::i32, Legal);

  // High multiplication results - not supported, forces LLVM to use native DIV
  setOperationAction(ISD::MULHS, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);

  // Division - mcasm has native signed division
  setOperationAction(ISD::SDIV, MVT::i32, Legal);

  // Unsigned division and remainder - expand to library calls
  setOperationAction(ISD::UDIV, MVT::i32, Expand);
  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::UREM, MVT::i32, Expand);

  // Floating point operations - use soft float library calls via Expand
  // LLVM will automatically generate library calls for soft float
  // f32 operations
  setOperationAction(ISD::FADD, MVT::f32, Expand);
  setOperationAction(ISD::FSUB, MVT::f32, Expand);
  setOperationAction(ISD::FMUL, MVT::f32, Expand);
  setOperationAction(ISD::FDIV, MVT::f32, Expand);
  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FNEG, MVT::f32, Expand);
  setOperationAction(ISD::FABS, MVT::f32, Expand);
  setOperationAction(ISD::FSQRT, MVT::f32, Expand);
  setOperationAction(ISD::FSIN, MVT::f32, Expand);
  setOperationAction(ISD::FCOS, MVT::f32, Expand);
  setOperationAction(ISD::FPOW, MVT::f32, Expand);
  setOperationAction(ISD::FLOG, MVT::f32, Expand);
  setOperationAction(ISD::FLOG2, MVT::f32, Expand);
  setOperationAction(ISD::FLOG10, MVT::f32, Expand);
  setOperationAction(ISD::FEXP, MVT::f32, Expand);
  setOperationAction(ISD::FEXP2, MVT::f32, Expand);
  setOperationAction(ISD::FCEIL, MVT::f32, Expand);
  setOperationAction(ISD::FFLOOR, MVT::f32, Expand);
  setOperationAction(ISD::FTRUNC, MVT::f32, Expand);
  setOperationAction(ISD::FRINT, MVT::f32, Expand);
  setOperationAction(ISD::FNEARBYINT, MVT::f32, Expand);
  setOperationAction(ISD::FROUND, MVT::f32, Expand);
  setOperationAction(ISD::FMA, MVT::f32, Expand);
  setOperationAction(ISD::FMINNUM, MVT::f32, Expand);
  setOperationAction(ISD::FMAXNUM, MVT::f32, Expand);

  // f64 operations
  setOperationAction(ISD::FADD, MVT::f64, Expand);
  setOperationAction(ISD::FSUB, MVT::f64, Expand);
  setOperationAction(ISD::FMUL, MVT::f64, Expand);
  setOperationAction(ISD::FDIV, MVT::f64, Expand);
  setOperationAction(ISD::FREM, MVT::f64, Expand);
  setOperationAction(ISD::FNEG, MVT::f64, Expand);
  setOperationAction(ISD::FABS, MVT::f64, Expand);
  setOperationAction(ISD::FSQRT, MVT::f64, Expand);
  setOperationAction(ISD::FSIN, MVT::f64, Expand);
  setOperationAction(ISD::FCOS, MVT::f64, Expand);
  setOperationAction(ISD::FPOW, MVT::f64, Expand);
  setOperationAction(ISD::FLOG, MVT::f64, Expand);
  setOperationAction(ISD::FLOG2, MVT::f64, Expand);
  setOperationAction(ISD::FLOG10, MVT::f64, Expand);
  setOperationAction(ISD::FEXP, MVT::f64, Expand);
  setOperationAction(ISD::FEXP2, MVT::f64, Expand);
  setOperationAction(ISD::FCEIL, MVT::f64, Expand);
  setOperationAction(ISD::FFLOOR, MVT::f64, Expand);
  setOperationAction(ISD::FTRUNC, MVT::f64, Expand);
  setOperationAction(ISD::FRINT, MVT::f64, Expand);
  setOperationAction(ISD::FNEARBYINT, MVT::f64, Expand);
  setOperationAction(ISD::FROUND, MVT::f64, Expand);
  setOperationAction(ISD::FMA, MVT::f64, Expand);
  setOperationAction(ISD::FMINNUM, MVT::f64, Expand);
  setOperationAction(ISD::FMAXNUM, MVT::f64, Expand);

  // Floating point comparisons
  setOperationAction(ISD::SETCC, MVT::f32, Expand);
  setOperationAction(ISD::SETCC, MVT::f64, Expand);

  // Floating point conversions
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Expand);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Expand);
  setOperationAction(ISD::SINT_TO_FP, MVT::f32, Expand);
  setOperationAction(ISD::SINT_TO_FP, MVT::f64, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::f32, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::f64, Expand);
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Expand);
  setOperationAction(ISD::FP_ROUND, MVT::f32, Expand);

  // ========================================================================
  // i64 support - mcasm is 32-bit, so i64 is split into two i32 values
  // ========================================================================
  // CRITICAL: mcasm has NO carry flag, so ADDC/ADDE/SUBC/SUBE don't exist
  // Mark them as Expand to force library calls
  setOperationAction(ISD::ADDC, MVT::i32, Expand);
  setOperationAction(ISD::ADDE, MVT::i32, Expand);
  setOperationAction(ISD::SUBC, MVT::i32, Expand);
  setOperationAction(ISD::SUBE, MVT::i32, Expand);

  // i64 arithmetic operations - let LLVM expand to library calls
  // Without ADDC/ADDE, LLVM will generate library calls for these
  setOperationAction(ISD::ADD, MVT::i64, Expand);
  setOperationAction(ISD::SUB, MVT::i64, Expand);
  setOperationAction(ISD::MUL, MVT::i64, Expand);
  setOperationAction(ISD::SDIV, MVT::i64, Expand);
  setOperationAction(ISD::UDIV, MVT::i64, Expand);
  setOperationAction(ISD::SREM, MVT::i64, Expand);
  setOperationAction(ISD::UREM, MVT::i64, Expand);
  setOperationAction(ISD::SHL, MVT::i64, Expand);
  setOperationAction(ISD::SRL, MVT::i64, Expand);
  setOperationAction(ISD::SRA, MVT::i64, Expand);

  // Disable i64 optimizations to prevent crashes
  setMaxAtomicSizeInBitsSupported(32);
  MaxStoresPerMemset = 16;
  MaxStoresPerMemcpy = 16;
  MaxStoresPerMemmove = 16;

  // i64 calling convention handled in CC_Mcasm (see McasmCallingConv.cpp)

  // Shifts - now supported with native SHL/SHR/SAR instructions
  setOperationAction(ISD::SHL, MVT::i32, Legal);
  setOperationAction(ISD::SRA, MVT::i32, Legal);
  setOperationAction(ISD::SRL, MVT::i32, Legal);
  // No rotate instructions in mcasm
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);

  // i64 shift-parts - generated when LLVM type-splits i64 shifts into two i32
  // operations.  We lower these to branchless arithmetic sequences.
  setOperationAction(ISD::SHL_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Custom);

  // Logical operations - now supported with native AND/OR/XOR/NOT instructions
  setOperationAction(ISD::AND, MVT::i32, Legal);
  setOperationAction(ISD::OR, MVT::i32, Legal);
  setOperationAction(ISD::XOR, MVT::i32, Legal);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);
  setOperationAction(ISD::CTLZ, MVT::i32, Expand);
  setOperationAction(ISD::CTTZ, MVT::i32, Expand);

  // Comparison operations
  // mcasm has no SETcc instruction. SETCC as a value is lowered to branchless
  // arithmetic (see lowerSETCC). This is Custom to avoid the infinite-loop that
  // would otherwise occur: SETCC(Expand)→SELECT_CC→SETCC+SELECT→SELECT_CC→...
  setOperationAction(ISD::SETCC, MVT::i32, Custom);

  // Control flow operations
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  // BR_CC must be Custom so we can handle unsigned comparisons via MSB flip.
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  // SELECT_CC stays Expand; with SETCC+SELECT both Custom, there is no cycle.
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  // SELECT as a value is lowered to branchless arithmetic (lowerSELECT).
  setOperationAction(ISD::SELECT, MVT::i32, Custom);
  // BRCOND handles boolean-valued conditions (compare to 0 and branch).
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);

  // Global addresses and constants
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i32, Custom);
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);

  // Stack operations
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  // Expand unsupported operations
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // Vector operations - mcasm does not support SIMD/vector operations
  // Expand all vector operations to scalar operations
  setOperationAction(ISD::BUILD_VECTOR, MVT::Other, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::Other, Expand);
  setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::Other, Expand);
  setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::Other, Expand);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::Other, Expand);
  setOperationAction(ISD::CONCAT_VECTORS, MVT::Other, Expand);

  // Explicitly mark that we don't support any vector types
  for (MVT VT : MVT::fixedlen_vector_valuetypes()) {
    setOperationAction(ISD::BUILD_VECTOR, VT, Expand);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Expand);
    setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Expand);
    setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Expand);
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Expand);
    setOperationAction(ISD::CONCAT_VECTORS, VT, Expand);
    setOperationAction(ISD::EXTRACT_SUBVECTOR, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);

    // Arithmetic operations on vectors
    setOperationAction(ISD::ADD, VT, Expand);
    setOperationAction(ISD::SUB, VT, Expand);
    setOperationAction(ISD::MUL, VT, Expand);
    setOperationAction(ISD::SDIV, VT, Expand);
    setOperationAction(ISD::UDIV, VT, Expand);
    setOperationAction(ISD::SREM, VT, Expand);
    setOperationAction(ISD::UREM, VT, Expand);

    // Bitwise operations on vectors
    setOperationAction(ISD::AND, VT, Expand);
    setOperationAction(ISD::OR, VT, Expand);
    setOperationAction(ISD::XOR, VT, Expand);

    // Shift operations on vectors
    setOperationAction(ISD::SHL, VT, Expand);
    setOperationAction(ISD::SRA, VT, Expand);
    setOperationAction(ISD::SRL, VT, Expand);

    // Load/store operations on vectors
    setOperationAction(ISD::LOAD, VT, Expand);
    setOperationAction(ISD::STORE, VT, Expand);
  }

  // Disable indexed addressing modes for all value types
  // This forces LLVM to expand GEP into simple ADD operations
  // which avoids issues with mcasm's 4-byte address units
  for (MVT VT : MVT::all_valuetypes()) {
    setIndexedLoadAction(ISD::PRE_INC, VT, Expand);
    setIndexedLoadAction(ISD::PRE_DEC, VT, Expand);
    setIndexedLoadAction(ISD::POST_INC, VT, Expand);
    setIndexedLoadAction(ISD::POST_DEC, VT, Expand);
    setIndexedStoreAction(ISD::PRE_INC, VT, Expand);
    setIndexedStoreAction(ISD::PRE_DEC, VT, Expand);
    setIndexedStoreAction(ISD::POST_INC, VT, Expand);
    setIndexedStoreAction(ISD::POST_DEC, VT, Expand);
  }
}

bool McasmTargetLowering::shouldPreservePtrArith(const Function &F, EVT PtrVT) const {
  return true;
}

SDValue McasmTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Analyze incoming arguments according to calling convention
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Mcasm);

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    if (VA.isRegLoc()) {
      // Argument passed in register
      const TargetRegisterClass *RC = &Mcasm::GR32RegClass;
      unsigned VReg = MF.getRegInfo().createVirtualRegister(RC);
      MF.getRegInfo().addLiveIn(VA.getLocReg(), VReg);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i32);
      // f32 parameters are passed as i32 bits (BCvt), restore to f32
      if (VA.getLocInfo() == CCValAssign::BCvt)
        ArgValue = DAG.getNode(ISD::BITCAST, dl, VA.getValVT(), ArgValue);
      InVals.push_back(ArgValue);
    } else {
      // Argument passed on stack
      assert(VA.isMemLoc() && "Expected memory location");
      int FI = MFI.CreateFixedObject(4, VA.getLocMemOffset(), true);
      SDValue FINode = DAG.getFrameIndex(FI, MVT::i32);
      SDValue Load = DAG.getLoad(MVT::i32, dl, Chain, FINode,
                                  MachinePointerInfo::getFixedStack(MF, FI));
      InVals.push_back(Load);
    }
  }

  return Chain;
}

SDValue McasmTargetLowering::LowerCall(
    CallLoweringInfo &CLI, SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  bool &IsTailCall = CLI.IsTailCall;
  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze outgoing arguments
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_Mcasm);

  // Get the size of the outgoing arguments stack space
  unsigned NumBytes = CCInfo.getStackSize();

  // Check if tail call is possible
  // For now, only allow tail calls with no stack arguments
  if (IsTailCall && NumBytes > 0) {
    IsTailCall = false;  // Cannot tail call with stack arguments
  }

  // Adjust stack if needed
  if (NumBytes > 0) {
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  }

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // Walk the register/memloc assignments
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];

    // Convert FrameIndex to actual address expression
    // When passing the address of a stack variable (e.g., &array), the IR contains
    // a FrameIndex node. We need to convert this to an actual address (rsp + offset).
    if (Arg.getOpcode() == ISD::FrameIndex) {
      auto *FI = cast<FrameIndexSDNode>(Arg);
      // Get frame index reference
      const McasmFrameLowering *TFI = static_cast<const McasmFrameLowering *>(
          MF.getSubtarget().getFrameLowering());
      Register FrameReg;
      StackOffset Offset = TFI->getFrameIndexReference(MF, FI->getIndex(), FrameReg);

      // Build: ADD rsp, offset (in mcasm units)
      SDValue FrameRegNode = DAG.getRegister(FrameReg, MVT::i32);
      int64_t OffsetVal = Offset.getFixed();
      if (OffsetVal != 0) {
        SDValue OffsetNode = DAG.getConstant(OffsetVal, dl, MVT::i32);
        Arg = DAG.getNode(ISD::ADD, dl, MVT::i32, FrameRegNode, OffsetNode);
      } else {
        // Offset is 0, just use the frame register directly
        Arg = FrameRegNode;
      }
    }

    if (VA.isRegLoc()) {
      // Queue up register to pass
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      // Stack argument
      assert(VA.isMemLoc() && "Expected memory location");
      SDValue PtrOff = DAG.getIntPtrConstant(VA.getLocMemOffset(), dl);
      PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32,
                           DAG.getRegister(Mcasm::rsp, MVT::i32), PtrOff);
      MemOpChains.push_back(DAG.getStore(Chain, dl, Arg, PtrOff,
                                         MachinePointerInfo()));
    }
  }

  // Emit copies for argument registers
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build register list for CopyToReg
  SDValue Glue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, Glue);
    Glue = Chain.getValue(1);
  }

  // Convert GlobalAddress/ExternalSymbol to TargetGlobalAddress/TargetExternalSymbol
  // Direct calls will use CALL instruction, indirect calls will use CALLD
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i32,
                                         G->getOffset());
  } else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32);
  }
  // If Callee is already a register (indirect call via function pointer),
  // it will be matched by CALLD pattern

  // Emit call or tail call instruction
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add register operands
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                   RegsToPass[i].second.getValueType()));

  if (Glue.getNode())
    Ops.push_back(Glue);

  if (IsTailCall) {
    // Tail call: JMP directly to the callee, no return value handling needed
    return DAG.getNode(McasmISD::TC_RETURN, dl, MVT::Other, Ops);
  }

  // Regular call
  Chain = DAG.getNode(McasmISD::CALL, dl, DAG.getVTList(MVT::Other, MVT::Glue),
                      Ops);
  Glue = Chain.getValue(1);

  // Handle return values
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RVInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  RVInfo.AnalyzeCallResult(Ins, RetCC_Mcasm);

  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                                RVLocs[i].getValVT(), Glue).getValue(1);
    Glue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  // Clean up stack if needed
  if (NumBytes > 0) {
    Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, dl);
    Glue = Chain.getValue(1);
  }

  return Chain;
}

SDValue McasmTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_Mcasm);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps;
  RetOps.push_back(Chain);  // Operand 0: Chain

  // NOTE: mcasm RET instruction has NO parameters
  // Caller cleanup is done by caller using ADD rsp or POP after CALL
  // (see ZH_Calling-Convention.md: "调用者清理")

  // Copy return values to their assigned locations
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Only register returns are supported");

    // Use VA.getValNo() — RVLocs entries can be multiple for a single original value
    SDValue Val = OutVals[VA.getValNo()];

    // f32 return values must be bitcast to i32 bits before writing to return register
    if (VA.getLocInfo() == CCValAssign::BCvt)
      Val = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Val);

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;  // Update chain

  // Add glue if present
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(McasmISD::RET_GLUE, dl, MVT::Other, RetOps);
}

const char *McasmTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  default: return nullptr;
  case McasmISD::CALL:            return "McasmISD::CALL";
  case McasmISD::TC_RETURN:       return "McasmISD::TC_RETURN";
  case McasmISD::RET_GLUE:        return "McasmISD::RET_GLUE";
  case McasmISD::CMP:             return "McasmISD::CMP";
  case McasmISD::BRCOND:          return "McasmISD::BRCOND";
  case McasmISD::BR_CC:           return "McasmISD::BR_CC";
  case McasmISD::Wrapper:         return "McasmISD::Wrapper";
  case McasmISD::WrapperPIC:      return "McasmISD::WrapperPIC";
  case McasmISD::FunctionWrapper: return "McasmISD::FunctionWrapper";
  }
}

SDValue McasmTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return lowerConstantPool(Op, DAG);
  case ISD::JumpTable:
    return lowerJumpTable(Op, DAG);
  case ISD::BRCOND:
    return lowerBRCOND(Op, DAG);
  case ISD::BR_CC:
    return lowerBR_CC(Op, DAG);
  case ISD::SETCC:
    return lowerSETCC(Op, DAG);
  case ISD::SELECT:
    return lowerSELECT(Op, DAG);
  case ISD::SHL_PARTS:
    return lowerSHLParts(Op, DAG);
  case ISD::SRL_PARTS:
    return lowerSRLParts(Op, DAG);
  case ISD::SRA_PARTS:
    return lowerSRAParts(Op, DAG);

  // i64 arithmetic operations - lower to libcalls
  case ISD::MUL:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::MUL_I64);
    break;
  case ISD::SDIV:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::SDIV_I64);
    break;
  case ISD::UDIV:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::UDIV_I64);
    break;
  case ISD::SREM:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::SREM_I64);
    break;
  case ISD::UREM:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::UREM_I64);
    break;
  case ISD::SHL:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::SHL_I64);
    break;
  case ISD::SRL:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::SRL_I64);
    break;
  case ISD::SRA:
    if (Op.getValueType() == MVT::i64)
      return LowerI64LibCall(Op, DAG, RTLIB::SRA_I64);
    break;

  case ISD::BUILD_VECTOR:
    // mcasm does not support vector operations - return SDValue() to let LLVM expand
    return SDValue();

  case ISD::VASTART:
    // Minimal varargs support - expand to nothing for now
    return SDValue();

  case ISD::PTRADD: {
    // CRITICAL: mcasm uses 4-BYTE address units, not byte addressing!
    // LLVM's PTRADD uses byte offsets, so we must divide by 4.
    //
    // Example: getelementptr i32, ptr %p, i32 %idx
    //   LLVM calculates: byte_offset = %idx * sizeof(i32) = %idx * 4
    //   LLVM generates: PTRADD %p, byte_offset
    //   mcasm needs: ADD %p, (byte_offset / 4) = ADD %p, %idx
    //
    SDLoc dl(Op);
    SDValue Ptr = Op.getOperand(0);
    SDValue ByteOffset = Op.getOperand(1);
    EVT PtrVT = Op.getValueType();  // Preserve the pointer type

    // Convert byte offset to mcasm address units (divide by 4)
    SDValue McasmOffset;
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(ByteOffset)) {
      // Constant case: just divide by 4
      int64_t ByteOffsetVal = C->getSExtValue();
      int64_t McasmOffsetVal = ByteOffsetVal / 4;
      McasmOffset = DAG.getConstant(McasmOffsetVal, dl, MVT::i32);
    } else if (ByteOffset.getOpcode() == ISD::SHL) {
      // Special case: ByteOffset = index << 2
      // This is LLVM's optimization for index * 4
      // We can directly extract the index
      SDValue Index = ByteOffset.getOperand(0);
      SDValue ShiftAmount = ByteOffset.getOperand(1);
      if (ConstantSDNode *SA = dyn_cast<ConstantSDNode>(ShiftAmount)) {
        if (SA->getZExtValue() == 2) {
          // Perfect: (index << 2) / 4 = index
          McasmOffset = Index;
        } else {
          // Different shift amount, use division
          SDValue Four = DAG.getConstant(4, dl, MVT::i32);
          McasmOffset = DAG.getNode(ISD::SDIV, dl, MVT::i32, ByteOffset, Four);
        }
      } else {
        // Variable shift amount, use division
        SDValue Four = DAG.getConstant(4, dl, MVT::i32);
        McasmOffset = DAG.getNode(ISD::SDIV, dl, MVT::i32, ByteOffset, Four);
      }
    } else {
      // General case: divide by 4
      SDValue Four = DAG.getConstant(4, dl, MVT::i32);
      McasmOffset = DAG.getNode(ISD::SDIV, dl, MVT::i32, ByteOffset, Four);
    }

    // Generate ADD with the original pointer type
    SDValue Result = DAG.getNode(ISD::ADD, dl, PtrVT, Ptr, McasmOffset);
    return Result;
  }

  default:
    // Operation not handled - return SDValue() to signal failure
    // This allows LLVM to use default expansion or report error
    return SDValue();
  }
}

SDValue McasmTargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  int64_t Offset = N->getOffset();

  // Create a TargetGlobalAddress node
  SDValue TargetAddr = DAG.getTargetGlobalAddress(GV, DL, Ty, Offset);

  // Check if this is a function address
  // Functions require MOVD (more expensive, should be cached)
  // Data addresses use regular MOV
  bool isFunction = isa<Function>(GV);
  fprintf(stderr, "DEBUG lowerGlobalAddress: GV=%s, isFunction=%d\n",
          GV->getName().str().c_str(), isFunction);
  fflush(stderr);

  if (isFunction) {
    // Function address - use FunctionWrapper -> will match MOVD32ri
    fprintf(stderr, "DEBUG: Using FunctionWrapper for %s\n", GV->getName().str().c_str());
    fflush(stderr);
    return DAG.getNode(McasmISD::FunctionWrapper, DL, Ty, TargetAddr);
  } else {
    // Data address - use regular Wrapper -> will match MOV32ri
    fprintf(stderr, "DEBUG: Using Wrapper for %s\n", GV->getName().str().c_str());
    fflush(stderr);
    return DAG.getNode(McasmISD::Wrapper, DL, Ty, TargetAddr);
  }
}

SDValue McasmTargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  BlockAddressSDNode *N = cast<BlockAddressSDNode>(Op);
  const BlockAddress *BA = N->getBlockAddress();
  int64_t Offset = N->getOffset();

  SDValue TargetAddr = DAG.getTargetBlockAddress(BA, Ty, Offset);
  return DAG.getNode(McasmISD::Wrapper, DL, Ty, TargetAddr);
}

SDValue McasmTargetLowering::lowerConstantPool(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);
  const Constant *C = N->getConstVal();
  int64_t Offset = N->getOffset();

  SDValue TargetAddr = DAG.getTargetConstantPool(C, Ty, N->getAlign(), Offset);
  return DAG.getNode(McasmISD::Wrapper, DL, Ty, TargetAddr);
}

SDValue McasmTargetLowering::lowerJumpTable(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  JumpTableSDNode *N = cast<JumpTableSDNode>(Op);
  int JTI = N->getIndex();

  SDValue TargetAddr = DAG.getTargetJumpTable(JTI, Ty);
  return DAG.getNode(McasmISD::Wrapper, DL, Ty, TargetAddr);
}

SDValue McasmTargetLowering::lowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);

  // If the condition is a setcc node, we can directly use its operands
  if (Cond.getOpcode() == ISD::SETCC) {
    SDValue LHS = Cond.getOperand(0);
    SDValue RHS = Cond.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();

    // mcasm only has signed comparison instructions (JG/JL/JGE/JLE).
    // Convert unsigned comparisons to signed by XOR-ing both operands
    // with 0x80000000 (flip the sign bit).  This preserves unsigned ordering
    // under signed comparison:  a <u b  iff  (a^MSB) <s (b^MSB).
    MVT OpVT = LHS.getSimpleValueType();
    if (OpVT == MVT::i32) {
      SDValue MSB = DAG.getConstant(0x80000000U, DL, MVT::i32);
      auto flipMSB = [&](SDValue V) {
        return DAG.getNode(ISD::XOR, DL, MVT::i32, V, MSB);
      };
      switch (CC) {
      case ISD::SETULT: LHS = flipMSB(LHS); RHS = flipMSB(RHS); CC = ISD::SETLT; break;
      case ISD::SETULE: LHS = flipMSB(LHS); RHS = flipMSB(RHS); CC = ISD::SETLE; break;
      case ISD::SETUGT: LHS = flipMSB(LHS); RHS = flipMSB(RHS); CC = ISD::SETGT; break;
      case ISD::SETUGE: LHS = flipMSB(LHS); RHS = flipMSB(RHS); CC = ISD::SETGE; break;
      default: break;
      }
    }

    // Create BR_CC node: chain, lhs, rhs, cc, dest
    return DAG.getNode(McasmISD::BR_CC, DL, MVT::Other, Chain, LHS, RHS,
                       DAG.getCondCode(CC), Dest);
  }

  // Non-SETCC condition: the value is already a boolean-like i32.
  // Compare to zero with JNZ (branch if non-zero = true).
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  return DAG.getNode(McasmISD::BR_CC, DL, MVT::Other, Chain, Cond, Zero,
                     DAG.getCondCode(ISD::SETNE), Dest);
}

SDValue McasmTargetLowering::lowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  // ISD::BR_CC operand layout: chain, cc, lhs, rhs, dest
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS   = Op.getOperand(2);
  SDValue RHS   = Op.getOperand(3);
  SDValue Dest  = Op.getOperand(4);

  // mcasm only has signed comparison instructions (JG/JL/JGE/JLE).
  // Convert unsigned comparisons to signed by XOR-ing both operands
  // with 0x80000000 (flip the sign bit).
  MVT OpVT = LHS.getSimpleValueType();
  if (OpVT == MVT::i32) {
    SDValue MSB = DAG.getConstant(0x80000000U, DL, MVT::i32);
    auto flipMSB = [&](SDValue V) {
      return DAG.getNode(ISD::XOR, DL, MVT::i32, V, MSB);
    };
    switch (CC) {
    case ISD::SETULT: LHS=flipMSB(LHS); RHS=flipMSB(RHS); CC=ISD::SETLT; break;
    case ISD::SETULE: LHS=flipMSB(LHS); RHS=flipMSB(RHS); CC=ISD::SETLE; break;
    case ISD::SETUGT: LHS=flipMSB(LHS); RHS=flipMSB(RHS); CC=ISD::SETGT; break;
    case ISD::SETUGE: LHS=flipMSB(LHS); RHS=flipMSB(RHS); CC=ISD::SETGE; break;
    default: break;
    }
  }

  // Emit McasmISD::BR_CC: chain, lhs, rhs, cc, dest
  return DAG.getNode(McasmISD::BR_CC, DL, MVT::Other,
                     Chain, LHS, RHS, DAG.getCondCode(CC), Dest);
}

// Lower SETCC to branchless arithmetic (mcasm has no SETcc instruction).
//
// Core primitive: SETULT(a, b) = ((a - b) >> 31) & 1
//   Unsigned subtraction wraps; if a < b (unsigned), the result has bit 31 set.
//
// Signed comparisons are reduced to unsigned via MSB flip:
//   SETLT(a,b) = SETULT(a^0x80000000, b^0x80000000)
//
// Equality:
//   SETNE(a,b) = ((d | -d) >> 31)  where d = a ^ b
//     (d is 0 iff a==b; d | -d has bit 31 set iff d != 0)
//   SETEQ = SETNE ^ 1
//
SDValue McasmTargetLowering::lowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();

  // Only handle i32 operands; other types are handled elsewhere.
  if (LHS.getSimpleValueType() != MVT::i32)
    return SDValue();

  SDValue C31  = DAG.getConstant(31,          DL, MVT::i32);
  SDValue C1   = DAG.getConstant(1,           DL, MVT::i32);
  SDValue MSB  = DAG.getConstant(0x80000000U, DL, MVT::i32);
  SDValue AllF = DAG.getConstant(0xFFFFFFFFU, DL, MVT::i32);

  // branchless SETULT: ((A - B) >> 31) & 1
  auto setult = [&](SDValue A, SDValue B) -> SDValue {
    SDValue Sub = DAG.getNode(ISD::SUB, DL, MVT::i32, A, B);
    SDValue Shr = DAG.getNode(ISD::SRL, DL, MVT::i32, Sub, C31);
    return DAG.getNode(ISD::AND, DL, MVT::i32, Shr, C1);
  };
  // flip the MSB (sign bit) to convert signed ordering to unsigned
  auto flipMSB = [&](SDValue V) {
    return DAG.getNode(ISD::XOR, DL, MVT::i32, V, MSB);
  };
  // NOT a 1-bit boolean value: x ^ 1
  auto notBit = [&](SDValue V) {
    return DAG.getNode(ISD::XOR, DL, MVT::i32, V, C1);
  };
  // branchless SETNE: ((d | -d) >> 31)  where d = A ^ B
  // neg(x) = (NOT x) + 1
  auto setne = [&](SDValue A, SDValue B) -> SDValue {
    SDValue D    = DAG.getNode(ISD::XOR, DL, MVT::i32, A, B);
    SDValue NotD = DAG.getNode(ISD::XOR, DL, MVT::i32, D, AllF);
    SDValue NegD = DAG.getNode(ISD::ADD, DL, MVT::i32, NotD, C1);
    SDValue OrD  = DAG.getNode(ISD::OR,  DL, MVT::i32, D, NegD);
    return DAG.getNode(ISD::SRL, DL, MVT::i32, OrD, C31);
  };

  switch (CC) {
  default:
    return SDValue(); // Let LLVM handle unsupported CCs
  case ISD::SETULT:  return setult(LHS, RHS);
  case ISD::SETLT:   return setult(flipMSB(LHS), flipMSB(RHS));
  case ISD::SETUGT:  return setult(RHS, LHS);
  case ISD::SETGT:   return setult(flipMSB(RHS), flipMSB(LHS));
  case ISD::SETULE:  return notBit(setult(RHS, LHS));       // NOT(a > b)
  case ISD::SETLE:   return notBit(setult(flipMSB(RHS), flipMSB(LHS)));
  case ISD::SETUGE:  return notBit(setult(LHS, RHS));       // NOT(a < b)
  case ISD::SETGE:   return notBit(setult(flipMSB(LHS), flipMSB(RHS)));
  case ISD::SETNE:   return setne(LHS, RHS);
  case ISD::SETEQ:   return notBit(setne(LHS, RHS));
  }
}

// Lower SELECT to branchless arithmetic (mcasm has no CMOV instruction).
//
// Precondition: Cond is a boolean i32 value (0 = false, 1 = true).
// This holds because: (a) the condition comes from lowerSETCC which produces
// 0/1, and (b) i1 conditions are zero-extended to i32 during type legalization.
//
// Formula: result = FalseVal + (-Cond) & (TrueVal - FalseVal)
//   When Cond=1: -Cond=0xFFFFFFFF → mask=TrueVal-FalseVal → result=TrueVal
//   When Cond=0: -Cond=0          → mask=0                → result=FalseVal
//
SDValue McasmTargetLowering::lowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Cond     = Op.getOperand(0); // 0 or 1
  SDValue TrueVal  = Op.getOperand(1);
  SDValue FalseVal = Op.getOperand(2);

  if (Op.getSimpleValueType() != MVT::i32)
    return SDValue();

  // neg(Cond) = NOT(Cond) + 1
  SDValue NotCond = DAG.getNode(ISD::XOR, DL, MVT::i32, Cond,
                                DAG.getConstant(0xFFFFFFFFU, DL, MVT::i32));
  SDValue NegCond = DAG.getNode(ISD::ADD, DL, MVT::i32, NotCond,
                                DAG.getConstant(1, DL, MVT::i32));
  SDValue Diff = DAG.getNode(ISD::SUB, DL, MVT::i32, TrueVal, FalseVal);
  SDValue Mask = DAG.getNode(ISD::AND, DL, MVT::i32, NegCond, Diff);
  return DAG.getNode(ISD::ADD, DL, MVT::i32, FalseVal, Mask);
}

// Helper used by all three *_PARTS lowerings.
// Computes:
//   n31_ne0_mask  = 0xFFFFFFFF if (N31 != 0), else 0
//   mask_lt32     = 0xFFFFFFFF if (ShAmt < 32), else 0
//   mask_ge32     = ~mask_lt32
// Also returns Inv31 = (32 - N31) & 31  (safe complementary shift).
static std::tuple<SDValue, SDValue, SDValue, SDValue, SDValue>
computeShiftMasks(SDValue ShAmt, SDLoc DL, SelectionDAG &DAG) {
  MVT VT = MVT::i32;
  SDValue C0   = DAG.getConstant(0U,          DL, VT);
  SDValue C31  = DAG.getConstant(31U,         DL, VT);
  SDValue C32  = DAG.getConstant(32U,         DL, VT);
  SDValue AllF = DAG.getConstant(0xFFFFFFFFU, DL, VT);

  // N31 = ShAmt & 31
  SDValue N31 = DAG.getNode(ISD::AND, DL, VT, ShAmt, C31);

  // Inv31 = (32 - N31) & 31  (clamp to avoid shift-by-32)
  SDValue Inv31 = DAG.getNode(ISD::AND, DL, VT,
                    DAG.getNode(ISD::SUB, DL, VT, C32, N31), C31);

  // n31_ne0_mask: (N31 | -N31) >> 31 gives 0-or-1; then 0 - bit gives mask
  SDValue NegN31      = DAG.getNode(ISD::SUB, DL, VT, C0, N31);
  SDValue Bit_n31_ne0 = DAG.getNode(ISD::SRL, DL, VT,
                          DAG.getNode(ISD::OR, DL, VT, N31, NegN31), C31);
  SDValue N31NeZeroMask = DAG.getNode(ISD::SUB, DL, VT, C0, Bit_n31_ne0);

  // mask_lt32: ((ShAmt - 32) >> 31) & 1 = 1 if ShAmt < 32, then 0 - bit
  SDValue ShAmt_m32 = DAG.getNode(ISD::SUB, DL, VT, ShAmt, C32);
  SDValue Bit_lt32  = DAG.getNode(ISD::SRL, DL, VT, ShAmt_m32, C31);
  SDValue Mask_lt32 = DAG.getNode(ISD::SUB, DL, VT, C0, Bit_lt32);
  SDValue Mask_ge32 = DAG.getNode(ISD::XOR, DL, VT, Mask_lt32, AllF);

  return {N31, Inv31, N31NeZeroMask, Mask_lt32, Mask_ge32};
}

// Lower SHL_PARTS to branchless arithmetic.
//
// Input:  Lo = low i32, Hi = high i32, ShAmt = shift amount [0..63]
// Output: (Lo_out, Hi_out) such that (Hi_out:Lo_out) = (Hi:Lo) << ShAmt
//
// Small case (ShAmt < 32), N31 = ShAmt & 31:
//   Lo_out = Lo << N31
//   Hi_out = (Hi << N31) | ((Lo >> Inv31) & n31_ne0_mask)
//
// Large case (ShAmt >= 32), N31 = ShAmt - 32:
//   Lo_out = 0
//   Hi_out = Lo << N31
//
SDValue McasmTargetLowering::lowerSHLParts(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo    = Op.getOperand(0);
  SDValue Hi    = Op.getOperand(1);
  SDValue ShAmt = Op.getOperand(2);
  MVT VT = MVT::i32;

  auto [N31, Inv31, N31NeZeroMask, Mask_lt32, Mask_ge32] =
      computeShiftMasks(ShAmt, DL, DAG);

  // Small case
  SDValue Lo_Sh    = DAG.getNode(ISD::SHL, DL, VT, Lo, N31);   // lo << N31
  SDValue Hi_Sh    = DAG.getNode(ISD::SHL, DL, VT, Hi, N31);   // hi << N31
  SDValue Lo_SHR   = DAG.getNode(ISD::SRL, DL, VT, Lo, Inv31); // lo >> Inv31
  SDValue Cross    = DAG.getNode(ISD::AND, DL, VT, Lo_SHR, N31NeZeroMask);
  SDValue Hi_small = DAG.getNode(ISD::OR,  DL, VT, Hi_Sh, Cross);

  // Lo_out = Lo_Sh & Mask_lt32  (zeroed when ShAmt >= 32)
  SDValue Lo_out = DAG.getNode(ISD::AND, DL, VT, Lo_Sh, Mask_lt32);

  // Hi_out = (Hi_small & Mask_lt32) | (Lo_Sh & Mask_ge32)
  // Large-case hi is Lo << N31 = Lo_Sh (same formula, N31 = ShAmt-32 there)
  SDValue Hi_out = DAG.getNode(ISD::OR, DL, VT,
                    DAG.getNode(ISD::AND, DL, VT, Hi_small, Mask_lt32),
                    DAG.getNode(ISD::AND, DL, VT, Lo_Sh,    Mask_ge32));

  return DAG.getMergeValues({Lo_out, Hi_out}, DL);
}

// Lower SRL_PARTS to branchless arithmetic.
//
// Small case (ShAmt < 32), N31 = ShAmt & 31:
//   Hi_out = Hi >> N31                                          (logical)
//   Lo_out = (Lo >> N31) | ((Hi << Inv31) & n31_ne0_mask)
//
// Large case (ShAmt >= 32), N31 = ShAmt - 32:
//   Hi_out = 0
//   Lo_out = Hi >> N31                                          (logical)
//
SDValue McasmTargetLowering::lowerSRLParts(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo    = Op.getOperand(0);
  SDValue Hi    = Op.getOperand(1);
  SDValue ShAmt = Op.getOperand(2);
  MVT VT = MVT::i32;

  auto [N31, Inv31, N31NeZeroMask, Mask_lt32, Mask_ge32] =
      computeShiftMasks(ShAmt, DL, DAG);

  // Small case
  SDValue Hi_SHR   = DAG.getNode(ISD::SRL, DL, VT, Hi, N31);   // hi >> N31 (logical)
  SDValue Lo_SHR   = DAG.getNode(ISD::SRL, DL, VT, Lo, N31);   // lo >> N31
  SDValue Hi_SHL   = DAG.getNode(ISD::SHL, DL, VT, Hi, Inv31); // hi << Inv31
  SDValue Cross    = DAG.getNode(ISD::AND, DL, VT, Hi_SHL, N31NeZeroMask);
  SDValue Lo_small = DAG.getNode(ISD::OR,  DL, VT, Lo_SHR, Cross);

  // Lo_out = (Lo_small & Mask_lt32) | (Hi_SHR & Mask_ge32)
  // Large-case lo is Hi >> N31 = Hi_SHR (same formula, N31 = ShAmt-32 there)
  SDValue Lo_out = DAG.getNode(ISD::OR, DL, VT,
                    DAG.getNode(ISD::AND, DL, VT, Lo_small, Mask_lt32),
                    DAG.getNode(ISD::AND, DL, VT, Hi_SHR,   Mask_ge32));

  // Hi_out = Hi_SHR & Mask_lt32  (zeroed when ShAmt >= 32)
  SDValue Hi_out = DAG.getNode(ISD::AND, DL, VT, Hi_SHR, Mask_lt32);

  return DAG.getMergeValues({Lo_out, Hi_out}, DL);
}

// Lower SRA_PARTS to branchless arithmetic.
//
// Small case (ShAmt < 32), N31 = ShAmt & 31:
//   Hi_out = Hi >>> N31                                         (arithmetic)
//   Lo_out = (Lo >> N31) | ((Hi << Inv31) & n31_ne0_mask)
//
// Large case (ShAmt >= 32), N31 = ShAmt - 32:
//   Hi_out = Hi >>> 31                    (sign extension)
//   Lo_out = Hi >>> N31                   (arithmetic)
//
SDValue McasmTargetLowering::lowerSRAParts(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo    = Op.getOperand(0);
  SDValue Hi    = Op.getOperand(1);
  SDValue ShAmt = Op.getOperand(2);
  MVT VT = MVT::i32;

  SDValue C31 = DAG.getConstant(31U, DL, VT);

  auto [N31, Inv31, N31NeZeroMask, Mask_lt32, Mask_ge32] =
      computeShiftMasks(ShAmt, DL, DAG);

  // Small case
  SDValue Hi_SRA   = DAG.getNode(ISD::SRA, DL, VT, Hi, N31);   // hi >>> N31 (arith)
  SDValue Lo_SHR   = DAG.getNode(ISD::SRL, DL, VT, Lo, N31);   // lo >> N31 (logical)
  SDValue Hi_SHL   = DAG.getNode(ISD::SHL, DL, VT, Hi, Inv31); // hi << Inv31
  SDValue Cross    = DAG.getNode(ISD::AND, DL, VT, Hi_SHL, N31NeZeroMask);
  SDValue Lo_small = DAG.getNode(ISD::OR,  DL, VT, Lo_SHR, Cross);

  // Large-case hi = sign extension of Hi (all bits = sign bit of Hi)
  SDValue Hi_sign = DAG.getNode(ISD::SRA, DL, VT, Hi, C31); // hi >>> 31

  // Lo_out = (Lo_small & Mask_lt32) | (Hi_SRA & Mask_ge32)
  // Large-case lo is Hi >>> N31 = Hi_SRA (N31 = ShAmt-32 for large case)
  SDValue Lo_out = DAG.getNode(ISD::OR, DL, VT,
                    DAG.getNode(ISD::AND, DL, VT, Lo_small, Mask_lt32),
                    DAG.getNode(ISD::AND, DL, VT, Hi_SRA,   Mask_ge32));

  // Hi_out = (Hi_SRA & Mask_lt32) | (Hi_sign & Mask_ge32)
  SDValue Hi_out = DAG.getNode(ISD::OR, DL, VT,
                    DAG.getNode(ISD::AND, DL, VT, Hi_SRA,  Mask_lt32),
                    DAG.getNode(ISD::AND, DL, VT, Hi_sign, Mask_ge32));

  return DAG.getMergeValues({Lo_out, Hi_out}, DL);
}

// Lower i64 operations to libcalls
// This bypasses LLVM's type legalization which doesn't work for mcasm
SDValue McasmTargetLowering::LowerI64LibCall(SDValue Op, SelectionDAG &DAG,
                                               RTLIB::Libcall LC) const {
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();

  // Get operands
  SmallVector<SDValue, 2> Ops(Op->op_begin(), Op->op_end());

  // Use LLVM's makeLibCall utility
  // This handles all the argument setup automatically
  MakeLibCallOptions CallOptions;
  std::pair<SDValue, SDValue> CallResult =
      makeLibCall(DAG, LC, VT, Ops, CallOptions, DL);

  return CallResult.first;
}
