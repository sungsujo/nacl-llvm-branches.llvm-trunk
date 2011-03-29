//===-- ARMISelDAGToDAG.cpp - A dag to dag inst selector for ARM ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the ARM target.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-isel"
#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMAddressingModes.h"
#include "ARMTargetMachine.h"
#include "llvm/CallingConv.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Intrinsics.h"
#include "llvm/LLVMContext.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

// @LOCALMOD-START
#include "llvm/Support/CommandLine.h"
namespace llvm {
  extern cl::opt<bool> FlagSfiDisableStore;
}
// @LOCALMOD-END

using namespace llvm;


static cl::opt<bool>
DisableShifterOp("disable-shifter-op", cl::Hidden,
  cl::desc("Disable isel of shifter-op"),
  cl::init(false));

static cl::opt<bool>
CheckVMLxHazard("check-vmlx-hazard", cl::Hidden,
  cl::desc("Check fp vmla / vmls hazard at isel time"),
  cl::init(false));

//===--------------------------------------------------------------------===//
/// ARMDAGToDAGISel - ARM specific code to select ARM machine
/// instructions for SelectionDAG operations.
///
namespace {

enum AddrMode2Type {
  AM2_BASE, // Simple AM2 (+-imm12)
  AM2_SHOP  // Shifter-op AM2
};

class ARMDAGToDAGISel : public SelectionDAGISel {
  ARMBaseTargetMachine &TM;
  const ARMBaseInstrInfo *TII;

  /// Subtarget - Keep a pointer to the ARMSubtarget around so that we can
  /// make the right decision when generating code for different targets.
  const ARMSubtarget *Subtarget;

public:
  explicit ARMDAGToDAGISel(ARMBaseTargetMachine &tm,
                           CodeGenOpt::Level OptLevel)
    : SelectionDAGISel(tm, OptLevel), TM(tm),
      TII(static_cast<const ARMBaseInstrInfo*>(TM.getInstrInfo())),
      Subtarget(&TM.getSubtarget<ARMSubtarget>()) {
  }

  virtual const char *getPassName() const {
    return "ARM Instruction Selection";
  }

  /// getI32Imm - Return a target constant of type i32 with the specified
  /// value.
  inline SDValue getI32Imm(unsigned Imm) {
    return CurDAG->getTargetConstant(Imm, MVT::i32);
  }

  SDNode *Select(SDNode *N);


  bool hasNoVMLxHazardUse(SDNode *N) const;
  bool isShifterOpProfitable(const SDValue &Shift,
                             ARM_AM::ShiftOpc ShOpcVal, unsigned ShAmt);
  bool SelectShifterOperandReg(SDValue N, SDValue &A,
                               SDValue &B, SDValue &C);
  bool SelectShiftShifterOperandReg(SDValue N, SDValue &A,
                                    SDValue &B, SDValue &C);
  bool SelectAddrModeImm12(SDValue N, SDValue &Base, SDValue &OffImm);
  bool SelectLdStSOReg(SDValue N, SDValue &Base, SDValue &Offset, SDValue &Opc);

  AddrMode2Type SelectAddrMode2Worker(SDNode *Op, SDValue N, SDValue &Base,
                                      SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode2Base(SDNode *Op,
                           SDValue N, SDValue &Base, SDValue &Offset,
                           SDValue &Opc) {
    return SelectAddrMode2Worker(Op, N, Base, Offset, Opc) == AM2_BASE;
  }

  bool SelectAddrMode2ShOp(SDNode *Op,
                           SDValue N, SDValue &Base, SDValue &Offset,
                           SDValue &Opc) {
    return SelectAddrMode2Worker(Op, N, Base, Offset, Opc) == AM2_SHOP;
  }

  bool SelectAddrMode2(SDNode *Op, 
                       SDValue N, SDValue &Base, SDValue &Offset,
                       SDValue &Opc) {
    SelectAddrMode2Worker(Op, N, Base, Offset, Opc);
//    return SelectAddrMode2ShOp(N, Base, Offset, Opc);
    // This always matches one way or another.
    return true;
  }

  bool SelectAddrMode2Offset(SDNode *Op, SDValue N,
                             SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode3(SDNode *Op, SDValue N, SDValue &Base,
                       SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode3Offset(SDNode *Op, SDValue N,
                             SDValue &Offset, SDValue &Opc);
  bool SelectAddrMode5(SDValue N, SDValue &Base,
                       SDValue &Offset);
  bool SelectAddrMode6(SDNode *Parent, SDValue N, SDValue &Addr,SDValue &Align);

  bool SelectAddrModePC(SDValue N, SDValue &Offset, SDValue &Label);

  // Thumb Addressing Modes:
  bool SelectThumbAddrModeRR(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectThumbAddrModeRI(SDValue N, SDValue &Base, SDValue &Offset,
                             unsigned Scale);
  bool SelectThumbAddrModeRI5S1(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectThumbAddrModeRI5S2(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectThumbAddrModeRI5S4(SDValue N, SDValue &Base, SDValue &Offset);
  bool SelectThumbAddrModeImm5S(SDValue N, unsigned Scale, SDValue &Base,
                                SDValue &OffImm);
  bool SelectThumbAddrModeImm5S1(SDValue N, SDValue &Base,
                                 SDValue &OffImm);
  bool SelectThumbAddrModeImm5S2(SDValue N, SDValue &Base,
                                 SDValue &OffImm);
  bool SelectThumbAddrModeImm5S4(SDValue N, SDValue &Base,
                                 SDValue &OffImm);
  bool SelectThumbAddrModeSP(SDValue N, SDValue &Base, SDValue &OffImm);

  // Thumb 2 Addressing Modes:
  bool SelectT2ShifterOperandReg(SDValue N,
                                 SDValue &BaseReg, SDValue &Opc);
  bool SelectT2AddrModeImm12(SDValue N, SDValue &Base, SDValue &OffImm);
  bool SelectT2AddrModeImm8(SDValue N, SDValue &Base,
                            SDValue &OffImm);
  bool SelectT2AddrModeImm8Offset(SDNode *Op, SDValue N,
                                 SDValue &OffImm);
  bool SelectT2AddrModeSoReg(SDValue N, SDValue &Base,
                             SDValue &OffReg, SDValue &ShImm);

  inline bool is_so_imm(unsigned Imm) const {
    return ARM_AM::getSOImmVal(Imm) != -1;
  }

  inline bool is_so_imm_not(unsigned Imm) const {
    return ARM_AM::getSOImmVal(~Imm) != -1;
  }

  inline bool is_t2_so_imm(unsigned Imm) const {
    return ARM_AM::getT2SOImmVal(Imm) != -1;
  }

  inline bool is_t2_so_imm_not(unsigned Imm) const {
    return ARM_AM::getT2SOImmVal(~Imm) != -1;
  }

  inline bool Pred_so_imm(SDNode *inN) const {
    ConstantSDNode *N = cast<ConstantSDNode>(inN);
    return is_so_imm(N->getZExtValue());
  }

  inline bool Pred_t2_so_imm(SDNode *inN) const {
    ConstantSDNode *N = cast<ConstantSDNode>(inN);
    return is_t2_so_imm(N->getZExtValue());
  }

  // Include the pieces autogenerated from the target description.
#include "ARMGenDAGISel.inc"

private:
  /// SelectARMIndexedLoad - Indexed (pre/post inc/dec) load matching code for
  /// ARM.
  SDNode *SelectARMIndexedLoad(SDNode *N);
  SDNode *SelectT2IndexedLoad(SDNode *N);

  /// SelectVLD - Select NEON load intrinsics.  NumVecs should be
  /// 1, 2, 3 or 4.  The opcode arrays specify the instructions used for
  /// loads of D registers and even subregs and odd subregs of Q registers.
  /// For NumVecs <= 2, QOpcodes1 is not used.
  SDNode *SelectVLD(SDNode *N, unsigned NumVecs, unsigned *DOpcodes,
                    unsigned *QOpcodes0, unsigned *QOpcodes1);

  /// SelectVST - Select NEON store intrinsics.  NumVecs should
  /// be 1, 2, 3 or 4.  The opcode arrays specify the instructions used for
  /// stores of D registers and even subregs and odd subregs of Q registers.
  /// For NumVecs <= 2, QOpcodes1 is not used.
  SDNode *SelectVST(SDNode *N, unsigned NumVecs, unsigned *DOpcodes,
                    unsigned *QOpcodes0, unsigned *QOpcodes1);

  /// SelectVLDSTLane - Select NEON load/store lane intrinsics.  NumVecs should
  /// be 2, 3 or 4.  The opcode arrays specify the instructions used for
  /// load/store of D registers and Q registers.
  SDNode *SelectVLDSTLane(SDNode *N, bool IsLoad, unsigned NumVecs,
                          unsigned *DOpcodes, unsigned *QOpcodes);

  /// SelectVLDDup - Select NEON load-duplicate intrinsics.  NumVecs
  /// should be 2, 3 or 4.  The opcode array specifies the instructions used
  /// for loading D registers.  (Q registers are not supported.)
  SDNode *SelectVLDDup(SDNode *N, unsigned NumVecs, unsigned *Opcodes);

  /// SelectVTBL - Select NEON VTBL and VTBX intrinsics.  NumVecs should be 2,
  /// 3 or 4.  These are custom-selected so that a REG_SEQUENCE can be
  /// generated to force the table registers to be consecutive.
  SDNode *SelectVTBL(SDNode *N, bool IsExt, unsigned NumVecs, unsigned Opc);

  /// SelectV6T2BitfieldExtractOp - Select SBFX/UBFX instructions for ARM.
  SDNode *SelectV6T2BitfieldExtractOp(SDNode *N, bool isSigned);

  /// SelectCMOVOp - Select CMOV instructions for ARM.
  SDNode *SelectCMOVOp(SDNode *N);
  SDNode *SelectT2CMOVShiftOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                              ARMCC::CondCodes CCVal, SDValue CCR,
                              SDValue InFlag);
  SDNode *SelectARMCMOVShiftOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                               ARMCC::CondCodes CCVal, SDValue CCR,
                               SDValue InFlag);
  SDNode *SelectT2CMOVImmOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                              ARMCC::CondCodes CCVal, SDValue CCR,
                              SDValue InFlag);
  SDNode *SelectARMCMOVImmOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                               ARMCC::CondCodes CCVal, SDValue CCR,
                               SDValue InFlag);

  SDNode *SelectConcatVector(SDNode *N);

  /// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
  /// inline asm expressions.
  virtual bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                            char ConstraintCode,
                                            std::vector<SDValue> &OutOps);

  // Form pairs of consecutive S, D, or Q registers.
  SDNode *PairSRegs(EVT VT, SDValue V0, SDValue V1);
  SDNode *PairDRegs(EVT VT, SDValue V0, SDValue V1);
  SDNode *PairQRegs(EVT VT, SDValue V0, SDValue V1);

  // Form sequences of 4 consecutive S, D, or Q registers.
  SDNode *QuadSRegs(EVT VT, SDValue V0, SDValue V1, SDValue V2, SDValue V3);
  SDNode *QuadDRegs(EVT VT, SDValue V0, SDValue V1, SDValue V2, SDValue V3);
  SDNode *QuadQRegs(EVT VT, SDValue V0, SDValue V1, SDValue V2, SDValue V3);

  // Get the alignment operand for a NEON VLD or VST instruction.
  SDValue GetVLDSTAlign(SDValue Align, unsigned NumVecs, bool is64BitVector);
};
}

/// isInt32Immediate - This method tests to see if the node is a 32-bit constant
/// operand. If so Imm will receive the 32-bit value.
static bool isInt32Immediate(SDNode *N, unsigned &Imm) {
  if (N->getOpcode() == ISD::Constant && N->getValueType(0) == MVT::i32) {
    Imm = cast<ConstantSDNode>(N)->getZExtValue();
    return true;
  }
  return false;
}

// isInt32Immediate - This method tests to see if a constant operand.
// If so Imm will receive the 32 bit value.
static bool isInt32Immediate(SDValue N, unsigned &Imm) {
  return isInt32Immediate(N.getNode(), Imm);
}

// isOpcWithIntImmediate - This method tests to see if the node is a specific
// opcode and that it has a immediate integer right operand.
// If so Imm will receive the 32 bit value.
static bool isOpcWithIntImmediate(SDNode *N, unsigned Opc, unsigned& Imm) {
  return N->getOpcode() == Opc &&
         isInt32Immediate(N->getOperand(1).getNode(), Imm);
}

/// \brief Check whether a particular node is a constant value representable as
/// (N * Scale) where (N in [\arg RangeMin, \arg RangeMax).
///
/// \param ScaledConstant [out] - On success, the pre-scaled constant value.
static bool isScaledConstantInRange(SDValue Node, unsigned Scale,
                                    int RangeMin, int RangeMax,
                                    int &ScaledConstant) {
  assert(Scale && "Invalid scale!");

  // Check that this is a constant.
  const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Node);
  if (!C)
    return false;

  ScaledConstant = (int) C->getZExtValue();
  if ((ScaledConstant % Scale) != 0)
    return false;

  ScaledConstant /= Scale;
  return ScaledConstant >= RangeMin && ScaledConstant < RangeMax;
}

