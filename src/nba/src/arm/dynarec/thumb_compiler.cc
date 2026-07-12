// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/thumb_compiler.hh"

#include <nba/common/punning.hh>

namespace nba::core::arm::dynarec {

namespace {

auto ReadPhysicalHalf(Bus& bus, u32 address) -> u16 {
  address &= ~1u;
  const u32 page = address >> 24;

  switch(page) {
    case 0x00: {
      if(address >= bus.memory.bios.size()) {
        return 0;
      }
      return read<u16>(bus.memory.bios.data(), address);
    }
    case 0x02: {
      return read<u16>(bus.memory.wram.data(), address & 0x3FFFF);
    }
    case 0x03: {
      return read<u16>(bus.memory.iram.data(), address & 0x7FFF);
    }
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: {
      u16 value = 0;
      if(bus.memory.rom.CopyRange(address & 0x01FFFFFF, sizeof(value),
                                  reinterpret_cast<u8*>(&value))) {
        return value;
      }
      return 0;
    }
    default:
      return 0;
  }
}

enum class DecodeResult {
  Emit,
  EndBlockBefore, // unsupported / control — stop before this insn
  EndBlockAfter,  // branch compiled as final op
};

struct Decoded {
  DecodeResult result = DecodeResult::EndBlockBefore;
  IrOp op{};
};

auto DecodeThumb(u16 insn, u32 insn_pc) -> Decoded {
  Decoded d{};

  // THUMB.1 Move shifted register
  if((insn & 0xE000) == 0x0000 && (insn & 0x1800) != 0x1800) {
    const int op = (insn >> 11) & 3;
    const int imm = (insn >> 6) & 0x1F;
    const int rs = (insn >> 3) & 7;
    const int rd = insn & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MovShifted;
    d.op.rd = static_cast<u8>(rd);
    d.op.rm = static_cast<u8>(rs);
    d.op.aux = static_cast<u8>(op); // 0 LSL, 1 LSR, 2 ASR
    d.op.imm = static_cast<u32>(imm);
    return d;
  }

  // THUMB.2 Add/subtract
  if((insn & 0xF800) == 0x1800) {
    const bool immediate = (insn & (1 << 10)) != 0;
    const bool subtract = (insn & (1 << 9)) != 0;
    const int field3 = (insn >> 6) & 7;
    const int rs = (insn >> 3) & 7;
    const int rd = insn & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = subtract ? IrOpKind::SubReg : IrOpKind::AddReg;
    d.op.rd = static_cast<u8>(rd);
    d.op.rn = static_cast<u8>(rs);
    if(immediate) {
      d.op.rm = 0xFF; // marker: use imm
      d.op.imm = static_cast<u32>(field3);
    } else {
      d.op.rm = static_cast<u8>(field3);
      d.op.imm = 0;
    }
    return d;
  }

  // THUMB.3 Move/compare/add/subtract immediate
  if((insn & 0xE000) == 0x2000) {
    const int op = (insn >> 11) & 3;
    const int rd = (insn >> 8) & 7;
    const u32 imm = insn & 0xFF;
    d.result = DecodeResult::Emit;
    d.op.rd = static_cast<u8>(rd);
    d.op.imm = imm;
    switch(op) {
      case 0: d.op.kind = IrOpKind::MovImm; break;
      case 1: d.op.kind = IrOpKind::CmpImm; break;
      case 2: d.op.kind = IrOpKind::AddImm; break;
      default: d.op.kind = IrOpKind::SubImm; break;
    }
    return d;
  }

  // THUMB.4 ALU operations
  if((insn & 0xFC00) == 0x4000) {
    const int op = (insn >> 6) & 0xF;
    const int rs = (insn >> 3) & 7;
    const int rd = insn & 7;
    // MUL needs multi-cycle Idle — still OK via IrExec calling bus.Idle.
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::AluReg;
    d.op.rd = static_cast<u8>(rd);
    d.op.rm = static_cast<u8>(rs);
    d.op.aux = static_cast<u8>(op);
    return d;
  }

  // THUMB.18 unconditional branch: 11100ooooooooooo
  if((insn & 0xF800) == 0xE000) {
    // Mirror Thumb_UnconditionalBranch: offset added to r15 (== insn_pc + 4).
    u32 imm = (insn & 0x3FF) * 2;
    if(insn & 0x400) {
      imm |= 0xFFFFF800u;
    }
    const u32 target = (insn_pc + 4 + imm) & ~1u;
    d.result = DecodeResult::EndBlockAfter;
    d.op.kind = IrOpKind::Branch;
    d.op.imm = target;
    return d;
  }

  // Everything else: memory, hi-reg, BL, conditional B, SWI, …
  d.result = DecodeResult::EndBlockBefore;
  return d;
}

} // namespace

auto PeekCodeHalf(Bus& bus, u32 address) -> u16 {
  return ReadPhysicalHalf(bus, address);
}

auto CompileThumbBlock(Bus& bus, u32 pc, CompiledBlock& out) -> bool {
  out = {};
  out.pc = pc & ~1u;
  out.thumb = true;

  u32 cursor = out.pc;
  for(int i = 0; i < kMaxBlockInsns; ++i) {
    const u16 insn = PeekCodeHalf(bus, cursor);
    const auto decoded = DecodeThumb(insn, cursor);

    if(decoded.result == DecodeResult::EndBlockBefore) {
      break;
    }

    if(out.ir_count >= kMaxIrOps - 1) {
      break;
    }

    out.ir[out.ir_count++] = decoded.op;
    ++out.guest_insns;
    cursor += 2;

    if(decoded.result == DecodeResult::EndBlockAfter) {
      break;
    }
  }

  if(out.guest_insns == 0) {
    return false;
  }

  // Ensure the block ends with Exit unless it already ends in Branch.
  if(out.ir[out.ir_count - 1].kind != IrOpKind::Branch) {
    out.ir[out.ir_count++] = IrOp{.kind = IrOpKind::Exit};
  }

  return true;
}

} // namespace nba::core::arm::dynarec
