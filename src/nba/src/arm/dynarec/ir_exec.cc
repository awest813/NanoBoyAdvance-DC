// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/ir_exec.hh"
#include "arm/dynarec/sh4_helpers.hh"
#include "arm/arm7tdmi.hh"

namespace nba::core::arm {

namespace {

enum class ThumbDataOp {
  AND = 0,
  EOR = 1,
  LSL = 2,
  LSR = 3,
  ASR = 4,
  ADC = 5,
  SBC = 6,
  ROR = 7,
  TST = 8,
  NEG = 9,
  CMP = 10,
  CMN = 11,
  ORR = 12,
  MUL = 13,
  BIC = 14,
  MVN = 15
};

} // namespace

auto ARM7TDMI::RunOneIrOp(dynarec::IrOp const& op) -> dynarec::IrStepResult {
  using namespace dynarec;
  using Access = Bus::Access;

  if(op.kind == IrOpKind::Exit) {
    return IrStepResult::Done;
  }

  if(IRQLine()) {
    SignalIRQ();
    return IrStepResult::IrqExit;
  }

  latch_irq_disable = state.cpsr.f.mask_irq;
  state.r15 &= ~1;

  pipe.opcode[0] = pipe.opcode[1];
  if(state.cpsr.f.thumb) {
    pipe.opcode[1] = ReadHalf(state.r15, pipe.access);
  } else {
    pipe.opcode[1] = ReadWord(state.r15, pipe.access);
  }

  switch(op.kind) {
    case IrOpKind::MovImm: {
      state.reg[op.rd] = op.imm;
      state.cpsr.f.n = 0;
      state.cpsr.f.z = (op.imm == 0);
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::CmpImm: {
      SUB(state.reg[op.rd], op.imm, true);
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::AddImm: {
      state.reg[op.rd] = ADD(state.reg[op.rd], op.imm, true);
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::SubImm: {
      state.reg[op.rd] = SUB(state.reg[op.rd], op.imm, true);
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::MovShifted: {
      int carry = state.cpsr.f.c;
      u32 result = state.reg[op.rm];
      DoShift(op.aux, result, static_cast<u8>(op.imm), carry, true);
      state.cpsr.f.c = carry;
      SetZeroAndSignFlag(result);
      state.reg[op.rd] = result;
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::AddReg: {
      const u32 operand = (op.rm == 0xFF) ? op.imm : state.reg[op.rm];
      state.reg[op.rd] = ADD(state.reg[op.rn], operand, true);
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::SubReg: {
      const u32 operand = (op.rm == 0xFF) ? op.imm : state.reg[op.rm];
      state.reg[op.rd] = SUB(state.reg[op.rn], operand, true);
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::AluReg: {
      const int dst = op.rd;
      const int src = op.rm;

      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;

      switch(static_cast<ThumbDataOp>(op.aux)) {
        case ThumbDataOp::AND:
          state.reg[dst] &= state.reg[src];
          SetZeroAndSignFlag(state.reg[dst]);
          break;
        case ThumbDataOp::EOR:
          state.reg[dst] ^= state.reg[src];
          SetZeroAndSignFlag(state.reg[dst]);
          break;
        case ThumbDataOp::LSL: {
          const auto shift = state.reg[src];
          bus.Idle();
          pipe.access = Access::Code | Access::Nonsequential;
          int carry = state.cpsr.f.c;
          LSL(state.reg[dst], shift, carry);
          SetZeroAndSignFlag(state.reg[dst]);
          state.cpsr.f.c = carry;
          break;
        }
        case ThumbDataOp::LSR: {
          const auto shift = state.reg[src];
          bus.Idle();
          pipe.access = Access::Code | Access::Nonsequential;
          int carry = state.cpsr.f.c;
          LSR(state.reg[dst], shift, carry, false);
          SetZeroAndSignFlag(state.reg[dst]);
          state.cpsr.f.c = carry;
          break;
        }
        case ThumbDataOp::ASR: {
          const auto shift = state.reg[src];
          bus.Idle();
          pipe.access = Access::Code | Access::Nonsequential;
          int carry = state.cpsr.f.c;
          ASR(state.reg[dst], shift, carry, false);
          SetZeroAndSignFlag(state.reg[dst]);
          state.cpsr.f.c = carry;
          break;
        }
        case ThumbDataOp::ADC:
          state.reg[dst] = ADC(state.reg[dst], state.reg[src], true);
          break;
        case ThumbDataOp::SBC:
          state.reg[dst] = SBC(state.reg[dst], state.reg[src], true);
          break;
        case ThumbDataOp::ROR: {
          const auto shift = state.reg[src];
          bus.Idle();
          pipe.access = Access::Code | Access::Nonsequential;
          int carry = state.cpsr.f.c;
          ROR(state.reg[dst], shift, carry, false);
          SetZeroAndSignFlag(state.reg[dst]);
          state.cpsr.f.c = carry;
          break;
        }
        case ThumbDataOp::TST:
          SetZeroAndSignFlag(state.reg[dst] & state.reg[src]);
          break;
        case ThumbDataOp::NEG:
          state.reg[dst] = SUB(0, state.reg[src], true);
          break;
        case ThumbDataOp::CMP:
          SUB(state.reg[dst], state.reg[src], true);
          break;
        case ThumbDataOp::CMN:
          ADD(state.reg[dst], state.reg[src], true);
          break;
        case ThumbDataOp::ORR:
          state.reg[dst] |= state.reg[src];
          SetZeroAndSignFlag(state.reg[dst]);
          break;
        case ThumbDataOp::MUL: {
          const u32 lhs = state.reg[src];
          const u32 rhs = state.reg[dst];
          const bool full = TickMultiply(rhs);
          pipe.access = Access::Code | Access::Nonsequential;
          state.reg[dst] = lhs * rhs;
          SetZeroAndSignFlag(state.reg[dst]);
          if(full) {
            state.cpsr.f.c = MultiplyCarrySimple(rhs);
          } else {
            state.cpsr.f.c = MultiplyCarryLo(lhs, rhs);
          }
          break;
        }
        case ThumbDataOp::BIC:
          state.reg[dst] &= ~state.reg[src];
          SetZeroAndSignFlag(state.reg[dst]);
          break;
        case ThumbDataOp::MVN:
          state.reg[dst] = ~state.reg[src];
          SetZeroAndSignFlag(state.reg[dst]);
          break;
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::MemPcRel: {
      const u32 address = (state.r15 & ~2u) + op.imm;
      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;
      state.reg[op.rd] = ReadWord(address, Access::Nonsequential);
      bus.Idle();
      return IrStepResult::Continue;
    }
    case IrOpKind::MemOffsetReg: {
      const u32 address = state.reg[op.rn] + state.reg[op.rm];
      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;
      switch(op.aux) {
        case 0:
          WriteWord(address, state.reg[op.rd], Access::Nonsequential);
          break;
        case 1:
          WriteByte(address, static_cast<u8>(state.reg[op.rd]), Access::Nonsequential);
          break;
        case 2:
          state.reg[op.rd] = ReadWordRotate(address, Access::Nonsequential);
          bus.Idle();
          break;
        default:
          state.reg[op.rd] = ReadByte(address, Access::Nonsequential);
          bus.Idle();
          break;
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::MemSigned: {
      const u32 address = state.reg[op.rn] + state.reg[op.rm];
      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;
      switch(op.aux) {
        case 0:
          WriteHalf(address, state.reg[op.rd], Access::Nonsequential);
          break;
        case 1:
          state.reg[op.rd] = ReadByteSigned(address, Access::Nonsequential);
          bus.Idle();
          break;
        case 2:
          state.reg[op.rd] = ReadHalfRotate(address, Access::Nonsequential);
          bus.Idle();
          break;
        default:
          state.reg[op.rd] = ReadHalfSigned(address, Access::Nonsequential);
          bus.Idle();
          break;
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::MemOffsetImm: {
      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;
      switch(op.aux) {
        case 0:
          WriteWord(state.reg[op.rn] + op.imm, state.reg[op.rd], Access::Nonsequential);
          break;
        case 1:
          state.reg[op.rd] = ReadWordRotate(state.reg[op.rn] + op.imm, Access::Nonsequential);
          bus.Idle();
          break;
        case 2:
          WriteByte(state.reg[op.rn] + op.imm, static_cast<u8>(state.reg[op.rd]), Access::Nonsequential);
          break;
        default:
          state.reg[op.rd] = ReadByte(state.reg[op.rn] + op.imm, Access::Nonsequential);
          bus.Idle();
          break;
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::MemHalfImm: {
      const u32 address = state.reg[op.rn] + op.imm;
      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;
      if(op.aux) {
        state.reg[op.rd] = ReadHalfRotate(address, Access::Nonsequential);
        bus.Idle();
      } else {
        WriteHalf(address, state.reg[op.rd], Access::Nonsequential);
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::MemSpRel: {
      const u32 address = state.r13 + op.imm;
      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;
      if(op.aux) {
        state.reg[op.rd] = ReadWordRotate(address, Access::Nonsequential);
        bus.Idle();
      } else {
        WriteWord(address, state.reg[op.rd], Access::Nonsequential);
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::LoadAddress: {
      if(op.aux) {
        state.reg[op.rd] = state.r13 + op.imm;
      } else {
        state.reg[op.rd] = (state.r15 & ~2u) + op.imm;
      }
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::AddSpImm: {
      state.r13 = state.r13 + (op.aux ? -static_cast<s32>(op.imm) : static_cast<s32>(op.imm));
      pipe.access = Access::Code | Access::Sequential;
      state.r15 += 2;
      return IrStepResult::Continue;
    }
    case IrOpKind::PushPop: {
      const bool pop = (op.aux & 2) != 0;
      const bool rbit = (op.aux & 1) != 0;
      const auto list = static_cast<u16>(op.imm & 0xFF);

      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;

      if(list == 0 && !rbit) {
        if(pop) {
          state.r15 = ReadWord(state.r13, Access::Nonsequential);
          ReloadPipeline16();
          state.r13 += 0x40;
          return IrStepResult::Done;
        }
        state.r13 -= 0x40;
        WriteWord(state.r13, state.r15, Access::Nonsequential);
        return IrStepResult::Continue;
      }

      auto address = state.r13;
      auto access_type = Access::Nonsequential;

      if(pop) {
        for(int reg = 0; reg <= 7; reg++) {
          if(list & (1 << reg)) {
            state.reg[reg] = ReadWord(address, access_type);
            access_type = Access::Sequential;
            address += 4;
          }
        }

        if(rbit) {
          state.reg[15] = ReadWord(address, access_type) & ~1u;
          state.r13 = address + 4;
          bus.Idle();
          ReloadPipeline16();
          return IrStepResult::Done;
        }

        bus.Idle();
        state.r13 = address;
      } else {
        for(int reg = 0; reg <= 7; reg++) {
          if(list & (1 << reg)) {
            address -= 4;
          }
        }
        if(rbit) {
          address -= 4;
        }

        state.r13 = address;

        for(int reg = 0; reg <= 7; reg++) {
          if(list & (1 << reg)) {
            WriteWord(address, state.reg[reg], access_type);
            access_type = Access::Sequential;
            address += 4;
          }
        }

        if(rbit) {
          WriteWord(address, state.r14, access_type);
        }
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::LdmStm: {
      const bool load = op.aux != 0;
      const int base = op.rn;
      const auto list = static_cast<u16>(op.imm & 0xFF);

      pipe.access = Access::Code | Access::Nonsequential;
      state.r15 += 2;

      if(list == 0) {
        if(load) {
          state.r15 = ReadWord(state.reg[base], Access::Nonsequential);
          ReloadPipeline16();
        } else {
          WriteWord(state.reg[base], state.r15, Access::Nonsequential);
        }
        state.reg[base] += 0x40;
        return load ? IrStepResult::Done : IrStepResult::Continue;
      }

      if(load) {
        u32 address = state.reg[base];
        auto access_type = Access::Nonsequential;

        for(int i = 0; i <= 7; i++) {
          if(list & (1 << i)) {
            state.reg[i] = ReadWord(address, access_type);
            access_type = Access::Sequential;
            address += 4;
          }
        }
        bus.Idle();
        if(~list & (1 << base)) {
          state.reg[base] = address;
        }
      } else {
        int count = 0;
        int first = 0;

        for(int reg = 7; reg >= 0; reg--) {
          if(list & (1 << reg)) {
            count++;
            first = reg;
          }
        }

        u32 address = state.reg[base];
        const u32 base_new = address + count * 4;

        WriteWord(address, state.reg[first], Access::Nonsequential);
        state.reg[base] = base_new;
        address += 4;

        for(int reg = first + 1; reg <= 7; reg++) {
          if(list & (1 << reg)) {
            WriteWord(address, state.reg[reg], Access::Sequential);
            address += 4;
          }
        }
      }
      return IrStepResult::Continue;
    }
    case IrOpKind::Branch: {
      state.r15 = op.imm;
      if(state.cpsr.f.thumb) {
        ReloadPipeline16();
      } else {
        ReloadPipeline32();
      }
      return IrStepResult::Done;
    }
    case IrOpKind::CondBranch: {
      if(CheckCondition(static_cast<Condition>(op.aux))) {
        state.r15 = op.imm;
        if(state.cpsr.f.thumb) {
          ReloadPipeline16();
        } else {
          ReloadPipeline32();
        }
      } else if(state.cpsr.f.thumb) {
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
      } else {
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 4;
      }
      return IrStepResult::Done;
    }
    case IrOpKind::ArmDataImm: {
      if(!CheckCondition(static_cast<Condition>(op.rm))) {
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 4;
        return IrStepResult::Continue;
      }

      const int opcode = op.aux & 0xF;
      const bool set_flags = (op.aux & 0x10) != 0;
      const int value = op.imm & 0xFF;
      const int shift = ((op.imm >> 8) & 0xF) * 2;
      int carry = state.cpsr.f.c;
      u32 op2 = static_cast<u32>(value);
      if(shift != 0) {
        carry = (value >> (shift - 1)) & 1;
        op2 = (op2 >> shift) | (op2 << (32 - shift));
      }

      const u32 op1 = state.reg[op.rn];
      pipe.access = Access::Code | Access::Sequential;

      switch(opcode) {
        case 0: { // AND
          const u32 result = op1 & op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 1: { // EOR
          const u32 result = op1 ^ op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 2: // SUB
          state.reg[op.rd] = SUB(op1, op2, set_flags);
          break;
        case 3: // RSB
          state.reg[op.rd] = SUB(op2, op1, set_flags);
          break;
        case 4: // ADD
          state.reg[op.rd] = ADD(op1, op2, set_flags);
          break;
        case 5: // ADC
          state.reg[op.rd] = ADC(op1, op2, set_flags);
          break;
        case 6: // SBC
          state.reg[op.rd] = SBC(op1, op2, set_flags);
          break;
        case 7: // RSC
          state.reg[op.rd] = SBC(op2, op1, set_flags);
          break;
        case 8: // TST
          SetZeroAndSignFlag(op1 & op2);
          state.cpsr.f.c = carry;
          break;
        case 9: // TEQ
          SetZeroAndSignFlag(op1 ^ op2);
          state.cpsr.f.c = carry;
          break;
        case 10: // CMP
          SUB(op1, op2, true);
          break;
        case 11: // CMN
          ADD(op1, op2, true);
          break;
        case 12: { // ORR
          const u32 result = op1 | op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 13: { // MOV
          if(set_flags) { SetZeroAndSignFlag(op2); state.cpsr.f.c = carry; }
          state.reg[op.rd] = op2;
          break;
        }
        case 14: { // BIC
          const u32 result = op1 & ~op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 15: { // MVN
          const u32 result = ~op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
      }
      state.r15 += 4;
      return IrStepResult::Continue;
    }
    case IrOpKind::ArmDataReg: {
      const int cond = (op.imm >> 8) & 0xF;
      if(!CheckCondition(static_cast<Condition>(cond))) {
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 4;
        return IrStepResult::Continue;
      }

      const int opcode = op.aux & 0xF;
      const bool set_flags = (op.aux & 0x10) != 0;
      const int shift_type = (op.aux >> 5) & 3;
      const u8 shift_imm = static_cast<u8>(op.imm & 0x1F);
      int carry = state.cpsr.f.c;
      u32 op2 = state.reg[op.rm];
      DoShift(shift_type, op2, shift_imm, carry, true);
      const u32 op1 = state.reg[op.rn];
      pipe.access = Access::Code | Access::Sequential;

      switch(opcode) {
        case 0: {
          const u32 result = op1 & op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 1: {
          const u32 result = op1 ^ op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 2: state.reg[op.rd] = SUB(op1, op2, set_flags); break;
        case 3: state.reg[op.rd] = SUB(op2, op1, set_flags); break;
        case 4: state.reg[op.rd] = ADD(op1, op2, set_flags); break;
        case 5: state.reg[op.rd] = ADC(op1, op2, set_flags); break;
        case 6: state.reg[op.rd] = SBC(op1, op2, set_flags); break;
        case 7: state.reg[op.rd] = SBC(op2, op1, set_flags); break;
        case 8:
          SetZeroAndSignFlag(op1 & op2);
          state.cpsr.f.c = carry;
          break;
        case 9:
          SetZeroAndSignFlag(op1 ^ op2);
          state.cpsr.f.c = carry;
          break;
        case 10: SUB(op1, op2, true); break;
        case 11: ADD(op1, op2, true); break;
        case 12: {
          const u32 result = op1 | op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 13:
          if(set_flags) { SetZeroAndSignFlag(op2); state.cpsr.f.c = carry; }
          state.reg[op.rd] = op2;
          break;
        case 14: {
          const u32 result = op1 & ~op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
        case 15: {
          const u32 result = ~op2;
          if(set_flags) { SetZeroAndSignFlag(result); state.cpsr.f.c = carry; }
          state.reg[op.rd] = result;
          break;
        }
      }
      state.r15 += 4;
      return IrStepResult::Continue;
    }
    case IrOpKind::Exit:
      return IrStepResult::Done;
  }

  return IrStepResult::Done;
}

auto ARM7TDMI::RunIrBlock(dynarec::CompiledBlock const& block) -> bool {
  for(u16 i = 0; i < block.ir_count; ++i) {
    switch(RunOneIrOp(block.ir[i])) {
      case dynarec::IrStepResult::Continue:
        continue;
      case dynarec::IrStepResult::Done:
        return true;
      case dynarec::IrStepResult::IrqExit:
        return false;
    }
  }

  return true;
}

namespace dynarec {

auto ExecuteIrBlock(ARM7TDMI& cpu, CompiledBlock const& block) -> bool {
  return cpu.RunIrBlock(block);
}

} // namespace dynarec
} // namespace nba::core::arm

extern "C" int nba_dr_run_one_op(void* cpu, void const* op_ptr) {
  auto& arm = *static_cast<nba::core::arm::ARM7TDMI*>(cpu);
  auto& op = *static_cast<const nba::core::arm::dynarec::IrOp*>(op_ptr);

  switch(arm.RunOneIrOp(op)) {
    case nba::core::arm::dynarec::IrStepResult::Continue:
      return nba::core::arm::dynarec::kDrStepContinue;
    case nba::core::arm::dynarec::IrStepResult::Done:
      return nba::core::arm::dynarec::kDrStepDone;
    case nba::core::arm::dynarec::IrStepResult::IrqExit:
      return nba::core::arm::dynarec::kDrStepIrqExit;
  }

  return nba::core::arm::dynarec::kDrStepDone;
}

extern "C" std::size_t nba_dr_ir_op_stride() {
  return sizeof(nba::core::arm::dynarec::IrOp);
}