/// hasNoVMLxHazardUse - Return true if it's desirable to select a FP MLA / MLS
/// node. VFP / NEON fp VMLA / VMLS instructions have special RAW hazards (at
/// least on current ARM implementations) which should be avoidded.
bool ARMDAGToDAGISel::hasNoVMLxHazardUse(SDNode *N) const {
  if (OptLevel == CodeGenOpt::None)
    return true;

  if (!CheckVMLxHazard)
    return true;

  if (!Subtarget->isCortexA8() && !Subtarget->isCortexA9())
    return true;

  if (!N->hasOneUse())
    return false;

  SDNode *Use = *N->use_begin();
  if (Use->getOpcode() == ISD::CopyToReg)
    return true;
  if (Use->isMachineOpcode()) {
    const TargetInstrDesc &TID = TII->get(Use->getMachineOpcode());
    if (TID.mayStore())
      return true;
    unsigned Opcode = TID.getOpcode();
    if (Opcode == ARM::VMOVRS || Opcode == ARM::VMOVRRD)
      return true;
    // vmlx feeding into another vmlx. We actually want to unfold
    // the use later in the MLxExpansion pass. e.g.
    // vmla
    // vmla (stall 8 cycles)
    //
    // vmul (5 cycles)
    // vadd (5 cycles)
    // vmla
    // This adds up to about 18 - 19 cycles.
    //
    // vmla
    // vmul (stall 4 cycles)
    // vadd adds up to about 14 cycles.
    return TII->isFpMLxInstruction(Opcode);
  }

  return false;
}

bool ARMDAGToDAGISel::isShifterOpProfitable(const SDValue &Shift,
                                            ARM_AM::ShiftOpc ShOpcVal,
                                            unsigned ShAmt) {
  if (!Subtarget->isCortexA9())
    return true;
  if (Shift.hasOneUse())
    return true;
  // R << 2 is free.
  return ShOpcVal == ARM_AM::lsl && ShAmt == 2;
}

bool ARMDAGToDAGISel::SelectShifterOperandReg(SDValue N,
                                              SDValue &BaseReg,
                                              SDValue &ShReg,
                                              SDValue &Opc) {
  if (DisableShifterOp)
    return false;

  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N);

  // Don't match base register only case. That is matched to a separate
  // lower complexity pattern with explicit register operand.
  if (ShOpcVal == ARM_AM::no_shift) return false;

  BaseReg = N.getOperand(0);
  unsigned ShImmVal = 0;
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    ShReg = CurDAG->getRegister(0, MVT::i32);
    ShImmVal = RHS->getZExtValue() & 31;
  } else {
    ShReg = N.getOperand(1);
    if (!isShifterOpProfitable(N, ShOpcVal, ShImmVal))
      return false;
  }
  Opc = CurDAG->getTargetConstant(ARM_AM::getSORegOpc(ShOpcVal, ShImmVal),
                                  MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectShiftShifterOperandReg(SDValue N,
                                                   SDValue &BaseReg,
                                                   SDValue &ShReg,
                                                   SDValue &Opc) {
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N);

  // Don't match base register only case. That is matched to a separate
  // lower complexity pattern with explicit register operand.
  if (ShOpcVal == ARM_AM::no_shift) return false;

  BaseReg = N.getOperand(0);
  unsigned ShImmVal = 0;
  // Do not check isShifterOpProfitable. This must return true.
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    ShReg = CurDAG->getRegister(0, MVT::i32);
    ShImmVal = RHS->getZExtValue() & 31;
  } else {
    ShReg = N.getOperand(1);
  }
  Opc = CurDAG->getTargetConstant(ARM_AM::getSORegOpc(ShOpcVal, ShImmVal),
                                  MVT::i32);
  return true;
}

// @LOCALMOD-START
static bool ShouldOperandBeUnwrappedForUseAsBaseAddress(
  SDValue& N, const ARMSubtarget* Subtarget) {
  assert (N.getOpcode() == ARMISD::Wrapper);
  // Never use this transformation if constant island pools are disallowed 
  if (Subtarget->isTargetNaCl() && (!FlagSfiEnableCP)) return false;

  // always apply this when we do not have movt/movw available
  // (if we do have movt/movw we be able to get rid of the
  // constant pool entry altogether)
  if (!Subtarget->useMovt()) return true;
  // explain why we do not want to use this for TargetGlobalAddress
  if (N.getOperand(0).getOpcode() != ISD::TargetGlobalAddress) return true;
  return false;
}
// @LOCALMOD-END

bool ARMDAGToDAGISel::SelectAddrModeImm12(SDValue N,
                                          SDValue &Base,
                                          SDValue &OffImm) {
  // Match simple R + imm12 operands.

  // Base only.
  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB) {
    if (N.getOpcode() == ISD::FrameIndex) {
      // Match frame index...
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
      OffImm  = CurDAG->getTargetConstant(0, MVT::i32);
      return true;
    } else if (N.getOpcode() == ARMISD::Wrapper &&
               // @LOCALMOD
               ShouldOperandBeUnwrappedForUseAsBaseAddress(N, Subtarget)) {
      Base = N.getOperand(0);
    } else
      Base = N;
    OffImm  = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    int RHSC = (int)RHS->getZExtValue();
    if (N.getOpcode() == ISD::SUB)
      RHSC = -RHSC;

    if (RHSC >= 0 && RHSC < 0x1000) { // 12 bits (unsigned)
      Base   = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
      }
      OffImm = CurDAG->getTargetConstant(RHSC, MVT::i32);
      return true;
    }
  }

  // Base only.
  Base = N;
  OffImm  = CurDAG->getTargetConstant(0, MVT::i32);
  return true;
}



bool ARMDAGToDAGISel::SelectLdStSOReg(SDValue N, SDValue &Base, SDValue &Offset,
                                      SDValue &Opc) {
  // @LOCALMOD-BEGIN
  return false;   // Can't handle two registers
  // @LOCALMOD-END
  if (N.getOpcode() == ISD::MUL &&
      (!Subtarget->isCortexA9() || N.hasOneUse())) {
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      // X * [3,5,9] -> X + X * [2,4,8] etc.
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC & 1) {
        RHSC = RHSC & ~1;
        ARM_AM::AddrOpc AddSub = ARM_AM::add;
        if (RHSC < 0) {
          AddSub = ARM_AM::sub;
          RHSC = - RHSC;
        }
        if (isPowerOf2_32(RHSC)) {
          unsigned ShAmt = Log2_32(RHSC);
          Base = Offset = N.getOperand(0);
          Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt,
                                                            ARM_AM::lsl),
                                          MVT::i32);
          return true;
        }
      }
    }
  }

  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB)
    return false;

  // Leave simple R +/- imm12 operands for LDRi12
  if (N.getOpcode() == ISD::ADD) {
    int RHSC;
    if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/1,
                                -0x1000+1, 0x1000, RHSC)) // 12 bits.
      return false;
  }

  if (Subtarget->isCortexA9() && !N.hasOneUse())
    // Compute R +/- (R << N) and reuse it.
    return false;

  // Otherwise this is R +/- [possibly shifted] R.
  ARM_AM::AddrOpc AddSub = N.getOpcode() == ISD::ADD ? ARM_AM::add:ARM_AM::sub;
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOperand(1));
  unsigned ShAmt = 0;

  Base   = N.getOperand(0);
  Offset = N.getOperand(1);

  if (ShOpcVal != ARM_AM::no_shift) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.
    if (ConstantSDNode *Sh =
           dyn_cast<ConstantSDNode>(N.getOperand(1).getOperand(1))) {
      ShAmt = Sh->getZExtValue();
      if (isShifterOpProfitable(Offset, ShOpcVal, ShAmt))
        Offset = N.getOperand(1).getOperand(0);
      else {
        ShAmt = 0;
        ShOpcVal = ARM_AM::no_shift;
      }
    } else {
      ShOpcVal = ARM_AM::no_shift;
    }
  }

  // Try matching (R shl C) + (R).
  if (N.getOpcode() == ISD::ADD && ShOpcVal == ARM_AM::no_shift &&
      !(Subtarget->isCortexA9() || N.getOperand(0).hasOneUse())) {
    ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOperand(0));
    if (ShOpcVal != ARM_AM::no_shift) {
      // Check to see if the RHS of the shift is a constant, if not, we can't
      // fold it.
      if (ConstantSDNode *Sh =
          dyn_cast<ConstantSDNode>(N.getOperand(0).getOperand(1))) {
        ShAmt = Sh->getZExtValue();
        if (!Subtarget->isCortexA9() ||
            (N.hasOneUse() &&
             isShifterOpProfitable(N.getOperand(0), ShOpcVal, ShAmt))) {
          Offset = N.getOperand(0).getOperand(0);
          Base = N.getOperand(1);
        } else {
          ShAmt = 0;
          ShOpcVal = ARM_AM::no_shift;
        }
      } else {
        ShOpcVal = ARM_AM::no_shift;
      }
    }
  }

  Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt, ShOpcVal),
                                  MVT::i32);
  return true;
}




//-----

AddrMode2Type ARMDAGToDAGISel::SelectAddrMode2Worker(SDNode *Op,
                                                     SDValue N,
                                                     SDValue &Base,
                                                     SDValue &Offset,
// @LOCALMOD-START
// Note: In the code below we do not want "Offfset" to be real register to
// not violate ARM sandboxing.
// @LOCALMOD-END
                                                     SDValue &Opc) {
  // @LOCALMOD-START
  // avoid two reg addressing mode for stores
  const bool is_store = (Op->getOpcode() == ISD::STORE);
  if (FlagSfiDisableStore || !is_store ) {
  // @LOCALMOD-END

  if (N.getOpcode() == ISD::MUL &&
      (!Subtarget->isCortexA9() || N.hasOneUse())) {
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      // X * [3,5,9] -> X + X * [2,4,8] etc.
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC & 1) {
        RHSC = RHSC & ~1;
        ARM_AM::AddrOpc AddSub = ARM_AM::add;
        if (RHSC < 0) {
          AddSub = ARM_AM::sub;
          RHSC = - RHSC;
        }
        if (isPowerOf2_32(RHSC)) {
          unsigned ShAmt = Log2_32(RHSC);
          Base = Offset = N.getOperand(0);
          Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt,
                                                            ARM_AM::lsl),
                                          MVT::i32);
          return AM2_SHOP;
        }
      }
    }
  }
  } // @LOCALMOD

  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB) {
    Base = N;
    if (N.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    } else if (N.getOpcode() == ARMISD::Wrapper &&
               // @LOCALMOD
               ShouldOperandBeUnwrappedForUseAsBaseAddress(N, Subtarget)) {
      Base = N.getOperand(0);
    }
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(ARM_AM::add, 0,
                                                      ARM_AM::no_shift),
                                    MVT::i32);
    return AM2_BASE;
  }

  // Match simple R +/- imm12 operands.
  if (N.getOpcode() == ISD::ADD) {
    int RHSC;
    if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/1,
                                -0x1000+1, 0x1000, RHSC)) { // 12 bits.
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
      }
      Offset = CurDAG->getRegister(0, MVT::i32);

      ARM_AM::AddrOpc AddSub = ARM_AM::add;
      if (RHSC < 0) {
        AddSub = ARM_AM::sub;
        RHSC = - RHSC;
      }
      Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, RHSC,
                                                        ARM_AM::no_shift),
                                      MVT::i32);
      return AM2_BASE;
    }
  }
  
  if (Subtarget->isCortexA9() && !N.hasOneUse()) {
    // Compute R +/- (R << N) and reuse it.
    Base = N;
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(ARM_AM::add, 0,
                                                      ARM_AM::no_shift),
                                    MVT::i32);
    return AM2_BASE;
  }
  
  // @LOCALMOD-START
  // keep store addressing modes simple
  if ((!FlagSfiDisableStore) && is_store) {
    Base = N;
    if (N.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    } else if (N.getOpcode() == ARMISD::Wrapper) {
      Base = N.getOperand(0);
    }
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(ARM_AM::add, 0,
                                                      ARM_AM::no_shift),
                                    MVT::i32);
    return AM2_BASE;
  }
  // @LOCALMOD-END

  // Otherwise this is R +/- [possibly shifted] R.
  ARM_AM::AddrOpc AddSub = N.getOpcode() == ISD::ADD ? ARM_AM::add:ARM_AM::sub;
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOperand(1));
  unsigned ShAmt = 0;

  Base   = N.getOperand(0);
  Offset = N.getOperand(1);

  if (ShOpcVal != ARM_AM::no_shift) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.
    if (ConstantSDNode *Sh =
           dyn_cast<ConstantSDNode>(N.getOperand(1).getOperand(1))) {
      ShAmt = Sh->getZExtValue();
      if (isShifterOpProfitable(Offset, ShOpcVal, ShAmt))
        Offset = N.getOperand(1).getOperand(0);
      else {
        ShAmt = 0;
        ShOpcVal = ARM_AM::no_shift;
      }
    } else {
      ShOpcVal = ARM_AM::no_shift;
    }
  }

  // Try matching (R shl C) + (R).
  if (N.getOpcode() == ISD::ADD && ShOpcVal == ARM_AM::no_shift &&
      !(Subtarget->isCortexA9() || N.getOperand(0).hasOneUse())) {
    ShOpcVal = ARM_AM::getShiftOpcForNode(N.getOperand(0));
    if (ShOpcVal != ARM_AM::no_shift) {
      // Check to see if the RHS of the shift is a constant, if not, we can't
      // fold it.
      if (ConstantSDNode *Sh =
          dyn_cast<ConstantSDNode>(N.getOperand(0).getOperand(1))) {
        ShAmt = Sh->getZExtValue();
        if (!Subtarget->isCortexA9() ||
            (N.hasOneUse() &&
             isShifterOpProfitable(N.getOperand(0), ShOpcVal, ShAmt))) {
          Offset = N.getOperand(0).getOperand(0);
          Base = N.getOperand(1);
        } else {
          ShAmt = 0;
          ShOpcVal = ARM_AM::no_shift;
        }
      } else {
        ShOpcVal = ARM_AM::no_shift;
      }
    }
  }

  Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt, ShOpcVal),
                                  MVT::i32);
  return AM2_SHOP;
}

