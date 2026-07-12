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
  EndBlockAfter,  // control transfer compiled as final op
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
    d.op.aux = static_cast<u8>(op);
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
      d.op.rm = 0xFF;
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
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::AluReg;
    d.op.rd = static_cast<u8>(rd);
    d.op.rm = static_cast<u8>(rs);
    d.op.aux = static_cast<u8>(op);
    return d;
  }

  // THUMB.5 Hi-reg / BX — leave to interpreter (mode switches / BX)
  if((insn & 0xFC00) == 0x4400) {
    d.result = DecodeResult::EndBlockBefore;
    return d;
  }

  // THUMB.6 PC-relative load
  if((insn & 0xF800) == 0x4800) {
    const int rd = (insn >> 8) & 7;
    const u32 offset = (insn & 0xFF) << 2;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemPcRel;
    d.op.rd = static_cast<u8>(rd);
    d.op.imm = offset;
    return d;
  }

  // THUMB.7 Load/store with register offset
  if((insn & 0xF200) == 0x5000) {
    const int op = (insn >> 10) & 3;
    const int ro = (insn >> 6) & 7;
    const int rb = (insn >> 3) & 7;
    const int rd = insn & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemOffsetReg;
    d.op.rd = static_cast<u8>(rd);
    d.op.rn = static_cast<u8>(rb);
    d.op.rm = static_cast<u8>(ro);
    d.op.aux = static_cast<u8>(op);
    return d;
  }

  // THUMB.8 Load/store sign-extended byte/halfword
  if((insn & 0xF200) == 0x5200) {
    const int op = (insn >> 10) & 3;
    const int ro = (insn >> 6) & 7;
    const int rb = (insn >> 3) & 7;
    const int rd = insn & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemSigned;
    d.op.rd = static_cast<u8>(rd);
    d.op.rn = static_cast<u8>(rb);
    d.op.rm = static_cast<u8>(ro);
    d.op.aux = static_cast<u8>(op);
    return d;
  }

  // THUMB.9 Load/store with immediate offset
  if((insn & 0xE000) == 0x6000) {
    const int op = (insn >> 11) & 3;
    const int offset5 = (insn >> 6) & 0x1F;
    const int rb = (insn >> 3) & 7;
    const int rd = insn & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemOffsetImm;
    d.op.rd = static_cast<u8>(rd);
    d.op.rn = static_cast<u8>(rb);
    d.op.aux = static_cast<u8>(op);
    // Scale matches Thumb_LoadStoreOffsetImm (×4 for word, ×1 for byte).
    d.op.imm = (op < 2) ? static_cast<u32>(offset5 * 4) : static_cast<u32>(offset5);
    return d;
  }

  // THUMB.10 Load/store halfword
  if((insn & 0xF000) == 0x8000) {
    const bool load = (insn & (1 << 11)) != 0;
    const int offset5 = (insn >> 6) & 0x1F;
    const int rb = (insn >> 3) & 7;
    const int rd = insn & 7;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemHalfImm;
    d.op.rd = static_cast<u8>(rd);
    d.op.rn = static_cast<u8>(rb);
    d.op.aux = load ? 1 : 0;
    d.op.imm = static_cast<u32>(offset5 * 2);
    return d;
  }

  // THUMB.11 SP-relative load/store
  if((insn & 0xF000) == 0x9000) {
    const bool load = (insn & (1 << 11)) != 0;
    const int rd = (insn >> 8) & 7;
    const u32 offset = (insn & 0xFF) * 4;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::MemSpRel;
    d.op.rd = static_cast<u8>(rd);
    d.op.aux = load ? 1 : 0;
    d.op.imm = offset;
    return d;
  }

  // THUMB.12 Load address
  if((insn & 0xF000) == 0xA000) {
    const bool use_sp = (insn & (1 << 11)) != 0;
    const int rd = (insn >> 8) & 7;
    const u32 offset = (insn & 0xFF) << 2;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::LoadAddress;
    d.op.rd = static_cast<u8>(rd);
    d.op.aux = use_sp ? 1 : 0;
    d.op.imm = offset;
    return d;
  }

  // THUMB.13 Add offset to stack pointer
  if((insn & 0xFF00) == 0xB000) {
    const bool sub = (insn & (1 << 7)) != 0;
    const u32 offset = (insn & 0x7F) * 4;
    d.result = DecodeResult::Emit;
    d.op.kind = IrOpKind::AddSpImm;
    d.op.aux = sub ? 1 : 0;
    d.op.imm = offset;
    return d;
  }

  // THUMB.14 Push/pop registers
  if((insn & 0xF600) == 0xB400) {
    const bool pop = (insn & (1 << 11)) != 0;
    const bool rbit = (insn & (1 << 8)) != 0;
    const u32 list = insn & 0xFF;
    d.op.kind = IrOpKind::PushPop;
    d.op.aux = static_cast<u8>((pop ? 2 : 0) | (rbit ? 1 : 0));
    d.op.imm = list;
    // POP {…,PC} reloads the pipeline — end the block.
    d.result = (pop && rbit) ? DecodeResult::EndBlockAfter : DecodeResult::Emit;
    return d;
  }

  // THUMB.15 Multiple load/store
  if((insn & 0xF000) == 0xC000) {
    const bool load = (insn & (1 << 11)) != 0;
    const int rb = (insn >> 8) & 7;
    const u32 list = insn & 0xFF;
    d.op.kind = IrOpKind::LdmStm;
    d.op.rn = static_cast<u8>(rb);
    d.op.aux = load ? 1 : 0;
    d.op.imm = list;
    // Empty LDM loads PC and reloads the pipeline.
    d.result = (load && list == 0) ? DecodeResult::EndBlockAfter : DecodeResult::Emit;
    return d;
  }

  // THUMB.18 unconditional branch
  if((insn & 0xF800) == 0xE000) {
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

  // Conditional B, BL, SWI, …
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

  // Append Exit unless the last op already transfers control (Branch, POP PC,
  // or empty LDM).
  const auto& last = out.ir[out.ir_count - 1];
  const bool control_transfer =
    last.kind == IrOpKind::Branch ||
    (last.kind == IrOpKind::PushPop && (last.aux & 3) == 3) ||
    (last.kind == IrOpKind::LdmStm && last.aux != 0 && last.imm == 0);

  if(!control_transfer) {
    out.ir[out.ir_count++] = IrOp{.kind = IrOpKind::Exit};
  }

  return true;
}

} // namespace nba::core::arm::dynarec
