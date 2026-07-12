// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/integer.hh>
#include <array>
#include <cstddef>
#include <cstdint>

namespace nba::core::arm::dynarec {

inline constexpr int kMaxBlockInsns = 32;
inline constexpr int kMaxIrOps = kMaxBlockInsns + 4;
inline constexpr int kMaxBlockChain = 8;

enum class IrOpKind : u8 {
  MovImm,
  CmpImm,
  AddImm,
  SubImm,
  MovShifted,
  AddReg,
  SubReg,
  AluReg,

  MemPcRel,
  MemOffsetReg,
  MemSigned,
  MemOffsetImm,
  MemHalfImm,
  MemSpRel,
  LoadAddress,
  AddSpImm,
  PushPop,
  LdmStm,

  // Unconditional branch (Thumb B / ARM B with AL): imm = target PC
  Branch,
  // Conditional branch: aux = Condition, imm = taken target PC
  CondBranch,

  // ARM data-processing with immediate op2 (rotated).
  // rd, rn; aux = opcode | (S<<4); rm = cond; imm = op2
  ArmDataImm,
  // ARM data-processing with register + immediate shift.
  // rd, rn, rm = Rm; aux = opcode | (S<<4) | (shift_type<<5);
  // imm = shift_imm | (cond << 8)
  ArmDataReg,

  Exit,
};

enum class ShiftKind : u8 {
  LSL = 0,
  LSR = 1,
  ASR = 2,
};

enum class IrStepResult : u8 {
  Continue,
  Done,
  IrqExit,
};

struct IrOp {
  IrOpKind kind = IrOpKind::Exit;
  u8 rd = 0;
  u8 rn = 0;
  u8 rm = 0;
  u8 aux = 0;
  u32 imm = 0;
};

struct CompiledBlock {
  u32 pc = 0;
  bool thumb = true;
  u16 ir_count = 0;
  u16 guest_insns = 0;
  std::array<IrOp, kMaxIrOps> ir{};

  // Soft link targets for Phase-4 block chaining (guest PCs).
  u32 exit_taken = 0;
  u32 exit_fallthrough = 0;
  bool exit_taken_thumb = true;
  bool exit_fallthrough_thumb = true;

  bool (*native)(void* cpu, IrOp const* ops, u16 count) = nullptr;
  u8* native_code = nullptr;
  u32 native_size = 0;
};

inline constexpr u32 BlockKey(u32 pc, bool thumb) {
  return (pc & ~1u) | (thumb ? 1u : 0u);
}

} // namespace nba::core::arm::dynarec