bool ARMDAGToDAGISel::SelectAddrMode2Offset(SDNode *Op, SDValue N,
                                            SDValue &Offset, SDValue &Opc) {
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  ARM_AM::AddrOpc AddSub = (AM == ISD::PRE_INC || AM == ISD::POST_INC)
    ? ARM_AM::add : ARM_AM::sub;
  int Val;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 0x1000, Val)) { // 12 bits.
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, Val,
                                                      ARM_AM::no_shift),
                                    MVT::i32);
    return true;
  }

  const bool is_store = (Opcode == ISD::STORE); // @LOCALMOD


  Offset = N;
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N);
  unsigned ShAmt = 0;
  if (ShOpcVal != ARM_AM::no_shift) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.

    //if (ConstantSDNode *Sh = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    ConstantSDNode *Sh = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if ((FlagSfiDisableStore || !is_store) && Sh ) { // @LOCALMOD
      ShAmt = Sh->getZExtValue();
      if (isShifterOpProfitable(N, ShOpcVal, ShAmt))
        Offset = N.getOperand(0);
      else {
        ShAmt = 0;
        ShOpcVal = ARM_AM::no_shift;
      }
    } else {
      ShOpcVal = ARM_AM::no_shift;
    }
  }

  Opc = CurDAG->getTargetConstant(ARM_AM::getAM2Opc(AddSub, ShAmt, ShOpcVal),
                                  MVT::i32);
  return true;
}


bool ARMDAGToDAGISel::SelectAddrMode3(SDNode *Op, SDValue N,
                                      SDValue &Base, SDValue &Offset,
                                      SDValue &Opc) {
  // @LOCALMOD-START
  const bool is_store = (Op->getOpcode() == ISD::STORE);
  if (FlagSfiDisableStore ||!is_store) {
  // @LOCALMOD-END
  if (N.getOpcode() == ISD::SUB) {

    // X - C  is canonicalize to X + -C, no need to handle it here.
    Base = N.getOperand(0);
    Offset = N.getOperand(1);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::sub, 0),MVT::i32);
    return true;
  }
  } // @LOCALMOD-END

  if (N.getOpcode() != ISD::ADD) {
    Base = N;
    if (N.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    }
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::add, 0),MVT::i32);
    return true;
  }

  // If the RHS is +/- imm8, fold into addr mode.
  int RHSC;
  if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/1,
                              -256 + 1, 256, RHSC)) { // 8 bits.
    Base = N.getOperand(0);
    if (Base.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(Base)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    }
    Offset = CurDAG->getRegister(0, MVT::i32);

    ARM_AM::AddrOpc AddSub = ARM_AM::add;
    if (RHSC < 0) {
      AddSub = ARM_AM::sub;
      RHSC = - RHSC;
    }
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(AddSub, RHSC),MVT::i32);
    return true;
  }

  // @LOCALMOD-START
  if (!FlagSfiDisableStore && is_store) {
    Base = N;
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::add, 0),MVT::i32);
    return true;
  }
  // @LOCALMOD-END

  Base = N.getOperand(0);
  Offset = N.getOperand(1);
  Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(ARM_AM::add, 0), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode3Offset(SDNode *Op, SDValue N,
                                            SDValue &Offset, SDValue &Opc) {
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  ARM_AM::AddrOpc AddSub = (AM == ISD::PRE_INC || AM == ISD::POST_INC)
    ? ARM_AM::add : ARM_AM::sub;
  int Val;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 256, Val)) { // 12 bits.
    Offset = CurDAG->getRegister(0, MVT::i32);
    Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(AddSub, Val), MVT::i32);
    return true;
  }

  Offset = N;
  Opc = CurDAG->getTargetConstant(ARM_AM::getAM3Opc(AddSub, 0), MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode5(SDValue N,
                                      SDValue &Base, SDValue &Offset) {
  if (N.getOpcode() != ISD::ADD) {
    Base = N;
    if (N.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    } else if (N.getOpcode() == ARMISD::Wrapper &&
               // @LOCALMOD
               ShouldOperandBeUnwrappedForUseAsBaseAddress(N, Subtarget)) {
      Base = N.getOperand(0);
    }
    Offset = CurDAG->getTargetConstant(ARM_AM::getAM5Opc(ARM_AM::add, 0),
                                       MVT::i32);
    return true;
  }

  // If the RHS is +/- imm8, fold into addr mode.
  int RHSC;
  if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/4,
                              -256 + 1, 256, RHSC)) {
    Base = N.getOperand(0);
    if (Base.getOpcode() == ISD::FrameIndex) {
      int FI = cast<FrameIndexSDNode>(Base)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    }

    ARM_AM::AddrOpc AddSub = ARM_AM::add;
    if (RHSC < 0) {
      AddSub = ARM_AM::sub;
      RHSC = - RHSC;
    }
    Offset = CurDAG->getTargetConstant(ARM_AM::getAM5Opc(AddSub, RHSC),
                                       MVT::i32);
    return true;
  }

  Base = N;
  Offset = CurDAG->getTargetConstant(ARM_AM::getAM5Opc(ARM_AM::add, 0),
                                     MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrMode6(SDNode *Parent, SDValue N, SDValue &Addr,
                                      SDValue &Align) {
  Addr = N;

  unsigned Alignment = 0;
  if (LSBaseSDNode *LSN = dyn_cast<LSBaseSDNode>(Parent)) {
    // This case occurs only for VLD1-lane/dup and VST1-lane instructions.
    // The maximum alignment is equal to the memory size being referenced.
    unsigned LSNAlign = LSN->getAlignment();
    unsigned MemSize = LSN->getMemoryVT().getSizeInBits() / 8;
    if (LSNAlign > MemSize && MemSize > 1)
      Alignment = MemSize;
  } else {
    // All other uses of addrmode6 are for intrinsics.  For now just record
    // the raw alignment value; it will be refined later based on the legal
    // alignment operands for the intrinsic.
    Alignment = cast<MemIntrinsicSDNode>(Parent)->getAlignment();
  }

  Align = CurDAG->getTargetConstant(Alignment, MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectAddrModePC(SDValue N,
                                       SDValue &Offset, SDValue &Label) {
  if (N.getOpcode() == ARMISD::PIC_ADD && N.hasOneUse()) {
    Offset = N.getOperand(0);
    SDValue N1 = N.getOperand(1);
    Label = CurDAG->getTargetConstant(cast<ConstantSDNode>(N1)->getZExtValue(),
                                      MVT::i32);
    return true;
  }

  return false;
}


//===----------------------------------------------------------------------===//
//                         Thumb Addressing Modes
//===----------------------------------------------------------------------===//

bool ARMDAGToDAGISel::SelectThumbAddrModeRR(SDValue N,
                                            SDValue &Base, SDValue &Offset){
  // FIXME dl should come from the parent load or store, not the address
  if (N.getOpcode() != ISD::ADD) {
    ConstantSDNode *NC = dyn_cast<ConstantSDNode>(N);
    if (!NC || !NC->isNullValue())
      return false;

    Base = Offset = N;
    return true;
  }

  Base = N.getOperand(0);
  Offset = N.getOperand(1);
  return true;
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeRI(SDValue N, SDValue &Base,
                                       SDValue &Offset, unsigned Scale) {
  if (Scale == 4) {
    SDValue TmpBase, TmpOffImm;
    if (SelectThumbAddrModeSP(N, TmpBase, TmpOffImm))
      return false;  // We want to select tLDRspi / tSTRspi instead.

    if (N.getOpcode() == ARMISD::Wrapper &&
        N.getOperand(0).getOpcode() == ISD::TargetConstantPool)
      return false;  // We want to select tLDRpci instead.
  }

  if (N.getOpcode() != ISD::ADD)
    return false;

  // Thumb does not have [sp, r] address mode.
  RegisterSDNode *LHSR = dyn_cast<RegisterSDNode>(N.getOperand(0));
  RegisterSDNode *RHSR = dyn_cast<RegisterSDNode>(N.getOperand(1));
  if ((LHSR && LHSR->getReg() == ARM::SP) ||
      (RHSR && RHSR->getReg() == ARM::SP))
    return false;

  // FIXME: Why do we explicitly check for a match here and then return false?
  // Presumably to allow something else to match, but shouldn't this be
  // documented?
  int RHSC;
  if (isScaledConstantInRange(N.getOperand(1), Scale, 0, 32, RHSC))
    return false;

  Base = N.getOperand(0);
  Offset = N.getOperand(1);
  return true;
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeRI5S1(SDValue N,
                                          SDValue &Base,
                                          SDValue &Offset) {
  return SelectThumbAddrModeRI(N, Base, Offset, 1);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeRI5S2(SDValue N,
                                          SDValue &Base,
                                          SDValue &Offset) {
  return SelectThumbAddrModeRI(N, Base, Offset, 2);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeRI5S4(SDValue N,
                                          SDValue &Base,
                                          SDValue &Offset) {
  return SelectThumbAddrModeRI(N, Base, Offset, 4);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S(SDValue N, unsigned Scale,
                                          SDValue &Base, SDValue &OffImm) {
  if (Scale == 4) {
    SDValue TmpBase, TmpOffImm;
    if (SelectThumbAddrModeSP(N, TmpBase, TmpOffImm))
      return false;  // We want to select tLDRspi / tSTRspi instead.

    if (N.getOpcode() == ARMISD::Wrapper &&
        N.getOperand(0).getOpcode() == ISD::TargetConstantPool)
      return false;  // We want to select tLDRpci instead.
  }

  if (N.getOpcode() != ISD::ADD) {
    if (N.getOpcode() == ARMISD::Wrapper &&
        !(Subtarget->useMovt() &&
          N.getOperand(0).getOpcode() == ISD::TargetGlobalAddress)) {
      Base = N.getOperand(0);
    } else {
      Base = N;
    }

    OffImm = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }

  RegisterSDNode *LHSR = dyn_cast<RegisterSDNode>(N.getOperand(0));
  RegisterSDNode *RHSR = dyn_cast<RegisterSDNode>(N.getOperand(1));
  if ((LHSR && LHSR->getReg() == ARM::SP) ||
      (RHSR && RHSR->getReg() == ARM::SP)) {
    ConstantSDNode *LHS = dyn_cast<ConstantSDNode>(N.getOperand(0));
    ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1));
    unsigned LHSC = LHS ? LHS->getZExtValue() : 0;
    unsigned RHSC = RHS ? RHS->getZExtValue() : 0;

    // Thumb does not have [sp, #imm5] address mode for non-zero imm5.
    if (LHSC != 0 || RHSC != 0) return false;

    Base = N;
    OffImm = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }

  // If the RHS is + imm5 * scale, fold into addr mode.
  int RHSC;
  if (isScaledConstantInRange(N.getOperand(1), Scale, 0, 32, RHSC)) {
    Base = N.getOperand(0);
    OffImm = CurDAG->getTargetConstant(RHSC, MVT::i32);
    return true;
  }

  Base = N.getOperand(0);
  OffImm = CurDAG->getTargetConstant(0, MVT::i32);
  return true;
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S4(SDValue N, SDValue &Base,
                                           SDValue &OffImm) {
  return SelectThumbAddrModeImm5S(N, 4, Base, OffImm);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S2(SDValue N, SDValue &Base,
                                           SDValue &OffImm) {
  return SelectThumbAddrModeImm5S(N, 2, Base, OffImm);
}

bool
ARMDAGToDAGISel::SelectThumbAddrModeImm5S1(SDValue N, SDValue &Base,
                                           SDValue &OffImm) {
  return SelectThumbAddrModeImm5S(N, 1, Base, OffImm);
}

bool ARMDAGToDAGISel::SelectThumbAddrModeSP(SDValue N,
                                            SDValue &Base, SDValue &OffImm) {
  if (N.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    OffImm = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }

  if (N.getOpcode() != ISD::ADD)
    return false;

  RegisterSDNode *LHSR = dyn_cast<RegisterSDNode>(N.getOperand(0));
  if (N.getOperand(0).getOpcode() == ISD::FrameIndex ||
      (LHSR && LHSR->getReg() == ARM::SP)) {
    // If the RHS is + imm8 * scale, fold into addr mode.
    int RHSC;
    if (isScaledConstantInRange(N.getOperand(1), /*Scale=*/4, 0, 256, RHSC)) {
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
      }
      OffImm = CurDAG->getTargetConstant(RHSC, MVT::i32);
      return true;
    }
  }

  return false;
}


//===----------------------------------------------------------------------===//
//                        Thumb 2 Addressing Modes
//===----------------------------------------------------------------------===//


bool ARMDAGToDAGISel::SelectT2ShifterOperandReg(SDValue N, SDValue &BaseReg,
                                                SDValue &Opc) {
  if (DisableShifterOp)
    return false;

  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(N);

  // Don't match base register only case. That is matched to a separate
  // lower complexity pattern with explicit register operand.
  if (ShOpcVal == ARM_AM::no_shift) return false;

  BaseReg = N.getOperand(0);
  unsigned ShImmVal = 0;
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    ShImmVal = RHS->getZExtValue() & 31;
    Opc = getI32Imm(ARM_AM::getSORegOpc(ShOpcVal, ShImmVal));
    return true;
  }

  return false;
}

bool ARMDAGToDAGISel::SelectT2AddrModeImm12(SDValue N,
                                            SDValue &Base, SDValue &OffImm) {
  // Match simple R + imm12 operands.

  // Base only.
  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::SUB) {
    if (N.getOpcode() == ISD::FrameIndex) {
      // Match frame index...
      int FI = cast<FrameIndexSDNode>(N)->getIndex();
      Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
      OffImm  = CurDAG->getTargetConstant(0, MVT::i32);
      return true;
    } else if (N.getOpcode() == ARMISD::Wrapper &&
               !(Subtarget->useMovt() &&
                 N.getOperand(0).getOpcode() == ISD::TargetGlobalAddress)) {
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::TargetConstantPool)
        return false;  // We want to select t2LDRpci instead.
    } else
      Base = N;
    OffImm  = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    if (SelectT2AddrModeImm8(N, Base, OffImm))
      // Let t2LDRi8 handle (R - imm8).
      return false;

    int RHSC = (int)RHS->getZExtValue();
    if (N.getOpcode() == ISD::SUB)
      RHSC = -RHSC;

    if (RHSC >= 0 && RHSC < 0x1000) { // 12 bits (unsigned)
      Base   = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
      }
      OffImm = CurDAG->getTargetConstant(RHSC, MVT::i32);
      return true;
    }
  }

  // Base only.
  Base = N;
  OffImm  = CurDAG->getTargetConstant(0, MVT::i32);
  return true;
}

bool ARMDAGToDAGISel::SelectT2AddrModeImm8(SDValue N,
                                           SDValue &Base, SDValue &OffImm) {
  // Match simple R - imm8 operands.
  if (N.getOpcode() == ISD::ADD || N.getOpcode() == ISD::SUB) {
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      int RHSC = (int)RHS->getSExtValue();
      if (N.getOpcode() == ISD::SUB)
        RHSC = -RHSC;

      if ((RHSC >= -255) && (RHSC < 0)) { // 8 bits (always negative)
        Base = N.getOperand(0);
        if (Base.getOpcode() == ISD::FrameIndex) {
          int FI = cast<FrameIndexSDNode>(Base)->getIndex();
          Base = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
        }
        OffImm = CurDAG->getTargetConstant(RHSC, MVT::i32);
        return true;
      }
    }
  }

  return false;
}

bool ARMDAGToDAGISel::SelectT2AddrModeImm8Offset(SDNode *Op, SDValue N,
                                                 SDValue &OffImm){
  unsigned Opcode = Op->getOpcode();
  ISD::MemIndexedMode AM = (Opcode == ISD::LOAD)
    ? cast<LoadSDNode>(Op)->getAddressingMode()
    : cast<StoreSDNode>(Op)->getAddressingMode();
  int RHSC;
  if (isScaledConstantInRange(N, /*Scale=*/1, 0, 0x100, RHSC)) { // 8 bits.
    OffImm = ((AM == ISD::PRE_INC) || (AM == ISD::POST_INC))
      ? CurDAG->getTargetConstant(RHSC, MVT::i32)
      : CurDAG->getTargetConstant(-RHSC, MVT::i32);
    return true;
  }

  return false;
}

bool ARMDAGToDAGISel::SelectT2AddrModeSoReg(SDValue N,
                                            SDValue &Base,
                                            SDValue &OffReg, SDValue &ShImm) {
  // (R - imm8) should be handled by t2LDRi8. The rest are handled by t2LDRi12.
  if (N.getOpcode() != ISD::ADD)
    return false;

  // Leave (R + imm12) for t2LDRi12, (R - imm8) for t2LDRi8.
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    int RHSC = (int)RHS->getZExtValue();
    if (RHSC >= 0 && RHSC < 0x1000) // 12 bits (unsigned)
      return false;
    else if (RHSC < 0 && RHSC >= -255) // 8 bits
      return false;
  }

  if (Subtarget->isCortexA9() && !N.hasOneUse()) {
    // Compute R + (R << [1,2,3]) and reuse it.
    Base = N;
    return false;
  }

  // Look for (R + R) or (R + (R << [1,2,3])).
  unsigned ShAmt = 0;
  Base   = N.getOperand(0);
  OffReg = N.getOperand(1);

  // Swap if it is ((R << c) + R).
  ARM_AM::ShiftOpc ShOpcVal = ARM_AM::getShiftOpcForNode(OffReg);
  if (ShOpcVal != ARM_AM::lsl) {
    ShOpcVal = ARM_AM::getShiftOpcForNode(Base);
    if (ShOpcVal == ARM_AM::lsl)
      std::swap(Base, OffReg);
  }

  if (ShOpcVal == ARM_AM::lsl) {
    // Check to see if the RHS of the shift is a constant, if not, we can't fold
    // it.
    if (ConstantSDNode *Sh = dyn_cast<ConstantSDNode>(OffReg.getOperand(1))) {
      ShAmt = Sh->getZExtValue();
      if (ShAmt < 4 && isShifterOpProfitable(OffReg, ShOpcVal, ShAmt))
        OffReg = OffReg.getOperand(0);
      else {
        ShAmt = 0;
        ShOpcVal = ARM_AM::no_shift;
      }
    } else {
      ShOpcVal = ARM_AM::no_shift;
    }
  }

  ShImm = CurDAG->getTargetConstant(ShAmt, MVT::i32);

  return true;
}

//===--------------------------------------------------------------------===//

/// getAL - Returns a ARMCC::AL immediate node.
static inline SDValue getAL(SelectionDAG *CurDAG) {
  return CurDAG->getTargetConstant((uint64_t)ARMCC::AL, MVT::i32);
}

SDNode *ARMDAGToDAGISel::SelectARMIndexedLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  if (AM == ISD::UNINDEXED)
    return NULL;

  EVT LoadedVT = LD->getMemoryVT();
  SDValue Offset, AMOpc;
  bool isPre = (AM == ISD::PRE_INC) || (AM == ISD::PRE_DEC);
  unsigned Opcode = 0;
  bool Match = false;
  if (LoadedVT == MVT::i32 &&
      SelectAddrMode2Offset(N, LD->getOffset(), Offset, AMOpc)) {
    Opcode = isPre ? ARM::LDR_PRE : ARM::LDR_POST;
    Match = true;
  } else if (LoadedVT == MVT::i16 &&
             SelectAddrMode3Offset(N, LD->getOffset(), Offset, AMOpc)) {
    Match = true;
    Opcode = (LD->getExtensionType() == ISD::SEXTLOAD)
      ? (isPre ? ARM::LDRSH_PRE : ARM::LDRSH_POST)
      : (isPre ? ARM::LDRH_PRE : ARM::LDRH_POST);
  } else if (LoadedVT == MVT::i8 || LoadedVT == MVT::i1) {
    if (LD->getExtensionType() == ISD::SEXTLOAD) {
      if (SelectAddrMode3Offset(N, LD->getOffset(), Offset, AMOpc)) {
        Match = true;
        Opcode = isPre ? ARM::LDRSB_PRE : ARM::LDRSB_POST;
      }
    } else {
      if (SelectAddrMode2Offset(N, LD->getOffset(), Offset, AMOpc)) {
        Match = true;
        Opcode = isPre ? ARM::LDRB_PRE : ARM::LDRB_POST;
      }
    }
  }

  if (Match) {
    SDValue Chain = LD->getChain();
    SDValue Base = LD->getBasePtr();
    SDValue Ops[]= { Base, Offset, AMOpc, getAL(CurDAG),
                     CurDAG->getRegister(0, MVT::i32), Chain };
    return CurDAG->getMachineNode(Opcode, N->getDebugLoc(), MVT::i32, MVT::i32,
                                  MVT::Other, Ops, 6);
  }

  return NULL;
}

