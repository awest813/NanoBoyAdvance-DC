// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/thumb_compiler.hh"

#include <nba/common/punning.hh>

namespace nba::core::arm::dynarec {

namespace {

enum class DecodeResult {
  Emit,
  EndBlockBefore,
  EndBlockAfter,
};

struct Decoded {
  DecodeResult result = DecodeResult::EndBlockBefore;
  IrOp op{};
};

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
    case 0x02:
      return read<u16>(bus.memory.wram.data(), address & 0x3FFFF);
    case 0x03:
      return read<u16>(bus.memory.iram.data(), address & 0x7FFF);
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

auto ReadPhysicalWord(Bus& bus, u32 address) -> u32 {
  address &= ~3u;
  const u32 page = address >> 24;

  switch(page) {
    case 0x00: {
      if(address + 3 >= bus.memory.bios.size()) {
        return 0;
      }
      return read<u32>(bus.memory.bios.data(), address);
    }
    case 0x02:
      return read<u32>(bus.memory.wram.data(), address & 0x3FFFF);
    case 0x03:
      return read<u32>(bus.memory.iram.data(), address & 0x7FFF);
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: {
      u32 value = 0;
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

auto DecodeThumb(u16 insn, u32 insn_pc) -> Decoded {
  Decoded d{};

  if((insn & 0xE000) == 0x0000 && (insn & 0x1800) != 0x1800) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MovShifted;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rm = static_cast<u8>((insn >> 3) & 7);
    d.op.aux = static_cast<u8>((insn >> 11) & 3);
    d.op.imm = static_cast<u32>((insn >> 6) & 0x1F);
    return d;
  }

  if((insn & 0xF800) == 0x1800) {
    const bool immediate = (insn & (1 << 10)) != 0;
    const bool subtract = (insn & (1 << 9)) != 0;
    const int field3 = (insn >> 6) & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = subtract ? IrOpKind::SubReg : IrOpKind::AddReg;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rn = static_cast<u8>((insn >> 3) & 7);
    if(immediate) {
      d.op.rm = 0xFF;
      d.op.imm = static_cast<u32>(field3);
    } else {
      d.op.rm = static_cast<u8>(field3);
    }
    return d;
  }

  if((insn & 0xE000) == 0x2000) {
    const int op = (insn >> 11) & 3;
    d.result = DecodeResult::Emit;
    d.op.rd = static_cast<u8>((insn >> 8) & 7);
    d.op.imm = insn & 0xFF;
    switch(op) {
      case 0: d.op.kind = IrOpKind::MovImm; break;
      case 1: d.op.kind = IrOpKind::CmpImm; break;
      case 2: d.op.kind = IrOpKind::AddImm; break;
      default: d.op.kind = IrOpKind::SubImm; break;
    }
    return d;
  }

  if((insn & 0xFC00) == 0x4000) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::AluReg;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rm = static_cast<u8>((insn >> 3) & 7);
    d.op.aux = static_cast<u8>((insn >> 6) & 0xF);
    return d;
  }

  if((insn & 0xFC00) == 0x4400) {
    d.result = DecodeResult::EndBlockBefore;
    return d;
  }

  if((insn & 0xF800) == 0x4800) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemPcRel;
    d.op.rd = static_cast<u8>((insn >> 8) & 7);
    d.op.imm = (insn & 0xFF) << 2;
    return d;
  }

  if((insn & 0xF200) == 0x5000) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemOffsetReg;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rn = static_cast<u8>((insn >> 3) & 7);
    d.op.rm = static_cast<u8>((insn >> 6) & 7);
    d.op.aux = static_cast<u8>((insn >> 10) & 3);
    return d;
  }

  if((insn & 0xF200) == 0x5200) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemSigned;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rn = static_cast<u8>((insn >> 3) & 7);
    d.op.rm = static_cast<u8>((insn >> 6) & 7);
    d.op.aux = static_cast<u8>((insn >> 10) & 3);
    return d;
  }

  if((insn & 0xE000) == 0x6000) {
    const int op = (insn >> 11) & 3;
    const int offset5 = (insn >> 6) & 0x1F;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemOffsetImm;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rn = static_cast<u8>((insn >> 3) & 7);
    d.op.aux = static_cast<u8>(op);
    d.op.imm = (op < 2) ? static_cast<u32>(offset5 * 4) : static_cast<u32>(offset5);
    return d;
  }

