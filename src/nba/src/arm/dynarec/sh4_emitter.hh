// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/ir.hh"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nba::core::arm::dynarec {

// Minimal SH4 encoder used by Phase 1 unit tests and Phase 2 native emit.
// Encodings follow the public SuperH SH-4 programming manual.
struct Sh4Emitter {
  void Reset() { code_.clear(); }

  auto Size() const -> std::size_t { return code_.size() * sizeof(u16); }
  auto Words() const -> std::vector<u16> const& { return code_; }
  auto Data() const -> u16 const* { return code_.data(); }

  auto Emit(u16 word) -> int {
    code_.push_back(word);
    return static_cast<int>(code_.size()) - 1;
  }

  void MovReg(int rn, int rm) {
    Emit(static_cast<u16>(0x6003 | (rn << 8) | (rm << 4)));
  }

  void MovImm(int rn, s8 imm) {
    Emit(static_cast<u16>(0xE000 | (rn << 8) | (static_cast<u8>(imm))));
  }

  void AddReg(int rn, int rm) {
    Emit(static_cast<u16>(0x300C | (rn << 8) | (rm << 4)));
  }

  void AddImm(int rn, s8 imm) {
    Emit(static_cast<u16>(0x7000 | (rn << 8) | (static_cast<u8>(imm))));
  }

  void SubReg(int rn, int rm) {
    Emit(static_cast<u16>(0x3008 | (rn << 8) | (rm << 4)));
  }

  // CMP/eq Rm, Rn — true when Rn == Rm.
  void CmpEq(int rn, int rm) {
    Emit(static_cast<u16>(0x3000 | (rn << 8) | (rm << 4)));
  }

  // CMP/GE Rm, Rn — true when Rn >= Rm (signed).
  void CmpGe(int rn, int rm) {
    Emit(static_cast<u16>(0x3005 | (rn << 8) | (rm << 4)));
  }

  void Shll2(int rn) {
    Emit(static_cast<u16>(0x4008 | (rn << 8)));
  }

  void Shll3(int rn) {
    Emit(static_cast<u16>(0x4018 | (rn << 8)));
  }

  void Bf(s8 disp) {
    Emit(static_cast<u16>(0x8B00 | (static_cast<u8>(disp))));
  }

  void Bt(s8 disp) {
    Emit(static_cast<u16>(0x8900 | (static_cast<u8>(disp))));
  }

  void Bra(s16 disp) {
    Emit(static_cast<u16>(0xA000 | (disp & 0x0FFF)));
  }

  void MovLPcDisp(int rn, u8 disp) {
    Emit(static_cast<u16>(0xD000 | (rn << 8) | disp));
  }

  void Jsr(int rn) {
    Emit(static_cast<u16>(0x400B | (rn << 8)));
  }

  void StsLPrPreDec(int rn) {
    Emit(static_cast<u16>(0x4F22 | (rn << 8)));
  }

  void LdsLPostIncPr(int rn) {
    Emit(static_cast<u16>(0x4026 | (rn << 8)));
  }

  void Rts() { Emit(0x000B); }
  void Nop() { Emit(0x0009); }

  void EmitPoolU32(u32 value) {
    Emit(static_cast<u16>(value & 0xFFFF));
    Emit(static_cast<u16>(value >> 16));
  }

  void PatchBf(int insn_idx, int target_pc) {
    const s8 disp = static_cast<s8>(target_pc - (insn_idx + 1));
    code_[insn_idx] = static_cast<u16>(0x8B00 | static_cast<u8>(disp));
  }

  void PatchBt(int insn_idx, int target_pc) {
    const s8 disp = static_cast<s8>(target_pc - (insn_idx + 1));
    code_[insn_idx] = static_cast<u16>(0x8900 | static_cast<u8>(disp));
  }

  void PatchBra(int insn_idx, int target_pc) {
    const s16 disp = static_cast<s16>(target_pc - (insn_idx + 1));
    code_[insn_idx] = static_cast<u16>(0xA000 | (disp & 0x0FFF));
  }

  void PatchMovLPcDisp(int insn_idx, int target_pc, int rn) {
    const u8 disp = static_cast<u8>(target_pc - (insn_idx + 1));
    code_[insn_idx] = static_cast<u16>(0xD000 | (rn << 8) | disp);
  }

private:
  std::vector<u16> code_;
};

namespace sh4_enc {
  inline constexpr u16 kNop = 0x0009;
  inline constexpr u16 kRts = 0x000B;
  inline constexpr u16 MovReg(int rn, int rm) {
    return static_cast<u16>(0x6003 | (rn << 8) | (rm << 4));
  }
  inline constexpr u16 AddImm(int rn, s8 imm) {
    return static_cast<u16>(0x7000 | (rn << 8) | (static_cast<u8>(imm)));
  }
  inline constexpr u16 CmpGe(int rn, int rm) {
    return static_cast<u16>(0x3005 | (rn << 8) | (rm << 4));
  }
  inline constexpr u16 CmpEq(int rn, int rm) {
    return static_cast<u16>(0x3000 | (rn << 8) | (rm << 4));
  }
}

} // namespace nba::core::arm::dynarec