SDNode *ARMDAGToDAGISel::SelectT2IndexedLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  if (AM == ISD::UNINDEXED)
    return NULL;

  EVT LoadedVT = LD->getMemoryVT();
  bool isSExtLd = LD->getExtensionType() == ISD::SEXTLOAD;
  SDValue Offset;
  bool isPre = (AM == ISD::PRE_INC) || (AM == ISD::PRE_DEC);
  unsigned Opcode = 0;
  bool Match = false;
  if (SelectT2AddrModeImm8Offset(N, LD->getOffset(), Offset)) {
    switch (LoadedVT.getSimpleVT().SimpleTy) {
    case MVT::i32:
      Opcode = isPre ? ARM::t2LDR_PRE : ARM::t2LDR_POST;
      break;
    case MVT::i16:
      if (isSExtLd)
        Opcode = isPre ? ARM::t2LDRSH_PRE : ARM::t2LDRSH_POST;
      else
        Opcode = isPre ? ARM::t2LDRH_PRE : ARM::t2LDRH_POST;
      break;
    case MVT::i8:
    case MVT::i1:
      if (isSExtLd)
        Opcode = isPre ? ARM::t2LDRSB_PRE : ARM::t2LDRSB_POST;
      else
        Opcode = isPre ? ARM::t2LDRB_PRE : ARM::t2LDRB_POST;
      break;
    default:
      return NULL;
    }
    Match = true;
  }

  if (Match) {
    SDValue Chain = LD->getChain();
    SDValue Base = LD->getBasePtr();
    SDValue Ops[]= { Base, Offset, getAL(CurDAG),
                     CurDAG->getRegister(0, MVT::i32), Chain };
    return CurDAG->getMachineNode(Opcode, N->getDebugLoc(), MVT::i32, MVT::i32,
                                  MVT::Other, Ops, 5);
  }

  return NULL;
}

/// PairSRegs - Form a D register from a pair of S registers.
///
SDNode *ARMDAGToDAGISel::PairSRegs(EVT VT, SDValue V0, SDValue V1) {
  DebugLoc dl = V0.getNode()->getDebugLoc();
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::ssub_0, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::ssub_1, MVT::i32);
  const SDValue Ops[] = { V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops, 4);
}

/// PairDRegs - Form a quad register from a pair of D registers.
///
SDNode *ARMDAGToDAGISel::PairDRegs(EVT VT, SDValue V0, SDValue V1) {
  DebugLoc dl = V0.getNode()->getDebugLoc();
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::dsub_0, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::dsub_1, MVT::i32);
  const SDValue Ops[] = { V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops, 4);
}

/// PairQRegs - Form 4 consecutive D registers from a pair of Q registers.
///
SDNode *ARMDAGToDAGISel::PairQRegs(EVT VT, SDValue V0, SDValue V1) {
  DebugLoc dl = V0.getNode()->getDebugLoc();
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::qsub_0, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::qsub_1, MVT::i32);
  const SDValue Ops[] = { V0, SubReg0, V1, SubReg1 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops, 4);
}

/// QuadSRegs - Form 4 consecutive S registers.
///
SDNode *ARMDAGToDAGISel::QuadSRegs(EVT VT, SDValue V0, SDValue V1,
                                   SDValue V2, SDValue V3) {
  DebugLoc dl = V0.getNode()->getDebugLoc();
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::ssub_0, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::ssub_1, MVT::i32);
  SDValue SubReg2 = CurDAG->getTargetConstant(ARM::ssub_2, MVT::i32);
  SDValue SubReg3 = CurDAG->getTargetConstant(ARM::ssub_3, MVT::i32);
  const SDValue Ops[] = { V0, SubReg0, V1, SubReg1, V2, SubReg2, V3, SubReg3 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops, 8);
}

/// QuadDRegs - Form 4 consecutive D registers.
///
SDNode *ARMDAGToDAGISel::QuadDRegs(EVT VT, SDValue V0, SDValue V1,
                                   SDValue V2, SDValue V3) {
  DebugLoc dl = V0.getNode()->getDebugLoc();
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::dsub_0, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::dsub_1, MVT::i32);
  SDValue SubReg2 = CurDAG->getTargetConstant(ARM::dsub_2, MVT::i32);
  SDValue SubReg3 = CurDAG->getTargetConstant(ARM::dsub_3, MVT::i32);
  const SDValue Ops[] = { V0, SubReg0, V1, SubReg1, V2, SubReg2, V3, SubReg3 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops, 8);
}

/// QuadQRegs - Form 4 consecutive Q registers.
///
SDNode *ARMDAGToDAGISel::QuadQRegs(EVT VT, SDValue V0, SDValue V1,
                                   SDValue V2, SDValue V3) {
  DebugLoc dl = V0.getNode()->getDebugLoc();
  SDValue SubReg0 = CurDAG->getTargetConstant(ARM::qsub_0, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(ARM::qsub_1, MVT::i32);
  SDValue SubReg2 = CurDAG->getTargetConstant(ARM::qsub_2, MVT::i32);
  SDValue SubReg3 = CurDAG->getTargetConstant(ARM::qsub_3, MVT::i32);
  const SDValue Ops[] = { V0, SubReg0, V1, SubReg1, V2, SubReg2, V3, SubReg3 };
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops, 8);
}

/// GetVLDSTAlign - Get the alignment (in bytes) for the alignment operand
/// of a NEON VLD or VST instruction.  The supported values depend on the
/// number of registers being loaded.
SDValue ARMDAGToDAGISel::GetVLDSTAlign(SDValue Align, unsigned NumVecs,
                                       bool is64BitVector) {
  unsigned NumRegs = NumVecs;
  if (!is64BitVector && NumVecs < 3)
    NumRegs *= 2;

  unsigned Alignment = cast<ConstantSDNode>(Align)->getZExtValue();
  if (Alignment >= 32 && NumRegs == 4)
    Alignment = 32;
  else if (Alignment >= 16 && (NumRegs == 2 || NumRegs == 4))
    Alignment = 16;
  else if (Alignment >= 8)
    Alignment = 8;
  else
    Alignment = 0;

  return CurDAG->getTargetConstant(Alignment, MVT::i32);
}