  if((insn & 0xF000) == 0x8000) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemHalfImm;
    d.op.rd = static_cast<u8>(insn & 7);
    d.op.rn = static_cast<u8>((insn >> 3) & 7);
    d.op.aux = (insn & (1 << 11)) ? 1 : 0;
    d.op.imm = static_cast<u32>(((insn >> 6) & 0x1F) * 2);
    return d;
  }

  if((insn & 0xF000) == 0x9000) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemSpRel;
    d.op.rd = static_cast<u8>((insn >> 8) & 7);
    d.op.aux = (insn & (1 << 11)) ? 1 : 0;
    d.op.imm = (insn & 0xFF) * 4;
    return d;
  }

  if((insn & 0xF000) == 0xA000) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::LoadAddress;
    d.op.rd = static_cast<u8>((insn >> 8) & 7);
    d.op.aux = (insn & (1 << 11)) ? 1 : 0;
    d.op.imm = (insn & 0xFF) << 2;
    return d;
  }

  if((insn & 0xFF00) == 0xB000) {
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::AddSpImm;
    d.op.aux = (insn & (1 << 7)) ? 1 : 0;
    d.op.imm = (insn & 0x7F) * 4;
    return d;
  }

  if((insn & 0xF600) == 0xB400) {
    const bool pop = (insn & (1 << 11)) != 0;
    const bool rbit = (insn & (1 << 8)) != 0;
    d.op.kind = IrOpKind::PushPop;
    d.op.aux = static_cast<u8>((pop ? 2 : 0) | (rbit ? 1 : 0));
    d.op.imm = insn & 0xFF;
    d.result = (pop && rbit) ? DecodeResult::EndBlockAfter : DecodeResult::Emit;
    return d;
  }

  if((insn & 0xF000) == 0xC000) {
    const bool load = (insn & (1 << 11)) != 0;
    d.op.kind = IrOpKind::LdmStm;
    d.op.rn = static_cast<u8>((insn >> 8) & 7);
    d.op.aux = load ? 1 : 0;
    d.op.imm = insn & 0xFF;
    d.result = (load && d.op.imm == 0) ? DecodeResult::EndBlockAfter : DecodeResult::Emit;
    return d;
  }

  if((insn & 0xF000) == 0xD000 && (insn & 0xFF00) != 0xDF00) {
    const int cond = (insn >> 8) & 0xF;
    if(cond < 14) {
      u32 imm = insn & 0xFF;
      if(imm & 0x80) {
        imm |= 0xFFFFFF00u;
      }
      d.result = DecodeResult::EndBlockAfter;
      d.op.kind = IrOpKind::CondBranch;
      d.op.aux = static_cast<u8>(cond);
      d.op.imm = (insn_pc + 4 + imm * 2) & ~1u;
      return d;
    }
  }

  if((insn & 0xF800) == 0xE000) {
    u32 imm = (insn & 0x3FF) * 2;
    if(insn & 0x400) {
      imm |= 0xFFFFF800u;
    }
    d.result = DecodeResult::EndBlockAfter;
    d.op.kind = IrOpKind::Branch;
    d.op.imm = (insn_pc + 4 + imm) & ~1u;
    return d;
  }

  d.result = DecodeResult::EndBlockBefore;
  return d;
}

