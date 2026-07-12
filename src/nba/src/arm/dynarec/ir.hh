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

enum class IrOpKind : u8 {
  // rd = imm8; set NZ; clear for MOV
  MovImm,
  // NZCV from (rd - imm8); rd unchanged
  CmpImm,
  // rd = rd + imm8; set NZCV
  AddImm,
  // rd = rd - imm8; set NZCV
  SubImm,

  // rd = Shift(rm, kind, imm); set NZ + C from shift
  MovShifted,
  // rd = rn + rm/imm; set NZCV
  AddReg,
  // rd = rn - rm/imm; set NZCV
  SubReg,

  // THUMB.4 ALU (low regs). op matches ThumbDataOp.
  AluReg,

  // Unconditional Thumb branch: PC = base + offset (bytes), reload pipeline.
  Branch,

  // End block and return to the dispatcher (no PC change beyond prior ops).
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
  u8 aux = 0;   // shift kind, alu op, or flags
  u32 imm = 0;  // immediate or branch target PC
};

struct CompiledBlock {
  u32 pc = 0;
  bool thumb = true;
  u16 ir_count = 0;
  u16 guest_insns = 0;
  std::array<IrOp, kMaxIrOps> ir{};

  // Native SH4 entry (null on host builds). Signature:
  // bool (*)(void* cpu, IrOp const* ops, u16 count)
  bool (*native)(void* cpu, IrOp const* ops, u16 count) = nullptr;
  u8* native_code = nullptr;
  u32 native_size = 0;
};

inline constexpr u32 BlockKey(u32 pc, bool thumb) {
  return (pc & ~1u) | (thumb ? 1u : 0u);
}

} // namespace nba::core::arm::dynarec