SDNode *ARMDAGToDAGISel::SelectVLD(SDNode *N, unsigned NumVecs,
                                   unsigned *DOpcodes, unsigned *QOpcodes0,
                                   unsigned *QOpcodes1) {
  assert(NumVecs >= 1 && NumVecs <= 4 && "VLD NumVecs out-of-range");
  DebugLoc dl = N->getDebugLoc();

  SDValue MemAddr, Align;
  if (!SelectAddrMode6(N, N->getOperand(2), MemAddr, Align))
    return NULL;

  SDValue Chain = N->getOperand(0);
  EVT VT = N->getValueType(0);
  bool is64BitVector = VT.is64BitVector();
  Align = GetVLDSTAlign(Align, NumVecs, is64BitVector);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vld type");
    // Double-register operations:
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
  case MVT::v1i64: OpcodeIndex = 3; break;
    // Quad-register operations:
  case MVT::v16i8: OpcodeIndex = 0; break;
  case MVT::v8i16: OpcodeIndex = 1; break;
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 2; break;
  case MVT::v2i64: OpcodeIndex = 3;
    assert(NumVecs == 1 && "v2i64 type only supported for VLD1");
    break;
  }

  EVT ResTy;
  if (NumVecs == 1)
    ResTy = VT;
  else {
    unsigned ResTyElts = (NumVecs == 3) ? 4 : NumVecs;
    if (!is64BitVector)
      ResTyElts *= 2;
    ResTy = EVT::getVectorVT(*CurDAG->getContext(), MVT::i64, ResTyElts);
  }

  SDValue Pred = getAL(CurDAG);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
  SDValue SuperReg;
  if (is64BitVector) {
    unsigned Opc = DOpcodes[OpcodeIndex];
    const SDValue Ops[] = { MemAddr, Align, Pred, Reg0, Chain };
    SDNode *VLd = CurDAG->getMachineNode(Opc, dl, ResTy, MVT::Other, Ops, 5);
    if (NumVecs == 1)
      return VLd;

    SuperReg = SDValue(VLd, 0);
    assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
    for (unsigned Vec = 0; Vec < NumVecs; ++Vec) {
      SDValue D = CurDAG->getTargetExtractSubreg(ARM::dsub_0+Vec,
                                                 dl, VT, SuperReg);
      ReplaceUses(SDValue(N, Vec), D);
    }
    ReplaceUses(SDValue(N, NumVecs), SDValue(VLd, 1));
    return NULL;
  }

  if (NumVecs <= 2) {
    // Quad registers are directly supported for VLD1 and VLD2,
    // loading pairs of D regs.
    unsigned Opc = QOpcodes0[OpcodeIndex];
    const SDValue Ops[] = { MemAddr, Align, Pred, Reg0, Chain };
    SDNode *VLd = CurDAG->getMachineNode(Opc, dl, ResTy, MVT::Other, Ops, 5);
    if (NumVecs == 1)
      return VLd;

    SuperReg = SDValue(VLd, 0);
    Chain = SDValue(VLd, 1);

  } else {
    // Otherwise, quad registers are loaded with two separate instructions,
    // where one loads the even registers and the other loads the odd registers.
    EVT AddrTy = MemAddr.getValueType();

    // Load the even subregs.
    unsigned Opc = QOpcodes0[OpcodeIndex];
    SDValue ImplDef =
      SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, ResTy), 0);
    const SDValue OpsA[] = { MemAddr, Align, Reg0, ImplDef, Pred, Reg0, Chain };
    SDNode *VLdA =
      CurDAG->getMachineNode(Opc, dl, ResTy, AddrTy, MVT::Other, OpsA, 7);
    Chain = SDValue(VLdA, 2);

    // Load the odd subregs.
    Opc = QOpcodes1[OpcodeIndex];
    const SDValue OpsB[] = { SDValue(VLdA, 1), Align, Reg0, SDValue(VLdA, 0),
                             Pred, Reg0, Chain };
    SDNode *VLdB =
      CurDAG->getMachineNode(Opc, dl, ResTy, AddrTy, MVT::Other, OpsB, 7);
    SuperReg = SDValue(VLdB, 0);
    Chain = SDValue(VLdB, 2);
  }

  // Extract out the Q registers.
  assert(ARM::qsub_3 == ARM::qsub_0+3 && "Unexpected subreg numbering");
  for (unsigned Vec = 0; Vec < NumVecs; ++Vec) {
    SDValue Q = CurDAG->getTargetExtractSubreg(ARM::qsub_0+Vec,
                                               dl, VT, SuperReg);
    ReplaceUses(SDValue(N, Vec), Q);
  }
  ReplaceUses(SDValue(N, NumVecs), Chain);
  return NULL;
}

SDNode *ARMDAGToDAGISel::SelectVST(SDNode *N, unsigned NumVecs,
                                   unsigned *DOpcodes, unsigned *QOpcodes0,
                                   unsigned *QOpcodes1) {
  assert(NumVecs >= 1 && NumVecs <= 4 && "VST NumVecs out-of-range");
  DebugLoc dl = N->getDebugLoc();

  SDValue MemAddr, Align;
  if (!SelectAddrMode6(N, N->getOperand(2), MemAddr, Align))
    return NULL;

  SDValue Chain = N->getOperand(0);
  EVT VT = N->getOperand(3).getValueType();
  bool is64BitVector = VT.is64BitVector();
  Align = GetVLDSTAlign(Align, NumVecs, is64BitVector);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vst type");
    // Double-register operations:
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
  case MVT::v1i64: OpcodeIndex = 3; break;
    // Quad-register operations:
  case MVT::v16i8: OpcodeIndex = 0; break;
  case MVT::v8i16: OpcodeIndex = 1; break;
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 2; break;
  case MVT::v2i64: OpcodeIndex = 3;
    assert(NumVecs == 1 && "v2i64 type only supported for VST1");
    break;
  }

  SDValue Pred = getAL(CurDAG);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);

  SmallVector<SDValue, 7> Ops;
  Ops.push_back(MemAddr);
  Ops.push_back(Align);

  if (is64BitVector) {
    if (NumVecs == 1) {
      Ops.push_back(N->getOperand(3));
    } else {
      SDValue RegSeq;
      SDValue V0 = N->getOperand(0+3);
      SDValue V1 = N->getOperand(1+3);

      // Form a REG_SEQUENCE to force register allocation.
      if (NumVecs == 2)
        RegSeq = SDValue(PairDRegs(MVT::v2i64, V0, V1), 0);
      else {
        SDValue V2 = N->getOperand(2+3);
        // If it's a vld3, form a quad D-register and leave the last part as
        // an undef.
        SDValue V3 = (NumVecs == 3)
          ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF,dl,VT), 0)
          : N->getOperand(3+3);
        RegSeq = SDValue(QuadDRegs(MVT::v4i64, V0, V1, V2, V3), 0);
      }
      Ops.push_back(RegSeq);
    }
    Ops.push_back(Pred);
    Ops.push_back(Reg0); // predicate register
    Ops.push_back(Chain);
    unsigned Opc = DOpcodes[OpcodeIndex];
    return CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops.data(), 6);
  }

  if (NumVecs <= 2) {
    // Quad registers are directly supported for VST1 and VST2.
    unsigned Opc = QOpcodes0[OpcodeIndex];
    if (NumVecs == 1) {
      Ops.push_back(N->getOperand(3));
    } else {
      // Form a QQ register.
      SDValue Q0 = N->getOperand(3);
      SDValue Q1 = N->getOperand(4);
      Ops.push_back(SDValue(PairQRegs(MVT::v4i64, Q0, Q1), 0));
    }
    Ops.push_back(Pred);
    Ops.push_back(Reg0); // predicate register
    Ops.push_back(Chain);
    return CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops.data(), 6);
  }

  // Otherwise, quad registers are stored with two separate instructions,
  // where one stores the even registers and the other stores the odd registers.

  // Form the QQQQ REG_SEQUENCE.
  SDValue V0 = N->getOperand(0+3);
  SDValue V1 = N->getOperand(1+3);
  SDValue V2 = N->getOperand(2+3);
  SDValue V3 = (NumVecs == 3)
    ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, VT), 0)
    : N->getOperand(3+3);
  SDValue RegSeq = SDValue(QuadQRegs(MVT::v8i64, V0, V1, V2, V3), 0);

  // Store the even D registers.
  Ops.push_back(Reg0); // post-access address offset
  Ops.push_back(RegSeq);
  Ops.push_back(Pred);
  Ops.push_back(Reg0); // predicate register
  Ops.push_back(Chain);
  unsigned Opc = QOpcodes0[OpcodeIndex];
  SDNode *VStA = CurDAG->getMachineNode(Opc, dl, MemAddr.getValueType(),
                                        MVT::Other, Ops.data(), 7);
  Chain = SDValue(VStA, 1);

  // Store the odd D registers.
  Ops[0] = SDValue(VStA, 0); // MemAddr
  Ops[6] = Chain;
  Opc = QOpcodes1[OpcodeIndex];
  SDNode *VStB = CurDAG->getMachineNode(Opc, dl, MemAddr.getValueType(),
                                        MVT::Other, Ops.data(), 7);
  Chain = SDValue(VStB, 1);
  ReplaceUses(SDValue(N, 0), Chain);
  return NULL;
}

SDNode *ARMDAGToDAGISel::SelectVLDSTLane(SDNode *N, bool IsLoad,
                                         unsigned NumVecs, unsigned *DOpcodes,
                                         unsigned *QOpcodes) {
  assert(NumVecs >=2 && NumVecs <= 4 && "VLDSTLane NumVecs out-of-range");
  DebugLoc dl = N->getDebugLoc();

  SDValue MemAddr, Align;
  if (!SelectAddrMode6(N, N->getOperand(2), MemAddr, Align))
    return NULL;

  SDValue Chain = N->getOperand(0);
  unsigned Lane =
    cast<ConstantSDNode>(N->getOperand(NumVecs+3))->getZExtValue();
  EVT VT = IsLoad ? N->getValueType(0) : N->getOperand(3).getValueType();
  bool is64BitVector = VT.is64BitVector();

  unsigned Alignment = 0;
  if (NumVecs != 3) {
    Alignment = cast<ConstantSDNode>(Align)->getZExtValue();
    unsigned NumBytes = NumVecs * VT.getVectorElementType().getSizeInBits()/8;
    if (Alignment > NumBytes)
      Alignment = NumBytes;
    if (Alignment < 8 && Alignment < NumBytes)
      Alignment = 0;
    // Alignment must be a power of two; make sure of that.
    Alignment = (Alignment & -Alignment);
    if (Alignment == 1)
      Alignment = 0;
  }
  Align = CurDAG->getTargetConstant(Alignment, MVT::i32);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vld/vst lane type");
    // Double-register operations:
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
    // Quad-register operations:
  case MVT::v8i16: OpcodeIndex = 0; break;
  case MVT::v4f32:
  case MVT::v4i32: OpcodeIndex = 1; break;
  }

  SDValue Pred = getAL(CurDAG);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);

  SmallVector<SDValue, 7> Ops;
  Ops.push_back(MemAddr);
  Ops.push_back(Align);

  unsigned Opc = (is64BitVector ? DOpcodes[OpcodeIndex] :
                                  QOpcodes[OpcodeIndex]);

  SDValue SuperReg;
  SDValue V0 = N->getOperand(0+3);
  SDValue V1 = N->getOperand(1+3);
  if (NumVecs == 2) {
    if (is64BitVector)
      SuperReg = SDValue(PairDRegs(MVT::v2i64, V0, V1), 0);
    else
      SuperReg = SDValue(PairQRegs(MVT::v4i64, V0, V1), 0);
  } else {
    SDValue V2 = N->getOperand(2+3);
    SDValue V3 = (NumVecs == 3)
      ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF,dl,VT), 0)
      : N->getOperand(3+3);
    if (is64BitVector)
      SuperReg = SDValue(QuadDRegs(MVT::v4i64, V0, V1, V2, V3), 0);
    else
      SuperReg = SDValue(QuadQRegs(MVT::v8i64, V0, V1, V2, V3), 0);
  }
  Ops.push_back(SuperReg);
  Ops.push_back(getI32Imm(Lane));
  Ops.push_back(Pred);
  Ops.push_back(Reg0);
  Ops.push_back(Chain);

  if (!IsLoad)
    return CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops.data(), 7);

  EVT ResTy;
  unsigned ResTyElts = (NumVecs == 3) ? 4 : NumVecs;
  if (!is64BitVector)
    ResTyElts *= 2;
  ResTy = EVT::getVectorVT(*CurDAG->getContext(), MVT::i64, ResTyElts);

  SDNode *VLdLn = CurDAG->getMachineNode(Opc, dl, ResTy, MVT::Other,
                                         Ops.data(), 7);
  SuperReg = SDValue(VLdLn, 0);
  Chain = SDValue(VLdLn, 1);

  // Extract the subregisters.
  assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
  assert(ARM::qsub_3 == ARM::qsub_0+3 && "Unexpected subreg numbering");
  unsigned SubIdx = is64BitVector ? ARM::dsub_0 : ARM::qsub_0;
  for (unsigned Vec = 0; Vec < NumVecs; ++Vec)
    ReplaceUses(SDValue(N, Vec),
                CurDAG->getTargetExtractSubreg(SubIdx+Vec, dl, VT, SuperReg));
  ReplaceUses(SDValue(N, NumVecs), Chain);
  return NULL;
}

SDNode *ARMDAGToDAGISel::SelectVLDDup(SDNode *N, unsigned NumVecs,
                                      unsigned *Opcodes) {
  assert(NumVecs >=2 && NumVecs <= 4 && "VLDDup NumVecs out-of-range");
  DebugLoc dl = N->getDebugLoc();

  SDValue MemAddr, Align;
  if (!SelectAddrMode6(N, N->getOperand(1), MemAddr, Align))
    return NULL;

  SDValue Chain = N->getOperand(0);
  EVT VT = N->getValueType(0);

  unsigned Alignment = 0;
  if (NumVecs != 3) {
    Alignment = cast<ConstantSDNode>(Align)->getZExtValue();
    unsigned NumBytes = NumVecs * VT.getVectorElementType().getSizeInBits()/8;
    if (Alignment > NumBytes)
      Alignment = NumBytes;
    if (Alignment < 8 && Alignment < NumBytes)
      Alignment = 0;
    // Alignment must be a power of two; make sure of that.
    Alignment = (Alignment & -Alignment);
    if (Alignment == 1)
      Alignment = 0;
  }
  Align = CurDAG->getTargetConstant(Alignment, MVT::i32);

  unsigned OpcodeIndex;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("unhandled vld-dup type");
  case MVT::v8i8:  OpcodeIndex = 0; break;
  case MVT::v4i16: OpcodeIndex = 1; break;
  case MVT::v2f32:
  case MVT::v2i32: OpcodeIndex = 2; break;
  }

  SDValue Pred = getAL(CurDAG);
  SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
  SDValue SuperReg;
  unsigned Opc = Opcodes[OpcodeIndex];
  const SDValue Ops[] = { MemAddr, Align, Pred, Reg0, Chain };

  unsigned ResTyElts = (NumVecs == 3) ? 4 : NumVecs;
  EVT ResTy = EVT::getVectorVT(*CurDAG->getContext(), MVT::i64, ResTyElts);
  SDNode *VLdDup = CurDAG->getMachineNode(Opc, dl, ResTy, MVT::Other, Ops, 5);
  SuperReg = SDValue(VLdDup, 0);
  Chain = SDValue(VLdDup, 1);

  // Extract the subregisters.
  assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
  unsigned SubIdx = ARM::dsub_0;
  for (unsigned Vec = 0; Vec < NumVecs; ++Vec)
    ReplaceUses(SDValue(N, Vec),
                CurDAG->getTargetExtractSubreg(SubIdx+Vec, dl, VT, SuperReg));
  ReplaceUses(SDValue(N, NumVecs), Chain);
  return NULL;
}

