// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/ir.hh"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nba::core::arm::dynarec {

// Minimal SH4 encoder used by Phase 1 unit tests and Phase 2 native emit.
// Encodings follow the public SuperH SH-4 programming manual.
struct Sh4Emitter {
  void Reset() { code_.clear(); }

  auto Size() const -> std::size_t { return code_.size() * sizeof(u16); }
  auto Words() const -> std::span<const u16> { return code_; }
  auto Data() const -> u16 const* { return code_.data(); }

  // Emit raw 16-bit instruction word.
  void Emit(u16 word) { code_.push_back(word); }

  // MOV Rm, Rn  (0100nnnnmmmm1100? No — MOV Rm,Rn is 0110nnnnmmmm0011)
  // Correct: MOV Rm,Rn = 0110nnnnmmmm0011
  void MovReg(int rn, int rm) {
    Emit(static_cast<u16>(0x6003 | (rn << 8) | (rm << 4)));
  }

  // MOV #imm, Rn (immediate signed 8-bit): 1110nnnniiiiiiii
  void MovImm(int rn, s8 imm) {
    Emit(static_cast<u16>(0xE000 | (rn << 8) | (static_cast<u8>(imm))));
  }

  // ADD Rm, Rn: 0011nnnnmmmm1100
  void AddReg(int rn, int rm) {
    Emit(static_cast<u16>(0x300C | (rn << 8) | (rm << 4)));
  }

  // ADD #imm, Rn: 0111nnnniiiiiiii
  void AddImm(int rn, s8 imm) {
    Emit(static_cast<u16>(0x7000 | (rn << 8) | (static_cast<u8>(imm))));
  }

  // SUB Rm, Rn: 0011nnnnmmmm1000
  void SubReg(int rn, int rm) {
    Emit(static_cast<u16>(0x3008 | (rn << 8) | (rm << 4)));
  }

  // RTS: 0000000000001011
  void Rts() { Emit(0x000B); }

  // NOP: 0000000000001001
  void Nop() { Emit(0x0009); }

  // JSR @Rn: 0100nnnn00001011
  void Jsr(int rn) {
    Emit(static_cast<u16>(0x400B | (rn << 8)));
  }

  // MOV.L @(disp,PC), Rn — placeholder helper for pools (disp in longwords).
  void MovLPcDisp(int rn, u8 disp) {
    Emit(static_cast<u16>(0xD000 | (rn << 8) | disp));
  }

private:
  std::vector<u16> code_;
};

// Known-good encodings for unit tests.
namespace sh4_enc {
  inline constexpr u16 kNop = 0x0009;
  inline constexpr u16 kRts = 0x000B;
  inline constexpr u16 MovReg(int rn, int rm) {
    return static_cast<u16>(0x6003 | (rn << 8) | (rm << 4));
  }
  inline constexpr u16 AddImm(int rn, s8 imm) {
    return static_cast<u16>(0x7000 | (rn << 8) | (static_cast<u8>(imm)));
  }
}

} // namespace nba::core::arm::dynarec
