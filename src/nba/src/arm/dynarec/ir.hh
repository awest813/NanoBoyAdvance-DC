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
  // rd = imm8; set NZ
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

  // Memory / addressing (Phase 3)
  // LDR Rd, [PC, #imm] — address = (r15 & ~2) + imm (imm already <<2)
  MemPcRel,
  // LDR/STR Rd, [Rn, Rm] — aux = 0 STR, 1 STRB, 2 LDR, 3 LDRB
  MemOffsetReg,
  // STRH/LDSB/LDRH/LDSH — aux = 0..3
  MemSigned,
  // LDR/STR/LDRB/STRB [Rn, #imm] — aux = 0 STR, 1 LDR, 2 STRB, 3 LDRB;
  // imm is already scaled (×4 for word, ×1 for byte)
  MemOffsetImm,
  // LDRH/STRH [Rn, #imm*2] — aux = load?
  MemHalfImm,
  // LDR/STR Rd, [SP, #imm*4] — aux = load?
  MemSpRel,
  // ADD Rd, PC/SP, #imm — aux = use_sp?
  LoadAddress,
  // ADD/SUB SP, #imm — aux = sub?
  AddSpImm,
  // PUSH/POP — aux = (pop<<1)|rbit, imm = low8 list
  PushPop,
  // LDMIA/STMIA — rn = base, aux = load?, imm = low8 list
  LdmStm,

  // Unconditional Thumb branch: PC = target, reload pipeline.
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
  u8 aux = 0;   // shift kind, alu op, mem op, or flags
  u32 imm = 0;  // immediate, list, or branch target PC
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