auto DecodeArm(u32 insn, u32 insn_pc) -> Decoded {
  Decoded d{};
  const int cond = (insn >> 28) & 0xF;
  if(cond == 0xF) {
    return d;
  }

  if((insn & 0x0E000000) == 0x0A000000) {
    if(insn & (1u << 24)) {
      return d;
    }
    u32 imm = insn & 0x00FFFFFF;
    if(imm & 0x00800000) {
      imm |= 0xFF000000u;
    }
    const u32 target = (insn_pc + 8 + (imm << 2)) & ~3u;
    d.result = DecodeResult::EndBlockAfter;
    if(cond == 0xE) {
      d.op.kind = IrOpKind::Branch;
      d.op.imm = target;
    } else {
      d.op.kind = IrOpKind::CondBranch;
      d.op.aux = static_cast<u8>(cond);
      d.op.imm = target;
    }
    return d;
  }

  if((insn & 0x0C000000) == 0x00000000 && (insn & 0x01900000) != 0x01000000) {
    const bool immediate = (insn & (1u << 25)) != 0;
    const int opcode = (insn >> 21) & 0xF;
    const bool set_flags = (insn & (1u << 20)) != 0;
    const int rn = (insn >> 16) & 0xF;
    const int rd = (insn >> 12) & 0xF;

    if(rd == 15 || rn == 15) {
      return d;
    }

    if(immediate) {
      d.result = DecodeResult::Emit;
      d.op.kind = IrOpKind::ArmDataImm;
      d.op.rd = static_cast<u8>(rd);
      d.op.rn = static_cast<u8>(rn);
      d.op.rm = static_cast<u8>(cond);
      d.op.aux = static_cast<u8>(opcode | (set_flags ? 0x10 : 0));
      d.op.imm = insn & 0xFFF; // raw operand2 field (imm8 + rotate)
      return d;
    }

    if((insn & (1u << 4)) != 0) {
      return d;
    }

    const int rm = insn & 0xF;
    if(rm == 15) {
      return d;
    }

    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::ArmDataReg;
    d.op.rd = static_cast<u8>(rd);
    d.op.rn = static_cast<u8>(rn);
    d.op.rm = static_cast<u8>(rm);
    d.op.aux = static_cast<u8>(
      opcode | (set_flags ? 0x10 : 0) | (((insn >> 5) & 3) << 5));
    d.op.imm = static_cast<u32>((insn >> 7) & 0x1F) | (static_cast<u32>(cond) << 8);
    return d;
  }

  return d;
}

void FinishExits(CompiledBlock& out, u32 cursor) {
  const auto& last = out.ir[out.ir_count - 1];
  const bool thumb = out.thumb;
  const bool control =
    last.kind == IrOpKind::Branch ||
    last.kind == IrOpKind::CondBranch ||
    (last.kind == IrOpKind::PushPop && (last.aux & 3) == 3) ||
    (last.kind == IrOpKind::LdmStm && last.aux != 0 && last.imm == 0);

  if(!control) {
    out.ir[out.ir_count++] = IrOp{.kind = IrOpKind::Exit};
    out.exit_fallthrough = cursor;
    out.exit_fallthrough_thumb = thumb;
    return;
  }

  if(last.kind == IrOpKind::Branch) {
    out.exit_taken = last.imm;
    out.exit_taken_thumb = thumb;
    return;
  }

  if(last.kind == IrOpKind::CondBranch) {
    out.exit_taken = last.imm;
    out.exit_taken_thumb = thumb;
    const u32 step = thumb ? 2u : 4u;
    out.exit_fallthrough = (out.pc + (out.guest_insns - 1) * step + step) &
                           (thumb ? ~1u : ~3u);
    out.exit_fallthrough_thumb = thumb;
  }
}

} // namespace

auto PeekCodeHalf(Bus& bus, u32 address) -> u16 {
  return ReadPhysicalHalf(bus, address);
}

auto PeekCodeWord(Bus& bus, u32 address) -> u32 {
  return ReadPhysicalWord(bus, address);
}

auto CompileThumbBlock(Bus& bus, u32 pc, CompiledBlock& out) -> bool {
  out = {};
  out.pc = pc & ~1u;
  out.thumb = true;

  u32 cursor = out.pc;
  for(int i = 0; i < kMaxBlockInsns; ++i) {
    const auto decoded = DecodeThumb(PeekCodeHalf(bus, cursor), cursor);
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
  FinishExits(out, cursor);
  return true;
}

auto CompileArmBlock(Bus& bus, u32 pc, CompiledBlock& out) -> bool {
  out = {};
  out.pc = pc & ~3u;
  out.thumb = false;

  u32 cursor = out.pc;
  for(int i = 0; i < kMaxBlockInsns; ++i) {
    const auto decoded = DecodeArm(PeekCodeWord(bus, cursor), cursor);
    if(decoded.result == DecodeResult::EndBlockBefore) {
      break;
    }
    if(out.ir_count >= kMaxIrOps - 1) {
      break;
    }
    out.ir[out.ir_count++] = decoded.op;
    ++out.guest_insns;
    cursor += 4;
    if(decoded.result == DecodeResult::EndBlockAfter) {
      break;
    }
  }

  if(out.guest_insns == 0) {
    return false;
  }
  FinishExits(out, cursor);
  return true;
}

} // namespace nba::core::arm::dynarec