SDNode *ARMDAGToDAGISel::SelectVTBL(SDNode *N, bool IsExt, unsigned NumVecs,
                                    unsigned Opc) {
  assert(NumVecs >= 2 && NumVecs <= 4 && "VTBL NumVecs out-of-range");
  DebugLoc dl = N->getDebugLoc();
  EVT VT = N->getValueType(0);
  unsigned FirstTblReg = IsExt ? 2 : 1;

  // Form a REG_SEQUENCE to force register allocation.
  SDValue RegSeq;
  SDValue V0 = N->getOperand(FirstTblReg + 0);
  SDValue V1 = N->getOperand(FirstTblReg + 1);
  if (NumVecs == 2)
    RegSeq = SDValue(PairDRegs(MVT::v16i8, V0, V1), 0);
  else {
    SDValue V2 = N->getOperand(FirstTblReg + 2);
    // If it's a vtbl3, form a quad D-register and leave the last part as
    // an undef.
    SDValue V3 = (NumVecs == 3)
      ? SDValue(CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, VT), 0)
      : N->getOperand(FirstTblReg + 3);
    RegSeq = SDValue(QuadDRegs(MVT::v4i64, V0, V1, V2, V3), 0);
  }

  SmallVector<SDValue, 6> Ops;
  if (IsExt)
    Ops.push_back(N->getOperand(1));
  Ops.push_back(RegSeq);
  Ops.push_back(N->getOperand(FirstTblReg + NumVecs));
  Ops.push_back(getAL(CurDAG)); // predicate
  Ops.push_back(CurDAG->getRegister(0, MVT::i32)); // predicate register
  return CurDAG->getMachineNode(Opc, dl, VT, Ops.data(), Ops.size());
}

SDNode *ARMDAGToDAGISel::SelectV6T2BitfieldExtractOp(SDNode *N,
                                                     bool isSigned) {
  if (!Subtarget->hasV6T2Ops())
    return NULL;

  unsigned Opc = isSigned ? (Subtarget->isThumb() ? ARM::t2SBFX : ARM::SBFX)
    : (Subtarget->isThumb() ? ARM::t2UBFX : ARM::UBFX);


  // For unsigned extracts, check for a shift right and mask
  unsigned And_imm = 0;
  if (N->getOpcode() == ISD::AND) {
    if (isOpcWithIntImmediate(N, ISD::AND, And_imm)) {

      // The immediate is a mask of the low bits iff imm & (imm+1) == 0
      if (And_imm & (And_imm + 1))
        return NULL;

      unsigned Srl_imm = 0;
      if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SRL,
                                Srl_imm)) {
        assert(Srl_imm > 0 && Srl_imm < 32 && "bad amount in shift node!");

        unsigned Width = CountTrailingOnes_32(And_imm);
        unsigned LSB = Srl_imm;
        SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
        SDValue Ops[] = { N->getOperand(0).getOperand(0),
                          CurDAG->getTargetConstant(LSB, MVT::i32),
                          CurDAG->getTargetConstant(Width, MVT::i32),
          getAL(CurDAG), Reg0 };
        return CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops, 5);
      }
    }
    return NULL;
  }

  // Otherwise, we're looking for a shift of a shift
  unsigned Shl_imm = 0;
  if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SHL, Shl_imm)) {
    assert(Shl_imm > 0 && Shl_imm < 32 && "bad amount in shift node!");
    unsigned Srl_imm = 0;
    if (isInt32Immediate(N->getOperand(1), Srl_imm)) {
      assert(Srl_imm > 0 && Srl_imm < 32 && "bad amount in shift node!");
      unsigned Width = 32 - Srl_imm;
      int LSB = Srl_imm - Shl_imm;
      if (LSB < 0)
        return NULL;
      SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
      SDValue Ops[] = { N->getOperand(0).getOperand(0),
                        CurDAG->getTargetConstant(LSB, MVT::i32),
                        CurDAG->getTargetConstant(Width, MVT::i32),
                        getAL(CurDAG), Reg0 };
      return CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops, 5);
    }
  }
  return NULL;
}

SDNode *ARMDAGToDAGISel::
SelectT2CMOVShiftOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                    ARMCC::CondCodes CCVal, SDValue CCR, SDValue InFlag) {
  SDValue CPTmp0;
  SDValue CPTmp1;
  if (SelectT2ShifterOperandReg(TrueVal, CPTmp0, CPTmp1)) {
    unsigned SOVal = cast<ConstantSDNode>(CPTmp1)->getZExtValue();
    unsigned SOShOp = ARM_AM::getSORegShOp(SOVal);
    unsigned Opc = 0;
    switch (SOShOp) {
    case ARM_AM::lsl: Opc = ARM::t2MOVCClsl; break;
    case ARM_AM::lsr: Opc = ARM::t2MOVCClsr; break;
    case ARM_AM::asr: Opc = ARM::t2MOVCCasr; break;
    case ARM_AM::ror: Opc = ARM::t2MOVCCror; break;
    default:
      llvm_unreachable("Unknown so_reg opcode!");
      break;
    }
    SDValue SOShImm =
      CurDAG->getTargetConstant(ARM_AM::getSORegOffset(SOVal), MVT::i32);
    SDValue CC = CurDAG->getTargetConstant(CCVal, MVT::i32);
    SDValue Ops[] = { FalseVal, CPTmp0, SOShImm, CC, CCR, InFlag };
    return CurDAG->SelectNodeTo(N, Opc, MVT::i32,Ops, 6);
  }
  return 0;
}

SDNode *ARMDAGToDAGISel::
SelectARMCMOVShiftOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                     ARMCC::CondCodes CCVal, SDValue CCR, SDValue InFlag) {
  SDValue CPTmp0;
  SDValue CPTmp1;
  SDValue CPTmp2;
  if (SelectShifterOperandReg(TrueVal, CPTmp0, CPTmp1, CPTmp2)) {
    SDValue CC = CurDAG->getTargetConstant(CCVal, MVT::i32);
    SDValue Ops[] = { FalseVal, CPTmp0, CPTmp1, CPTmp2, CC, CCR, InFlag };
    return CurDAG->SelectNodeTo(N, ARM::MOVCCs, MVT::i32, Ops, 7);
  }
  return 0;
}

SDNode *ARMDAGToDAGISel::
SelectT2CMOVImmOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                  ARMCC::CondCodes CCVal, SDValue CCR, SDValue InFlag) {
  ConstantSDNode *T = dyn_cast<ConstantSDNode>(TrueVal);
  if (!T)
    return 0;

  unsigned Opc = 0;
  unsigned TrueImm = T->getZExtValue();
  if (is_t2_so_imm(TrueImm)) {
    Opc = ARM::t2MOVCCi;
  } else if (TrueImm <= 0xffff) {
    Opc = ARM::t2MOVCCi16;
  } else if (is_t2_so_imm_not(TrueImm)) {
    TrueImm = ~TrueImm;
    Opc = ARM::t2MVNCCi;
  } else if (TrueVal.getNode()->hasOneUse() && Subtarget->hasV6T2Ops()) {
    // Large immediate.
    Opc = ARM::t2MOVCCi32imm;
  }

  if (Opc) {
    SDValue True = CurDAG->getTargetConstant(TrueImm, MVT::i32);
    SDValue CC = CurDAG->getTargetConstant(CCVal, MVT::i32);
    SDValue Ops[] = { FalseVal, True, CC, CCR, InFlag };
    return CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops, 5);
  }

  return 0;
}

SDNode *ARMDAGToDAGISel::
SelectARMCMOVImmOp(SDNode *N, SDValue FalseVal, SDValue TrueVal,
                   ARMCC::CondCodes CCVal, SDValue CCR, SDValue InFlag) {
  ConstantSDNode *T = dyn_cast<ConstantSDNode>(TrueVal);
  if (!T)
    return 0;

  unsigned Opc = 0;
  unsigned TrueImm = T->getZExtValue();
  bool isSoImm = is_so_imm(TrueImm);
  if (isSoImm) {
    Opc = ARM::MOVCCi;
  } else if (Subtarget->hasV6T2Ops() && TrueImm <= 0xffff) {
    Opc = ARM::MOVCCi16;
  } else if (is_so_imm_not(TrueImm)) {
    TrueImm = ~TrueImm;
    Opc = ARM::MVNCCi;
  } else if (TrueVal.getNode()->hasOneUse() &&
             (Subtarget->hasV6T2Ops() || ARM_AM::isSOImmTwoPartVal(TrueImm))) {
    // Large immediate.
    Opc = ARM::MOVCCi32imm;
  }

  if (Opc) {
    SDValue True = CurDAG->getTargetConstant(TrueImm, MVT::i32);
    SDValue CC = CurDAG->getTargetConstant(CCVal, MVT::i32);
    SDValue Ops[] = { FalseVal, True, CC, CCR, InFlag };
    return CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops, 5);
  }

  return 0;
}

SDNode *ARMDAGToDAGISel::SelectCMOVOp(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue FalseVal = N->getOperand(0);
  SDValue TrueVal  = N->getOperand(1);
  SDValue CC = N->getOperand(2);
  SDValue CCR = N->getOperand(3);
  SDValue InFlag = N->getOperand(4);
  assert(CC.getOpcode() == ISD::Constant);
  assert(CCR.getOpcode() == ISD::Register);
  ARMCC::CondCodes CCVal =
    (ARMCC::CondCodes)cast<ConstantSDNode>(CC)->getZExtValue();

  if (!Subtarget->isThumb1Only() && VT == MVT::i32) {
    // Pattern: (ARMcmov:i32 GPR:i32:$false, so_reg:i32:$true, (imm:i32):$cc)
    // Emits: (MOVCCs:i32 GPR:i32:$false, so_reg:i32:$true, (imm:i32):$cc)
    // Pattern complexity = 18  cost = 1  size = 0
    SDValue CPTmp0;
    SDValue CPTmp1;
    SDValue CPTmp2;
    if (Subtarget->isThumb()) {
      SDNode *Res = SelectT2CMOVShiftOp(N, FalseVal, TrueVal,
                                        CCVal, CCR, InFlag);
      if (!Res)
        Res = SelectT2CMOVShiftOp(N, TrueVal, FalseVal,
                               ARMCC::getOppositeCondition(CCVal), CCR, InFlag);
      if (Res)
        return Res;
    } else {
      SDNode *Res = SelectARMCMOVShiftOp(N, FalseVal, TrueVal,
                                         CCVal, CCR, InFlag);
      if (!Res)
        Res = SelectARMCMOVShiftOp(N, TrueVal, FalseVal,
                               ARMCC::getOppositeCondition(CCVal), CCR, InFlag);
      if (Res)
        return Res;
    }

    // Pattern: (ARMcmov:i32 GPR:i32:$false,
    //             (imm:i32)<<P:Pred_so_imm>>:$true,
    //             (imm:i32):$cc)
    // Emits: (MOVCCi:i32 GPR:i32:$false,
    //           (so_imm:i32 (imm:i32):$true), (imm:i32):$cc)
    // Pattern complexity = 10  cost = 1  size = 0
    if (Subtarget->isThumb()) {
      SDNode *Res = SelectT2CMOVImmOp(N, FalseVal, TrueVal,
                                        CCVal, CCR, InFlag);
      if (!Res)
        Res = SelectT2CMOVImmOp(N, TrueVal, FalseVal,
                               ARMCC::getOppositeCondition(CCVal), CCR, InFlag);
      if (Res)
        return Res;
    } else {
      SDNode *Res = SelectARMCMOVImmOp(N, FalseVal, TrueVal,
                                         CCVal, CCR, InFlag);
      if (!Res)
        Res = SelectARMCMOVImmOp(N, TrueVal, FalseVal,
                               ARMCC::getOppositeCondition(CCVal), CCR, InFlag);
      if (Res)
        return Res;
    }
  }

  // Pattern: (ARMcmov:i32 GPR:i32:$false, GPR:i32:$true, (imm:i32):$cc)
  // Emits: (MOVCCr:i32 GPR:i32:$false, GPR:i32:$true, (imm:i32):$cc)
  // Pattern complexity = 6  cost = 1  size = 0
  //
  // Pattern: (ARMcmov:i32 GPR:i32:$false, GPR:i32:$true, (imm:i32):$cc)
  // Emits: (tMOVCCr:i32 GPR:i32:$false, GPR:i32:$true, (imm:i32):$cc)
  // Pattern complexity = 6  cost = 11  size = 0
  //
  // Also FCPYScc and FCPYDcc.
  SDValue Tmp2 = CurDAG->getTargetConstant(CCVal, MVT::i32);
  SDValue Ops[] = { FalseVal, TrueVal, Tmp2, CCR, InFlag };
  unsigned Opc = 0;
  switch (VT.getSimpleVT().SimpleTy) {
  default: assert(false && "Illegal conditional move type!");
    break;
  case MVT::i32:
    Opc = Subtarget->isThumb()
      ? (Subtarget->hasThumb2() ? ARM::t2MOVCCr : ARM::tMOVCCr_pseudo)
      : ARM::MOVCCr;
    break;
  case MVT::f32:
    Opc = ARM::VMOVScc;
    break;
  case MVT::f64:
    Opc = ARM::VMOVDcc;
    break;
  }
  return CurDAG->SelectNodeTo(N, Opc, VT, Ops, 5);
}

SDNode *ARMDAGToDAGISel::SelectConcatVector(SDNode *N) {
  // The only time a CONCAT_VECTORS operation can have legal types is when
  // two 64-bit vectors are concatenated to a 128-bit vector.
  EVT VT = N->getValueType(0);
  if (!VT.is128BitVector() || N->getNumOperands() != 2)
    llvm_unreachable("unexpected CONCAT_VECTORS");
  return PairDRegs(VT, N->getOperand(0), N->getOperand(1));
}

SDNode *ARMDAGToDAGISel::Select(SDNode *N) {
  DebugLoc dl = N->getDebugLoc();

  if (N->isMachineOpcode())
    return NULL;   // Already selected.

  switch (N->getOpcode()) {
  default: break;
  case ISD::Constant: {
    unsigned Val = cast<ConstantSDNode>(N)->getZExtValue();
    bool UseCP = true;
    if (Subtarget->hasThumb2())
      // Thumb2-aware targets have the MOVT instruction, so all immediates can
      // be done with MOV + MOVT, at worst.
      UseCP = 0;
    else {
      if (Subtarget->isThumb()) {
        UseCP = (Val > 255 &&                          // MOV
                 ~Val > 255 &&                         // MOV + MVN
                 !ARM_AM::isThumbImmShiftedVal(Val));  // MOV + LSL
      } else
        UseCP = (ARM_AM::getSOImmVal(Val) == -1 &&     // MOV
                 ARM_AM::getSOImmVal(~Val) == -1 &&    // MVN
                 !ARM_AM::isSOImmTwoPartVal(Val));     // two instrs.
    }

    if (Subtarget->isTargetNaCl() && (!FlagSfiEnableCP)) 
      UseCP = false; // @LOCALMOD

    if (UseCP) {
      SDValue CPIdx =
        CurDAG->getTargetConstantPool(ConstantInt::get(
                                  Type::getInt32Ty(*CurDAG->getContext()), Val),
                                      TLI.getPointerTy());

      SDNode *ResNode;
      if (Subtarget->isThumb1Only()) {
        SDValue Pred = getAL(CurDAG);
        SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
        SDValue Ops[] = { CPIdx, Pred, PredReg, CurDAG->getEntryNode() };
        ResNode = CurDAG->getMachineNode(ARM::tLDRpci, dl, MVT::i32, MVT::Other,
                                         Ops, 4);
      } else {
        SDValue Ops[] = {
          CPIdx,
          CurDAG->getTargetConstant(0, MVT::i32),
          getAL(CurDAG),
          CurDAG->getRegister(0, MVT::i32),
          CurDAG->getEntryNode()
        };
        ResNode=CurDAG->getMachineNode(ARM::LDRcp, dl, MVT::i32, MVT::Other,
                                       Ops, 5);
      }
      ReplaceUses(SDValue(N, 0), SDValue(ResNode, 0));
      return NULL;
    }

    // Other cases are autogenerated.
    break;
  }
  case ISD::FrameIndex: {
    // Selects to ADDri FI, 0 which in turn will become ADDri SP, imm.
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, TLI.getPointerTy());
    if (Subtarget->isThumb1Only()) {
      return CurDAG->SelectNodeTo(N, ARM::tADDrSPi, MVT::i32, TFI,
                                  CurDAG->getTargetConstant(0, MVT::i32));
    } else {
      unsigned Opc = ((Subtarget->isThumb() && Subtarget->hasThumb2()) ?
                      ARM::t2ADDri : ARM::ADDri);
      SDValue Ops[] = { TFI, CurDAG->getTargetConstant(0, MVT::i32),
                        getAL(CurDAG), CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      return CurDAG->SelectNodeTo(N, Opc, MVT::i32, Ops, 5);
    }
  }
  case ISD::SRL:
    if (SDNode *I = SelectV6T2BitfieldExtractOp(N, false))
      return I;
    break;
  case ISD::SRA:
    if (SDNode *I = SelectV6T2BitfieldExtractOp(N, true))
      return I;
    break;
  case ISD::MUL:
    if (Subtarget->isThumb1Only())
      break;
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
      unsigned RHSV = C->getZExtValue();
      if (!RHSV) break;
      if (isPowerOf2_32(RHSV-1)) {  // 2^n+1?
        unsigned ShImm = Log2_32(RHSV-1);
        if (ShImm >= 32)
          break;
        SDValue V = N->getOperand(0);
        ShImm = ARM_AM::getSORegOpc(ARM_AM::lsl, ShImm);
        SDValue ShImmOp = CurDAG->getTargetConstant(ShImm, MVT::i32);
        SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
        if (Subtarget->isThumb()) {
          SDValue Ops[] = { V, V, ShImmOp, getAL(CurDAG), Reg0, Reg0 };
          return CurDAG->SelectNodeTo(N, ARM::t2ADDrs, MVT::i32, Ops, 6);
        } else {
          SDValue Ops[] = { V, V, Reg0, ShImmOp, getAL(CurDAG), Reg0, Reg0 };
          return CurDAG->SelectNodeTo(N, ARM::ADDrs, MVT::i32, Ops, 7);
        }
      }
      if (isPowerOf2_32(RHSV+1)) {  // 2^n-1?
        unsigned ShImm = Log2_32(RHSV+1);
        if (ShImm >= 32)
          break;
        SDValue V = N->getOperand(0);
        ShImm = ARM_AM::getSORegOpc(ARM_AM::lsl, ShImm);
        SDValue ShImmOp = CurDAG->getTargetConstant(ShImm, MVT::i32);
        SDValue Reg0 = CurDAG->getRegister(0, MVT::i32);
        if (Subtarget->isThumb()) {
          SDValue Ops[] = { V, V, ShImmOp, getAL(CurDAG), Reg0, Reg0 };
          return CurDAG->SelectNodeTo(N, ARM::t2RSBrs, MVT::i32, Ops, 6);
        } else {
          SDValue Ops[] = { V, V, Reg0, ShImmOp, getAL(CurDAG), Reg0, Reg0 };
          return CurDAG->SelectNodeTo(N, ARM::RSBrs, MVT::i32, Ops, 7);
        }
      }
    }
    break;
  case ISD::AND: {
    // Check for unsigned bitfield extract
    if (SDNode *I = SelectV6T2BitfieldExtractOp(N, false))
      return I;

    // (and (or x, c2), c1) and top 16-bits of c1 and c2 match, lower 16-bits
    // of c1 are 0xffff, and lower 16-bit of c2 are 0. That is, the top 16-bits
    // are entirely contributed by c2 and lower 16-bits are entirely contributed
    // by x. That's equal to (or (and x, 0xffff), (and c1, 0xffff0000)).
    // Select it to: "movt x, ((c1 & 0xffff) >> 16)
    EVT VT = N->getValueType(0);
    if (VT != MVT::i32)
      break;
    unsigned Opc = (Subtarget->isThumb() && Subtarget->hasThumb2())
      ? ARM::t2MOVTi16
      : (Subtarget->hasV6T2Ops() ? ARM::MOVTi16 : 0);
    if (!Opc)
      break;
    SDValue N0 = N->getOperand(0), N1 = N->getOperand(1);
    ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
    if (!N1C)
      break;
    if (N0.getOpcode() == ISD::OR && N0.getNode()->hasOneUse()) {
      SDValue N2 = N0.getOperand(1);
      ConstantSDNode *N2C = dyn_cast<ConstantSDNode>(N2);
      if (!N2C)
        break;
      unsigned N1CVal = N1C->getZExtValue();
      unsigned N2CVal = N2C->getZExtValue();
      if ((N1CVal & 0xffff0000U) == (N2CVal & 0xffff0000U) &&
          (N1CVal & 0xffffU) == 0xffffU &&
          (N2CVal & 0xffffU) == 0x0U) {
        SDValue Imm16 = CurDAG->getTargetConstant((N2CVal & 0xFFFF0000U) >> 16,
                                                  MVT::i32);
        SDValue Ops[] = { N0.getOperand(0), Imm16,
                          getAL(CurDAG), CurDAG->getRegister(0, MVT::i32) };
        return CurDAG->getMachineNode(Opc, dl, VT, Ops, 4);
      }
    }
    break;
  }
  case ARMISD::VMOVRRD:
    return CurDAG->getMachineNode(ARM::VMOVRRD, dl, MVT::i32, MVT::i32,
                                  N->getOperand(0), getAL(CurDAG),
                                  CurDAG->getRegister(0, MVT::i32));
  case ISD::UMUL_LOHI: {
    if (Subtarget->isThumb1Only())
      break;
    if (Subtarget->isThumb()) {
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1),
                        getAL(CurDAG), CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      return CurDAG->getMachineNode(ARM::t2UMULL, dl, MVT::i32, MVT::i32,Ops,4);
    } else {
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1),
                        getAL(CurDAG), CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      return CurDAG->getMachineNode(Subtarget->hasV6Ops() ?
                                    ARM::UMULL : ARM::UMULLv5,
                                    dl, MVT::i32, MVT::i32, Ops, 5);
    }
  }
  case ISD::SMUL_LOHI: {
    if (Subtarget->isThumb1Only())
      break;
    if (Subtarget->isThumb()) {
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1),
                        getAL(CurDAG), CurDAG->getRegister(0, MVT::i32) };
      return CurDAG->getMachineNode(ARM::t2SMULL, dl, MVT::i32, MVT::i32,Ops,4);
    } else {
      SDValue Ops[] = { N->getOperand(0), N->getOperand(1),
                        getAL(CurDAG), CurDAG->getRegister(0, MVT::i32),
                        CurDAG->getRegister(0, MVT::i32) };
      return CurDAG->getMachineNode(Subtarget->hasV6Ops() ?
                                    ARM::SMULL : ARM::SMULLv5,
                                    dl, MVT::i32, MVT::i32, Ops, 5);
    }
  }
  case ISD::LOAD: {
    SDNode *ResNode = 0;
    if (Subtarget->isThumb() && Subtarget->hasThumb2())
      ResNode = SelectT2IndexedLoad(N);
    else
      ResNode = SelectARMIndexedLoad(N);
    if (ResNode)
      return ResNode;
    // Other cases are autogenerated.
    break;
  }
  case ARMISD::BRCOND: {
    // Pattern: (ARMbrcond:void (bb:Other):$dst, (imm:i32):$cc)
    // Emits: (Bcc:void (bb:Other):$dst, (imm:i32):$cc)
    // Pattern complexity = 6  cost = 1  size = 0

    // Pattern: (ARMbrcond:void (bb:Other):$dst, (imm:i32):$cc)
    // Emits: (tBcc:void (bb:Other):$dst, (imm:i32):$cc)
    // Pattern complexity = 6  cost = 1  size = 0

    // Pattern: (ARMbrcond:void (bb:Other):$dst, (imm:i32):$cc)
    // Emits: (t2Bcc:void (bb:Other):$dst, (imm:i32):$cc)
    // Pattern complexity = 6  cost = 1  size = 0

    unsigned Opc = Subtarget->isThumb() ?
      ((Subtarget->hasThumb2()) ? ARM::t2Bcc : ARM::tBcc) : ARM::Bcc;
    SDValue Chain = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    SDValue N2 = N->getOperand(2);
    SDValue N3 = N->getOperand(3);
    SDValue InFlag = N->getOperand(4);
    assert(N1.getOpcode() == ISD::BasicBlock);
    assert(N2.getOpcode() == ISD::Constant);
    assert(N3.getOpcode() == ISD::Register);

    SDValue Tmp2 = CurDAG->getTargetConstant(((unsigned)
                               cast<ConstantSDNode>(N2)->getZExtValue()),
                               MVT::i32);
    SDValue Ops[] = { N1, Tmp2, N3, Chain, InFlag };
    SDNode *ResNode = CurDAG->getMachineNode(Opc, dl, MVT::Other,
                                             MVT::Glue, Ops, 5);
    Chain = SDValue(ResNode, 0);
    if (N->getNumValues() == 2) {
      InFlag = SDValue(ResNode, 1);
      ReplaceUses(SDValue(N, 1), InFlag);
    }
    ReplaceUses(SDValue(N, 0),
                SDValue(Chain.getNode(), Chain.getResNo()));
    return NULL;
  }
  case ARMISD::CMOV:
    return SelectCMOVOp(N);
  case ARMISD::CNEG: {
    EVT VT = N->getValueType(0);
    SDValue N0 = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    SDValue N2 = N->getOperand(2);
    SDValue N3 = N->getOperand(3);
    SDValue InFlag = N->getOperand(4);
    assert(N2.getOpcode() == ISD::Constant);
    assert(N3.getOpcode() == ISD::Register);

    SDValue Tmp2 = CurDAG->getTargetConstant(((unsigned)
                               cast<ConstantSDNode>(N2)->getZExtValue()),
                               MVT::i32);
    SDValue Ops[] = { N0, N1, Tmp2, N3, InFlag };
    unsigned Opc = 0;
    switch (VT.getSimpleVT().SimpleTy) {
    default: assert(false && "Illegal conditional move type!");
      break;
    case MVT::f32:
      Opc = ARM::VNEGScc;
      break;
    case MVT::f64:
      Opc = ARM::VNEGDcc;
      break;
    }
    return CurDAG->SelectNodeTo(N, Opc, VT, Ops, 5);
  }

  case ARMISD::VZIP: {
    unsigned Opc = 0;
    EVT VT = N->getValueType(0);
    switch (VT.getSimpleVT().SimpleTy) {
    default: return NULL;
    case MVT::v8i8:  Opc = ARM::VZIPd8; break;
    case MVT::v4i16: Opc = ARM::VZIPd16; break;
    case MVT::v2f32:
    case MVT::v2i32: Opc = ARM::VZIPd32; break;
    case MVT::v16i8: Opc = ARM::VZIPq8; break;
    case MVT::v8i16: Opc = ARM::VZIPq16; break;
    case MVT::v4f32:
    case MVT::v4i32: Opc = ARM::VZIPq32; break;
    }
    SDValue Pred = getAL(CurDAG);
    SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1), Pred, PredReg };
    return CurDAG->getMachineNode(Opc, dl, VT, VT, Ops, 4);
  }
  case ARMISD::VUZP: {
    unsigned Opc = 0;
    EVT VT = N->getValueType(0);
    switch (VT.getSimpleVT().SimpleTy) {
    default: return NULL;
    case MVT::v8i8:  Opc = ARM::VUZPd8; break;
    case MVT::v4i16: Opc = ARM::VUZPd16; break;
    case MVT::v2f32:
    case MVT::v2i32: Opc = ARM::VUZPd32; break;
    case MVT::v16i8: Opc = ARM::VUZPq8; break;
    case MVT::v8i16: Opc = ARM::VUZPq16; break;
    case MVT::v4f32:
    case MVT::v4i32: Opc = ARM::VUZPq32; break;
    }
    SDValue Pred = getAL(CurDAG);
    SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1), Pred, PredReg };
    return CurDAG->getMachineNode(Opc, dl, VT, VT, Ops, 4);
  }
  case ARMISD::VTRN: {
    unsigned Opc = 0;
    EVT VT = N->getValueType(0);
    switch (VT.getSimpleVT().SimpleTy) {
    default: return NULL;
    case MVT::v8i8:  Opc = ARM::VTRNd8; break;
    case MVT::v4i16: Opc = ARM::VTRNd16; break;
    case MVT::v2f32:
    case MVT::v2i32: Opc = ARM::VTRNd32; break;
    case MVT::v16i8: Opc = ARM::VTRNq8; break;
    case MVT::v8i16: Opc = ARM::VTRNq16; break;
    case MVT::v4f32:
    case MVT::v4i32: Opc = ARM::VTRNq32; break;
    }
    SDValue Pred = getAL(CurDAG);
    SDValue PredReg = CurDAG->getRegister(0, MVT::i32);
    SDValue Ops[] = { N->getOperand(0), N->getOperand(1), Pred, PredReg };
    return CurDAG->getMachineNode(Opc, dl, VT, VT, Ops, 4);
  }
  case ARMISD::BUILD_VECTOR: {
    EVT VecVT = N->getValueType(0);
    EVT EltVT = VecVT.getVectorElementType();
    unsigned NumElts = VecVT.getVectorNumElements();
    if (EltVT == MVT::f64) {
      assert(NumElts == 2 && "unexpected type for BUILD_VECTOR");
      return PairDRegs(VecVT, N->getOperand(0), N->getOperand(1));
    }
    assert(EltVT == MVT::f32 && "unexpected type for BUILD_VECTOR");
    if (NumElts == 2)
      return PairSRegs(VecVT, N->getOperand(0), N->getOperand(1));
    assert(NumElts == 4 && "unexpected type for BUILD_VECTOR");
    return QuadSRegs(VecVT, N->getOperand(0), N->getOperand(1),
                     N->getOperand(2), N->getOperand(3));
  }

  case ARMISD::VLD2DUP: {
    unsigned Opcodes[] = { ARM::VLD2DUPd8Pseudo, ARM::VLD2DUPd16Pseudo,
                           ARM::VLD2DUPd32Pseudo };
    return SelectVLDDup(N, 2, Opcodes);
  }

  case ARMISD::VLD3DUP: {
    unsigned Opcodes[] = { ARM::VLD3DUPd8Pseudo, ARM::VLD3DUPd16Pseudo,
                           ARM::VLD3DUPd32Pseudo };
    return SelectVLDDup(N, 3, Opcodes);
  }

  case ARMISD::VLD4DUP: {
    unsigned Opcodes[] = { ARM::VLD4DUPd8Pseudo, ARM::VLD4DUPd16Pseudo,
                           ARM::VLD4DUPd32Pseudo };
    return SelectVLDDup(N, 4, Opcodes);
  }

  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned IntNo = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
    switch (IntNo) {
    default:
      break;

    case Intrinsic::arm_neon_vld1: {
      unsigned DOpcodes[] = { ARM::VLD1d8, ARM::VLD1d16,
                              ARM::VLD1d32, ARM::VLD1d64 };
      unsigned QOpcodes[] = { ARM::VLD1q8Pseudo, ARM::VLD1q16Pseudo,
                              ARM::VLD1q32Pseudo, ARM::VLD1q64Pseudo };
      return SelectVLD(N, 1, DOpcodes, QOpcodes, 0);
    }

    case Intrinsic::arm_neon_vld2: {
      unsigned DOpcodes[] = { ARM::VLD2d8Pseudo, ARM::VLD2d16Pseudo,
                              ARM::VLD2d32Pseudo, ARM::VLD1q64Pseudo };
      unsigned QOpcodes[] = { ARM::VLD2q8Pseudo, ARM::VLD2q16Pseudo,
                              ARM::VLD2q32Pseudo };
      return SelectVLD(N, 2, DOpcodes, QOpcodes, 0);
    }

    case Intrinsic::arm_neon_vld3: {
      unsigned DOpcodes[] = { ARM::VLD3d8Pseudo, ARM::VLD3d16Pseudo,
                              ARM::VLD3d32Pseudo, ARM::VLD1d64TPseudo };
      unsigned QOpcodes0[] = { ARM::VLD3q8Pseudo_UPD,
                               ARM::VLD3q16Pseudo_UPD,
                               ARM::VLD3q32Pseudo_UPD };
      unsigned QOpcodes1[] = { ARM::VLD3q8oddPseudo_UPD,
                               ARM::VLD3q16oddPseudo_UPD,
                               ARM::VLD3q32oddPseudo_UPD };
      return SelectVLD(N, 3, DOpcodes, QOpcodes0, QOpcodes1);
    }

    case Intrinsic::arm_neon_vld4: {
      unsigned DOpcodes[] = { ARM::VLD4d8Pseudo, ARM::VLD4d16Pseudo,
                              ARM::VLD4d32Pseudo, ARM::VLD1d64QPseudo };
      unsigned QOpcodes0[] = { ARM::VLD4q8Pseudo_UPD,
                               ARM::VLD4q16Pseudo_UPD,
                               ARM::VLD4q32Pseudo_UPD };
      unsigned QOpcodes1[] = { ARM::VLD4q8oddPseudo_UPD,
                               ARM::VLD4q16oddPseudo_UPD,
                               ARM::VLD4q32oddPseudo_UPD };
      return SelectVLD(N, 4, DOpcodes, QOpcodes0, QOpcodes1);
    }

    case Intrinsic::arm_neon_vld2lane: {
      unsigned DOpcodes[] = { ARM::VLD2LNd8Pseudo, ARM::VLD2LNd16Pseudo,
                              ARM::VLD2LNd32Pseudo };
      unsigned QOpcodes[] = { ARM::VLD2LNq16Pseudo, ARM::VLD2LNq32Pseudo };
      return SelectVLDSTLane(N, true, 2, DOpcodes, QOpcodes);
    }

    case Intrinsic::arm_neon_vld3lane: {
      unsigned DOpcodes[] = { ARM::VLD3LNd8Pseudo, ARM::VLD3LNd16Pseudo,
                              ARM::VLD3LNd32Pseudo };
      unsigned QOpcodes[] = { ARM::VLD3LNq16Pseudo, ARM::VLD3LNq32Pseudo };
      return SelectVLDSTLane(N, true, 3, DOpcodes, QOpcodes);
    }

    case Intrinsic::arm_neon_vld4lane: {
      unsigned DOpcodes[] = { ARM::VLD4LNd8Pseudo, ARM::VLD4LNd16Pseudo,
                              ARM::VLD4LNd32Pseudo };
      unsigned QOpcodes[] = { ARM::VLD4LNq16Pseudo, ARM::VLD4LNq32Pseudo };
      return SelectVLDSTLane(N, true, 4, DOpcodes, QOpcodes);
    }

    case Intrinsic::arm_neon_vst1: {
      unsigned DOpcodes[] = { ARM::VST1d8, ARM::VST1d16,
                              ARM::VST1d32, ARM::VST1d64 };
      unsigned QOpcodes[] = { ARM::VST1q8Pseudo, ARM::VST1q16Pseudo,
                              ARM::VST1q32Pseudo, ARM::VST1q64Pseudo };
      return SelectVST(N, 1, DOpcodes, QOpcodes, 0);
    }

    case Intrinsic::arm_neon_vst2: {
      unsigned DOpcodes[] = { ARM::VST2d8Pseudo, ARM::VST2d16Pseudo,
                              ARM::VST2d32Pseudo, ARM::VST1q64Pseudo };
      unsigned QOpcodes[] = { ARM::VST2q8Pseudo, ARM::VST2q16Pseudo,
                              ARM::VST2q32Pseudo };
      return SelectVST(N, 2, DOpcodes, QOpcodes, 0);
    }

    case Intrinsic::arm_neon_vst3: {
      unsigned DOpcodes[] = { ARM::VST3d8Pseudo, ARM::VST3d16Pseudo,
                              ARM::VST3d32Pseudo, ARM::VST1d64TPseudo };
      unsigned QOpcodes0[] = { ARM::VST3q8Pseudo_UPD,
                               ARM::VST3q16Pseudo_UPD,
                               ARM::VST3q32Pseudo_UPD };
      unsigned QOpcodes1[] = { ARM::VST3q8oddPseudo_UPD,
                               ARM::VST3q16oddPseudo_UPD,
                               ARM::VST3q32oddPseudo_UPD };
      return SelectVST(N, 3, DOpcodes, QOpcodes0, QOpcodes1);
    }

    case Intrinsic::arm_neon_vst4: {
      unsigned DOpcodes[] = { ARM::VST4d8Pseudo, ARM::VST4d16Pseudo,
                              ARM::VST4d32Pseudo, ARM::VST1d64QPseudo };
      unsigned QOpcodes0[] = { ARM::VST4q8Pseudo_UPD,
                               ARM::VST4q16Pseudo_UPD,
                               ARM::VST4q32Pseudo_UPD };
      unsigned QOpcodes1[] = { ARM::VST4q8oddPseudo_UPD,
                               ARM::VST4q16oddPseudo_UPD,
                               ARM::VST4q32oddPseudo_UPD };
      return SelectVST(N, 4, DOpcodes, QOpcodes0, QOpcodes1);
    }

    case Intrinsic::arm_neon_vst2lane: {
      unsigned DOpcodes[] = { ARM::VST2LNd8Pseudo, ARM::VST2LNd16Pseudo,
                              ARM::VST2LNd32Pseudo };
      unsigned QOpcodes[] = { ARM::VST2LNq16Pseudo, ARM::VST2LNq32Pseudo };
      return SelectVLDSTLane(N, false, 2, DOpcodes, QOpcodes);
    }

    case Intrinsic::arm_neon_vst3lane: {
      unsigned DOpcodes[] = { ARM::VST3LNd8Pseudo, ARM::VST3LNd16Pseudo,
                              ARM::VST3LNd32Pseudo };
      unsigned QOpcodes[] = { ARM::VST3LNq16Pseudo, ARM::VST3LNq32Pseudo };
      return SelectVLDSTLane(N, false, 3, DOpcodes, QOpcodes);
    }

    case Intrinsic::arm_neon_vst4lane: {
      unsigned DOpcodes[] = { ARM::VST4LNd8Pseudo, ARM::VST4LNd16Pseudo,
                              ARM::VST4LNd32Pseudo };
      unsigned QOpcodes[] = { ARM::VST4LNq16Pseudo, ARM::VST4LNq32Pseudo };
      return SelectVLDSTLane(N, false, 4, DOpcodes, QOpcodes);
    }
    }
    break;
  }

  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntNo = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
    switch (IntNo) {
    default:
      break;

    case Intrinsic::arm_neon_vtbl2:
      return SelectVTBL(N, false, 2, ARM::VTBL2Pseudo);
    case Intrinsic::arm_neon_vtbl3:
      return SelectVTBL(N, false, 3, ARM::VTBL3Pseudo);
    case Intrinsic::arm_neon_vtbl4:
      return SelectVTBL(N, false, 4, ARM::VTBL4Pseudo);

    case Intrinsic::arm_neon_vtbx2:
      return SelectVTBL(N, true, 2, ARM::VTBX2Pseudo);
    case Intrinsic::arm_neon_vtbx3:
      return SelectVTBL(N, true, 3, ARM::VTBX3Pseudo);
    case Intrinsic::arm_neon_vtbx4:
      return SelectVTBL(N, true, 4, ARM::VTBX4Pseudo);
    }
    break;
  }

  case ISD::CONCAT_VECTORS:
    return SelectConcatVector(N);
  }

  return SelectCode(N);
}

bool ARMDAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, char ConstraintCode,
                             std::vector<SDValue> &OutOps) {
  assert(ConstraintCode == 'm' && "unexpected asm memory constraint");
  // Require the address to be in a register.  That is safe for all ARM
  // variants and it is hard to do anything much smarter without knowing
  // how the operand is used.
  OutOps.push_back(Op);
  return false;
}

/// createARMISelDag - This pass converts a legalized DAG into a
/// ARM-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createARMISelDag(ARMBaseTargetMachine &TM,
                                     CodeGenOpt::Level OptLevel) {
  return new ARMDAGToDAGISel(TM, OptLevel);
}
