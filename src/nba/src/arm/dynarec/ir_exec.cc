// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/ir_exec.hh"
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

auto ARM7TDMI::RunIrBlock(dynarec::CompiledBlock const& block) -> bool {
  using namespace dynarec;
  using Access = Bus::Access;

  for(u16 i = 0; i < block.ir_count; ++i) {
    const IrOp& op = block.ir[i];

    if(op.kind == IrOpKind::Exit) {
      return true;
    }

    // Mirror ARM7TDMI::Run() prologue for one Thumb instruction.
    if(IRQLine()) {
      SignalIRQ();
      return false;
    }

    latch_irq_disable = state.cpsr.f.mask_irq;
    state.r15 &= ~1;

    pipe.opcode[0] = pipe.opcode[1];
    pipe.opcode[1] = ReadHalf(state.r15, pipe.access);

    switch(op.kind) {
      case IrOpKind::MovImm: {
        state.reg[op.rd] = op.imm;
        state.cpsr.f.n = 0;
        state.cpsr.f.z = (op.imm == 0);
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
        break;
      }
      case IrOpKind::CmpImm: {
        SUB(state.reg[op.rd], op.imm, true);
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
        break;
      }
      case IrOpKind::AddImm: {
        state.reg[op.rd] = ADD(state.reg[op.rd], op.imm, true);
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
        break;
      }
      case IrOpKind::SubImm: {
        state.reg[op.rd] = SUB(state.reg[op.rd], op.imm, true);
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
        break;
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
        break;
      }
      case IrOpKind::AddReg: {
        const u32 operand = (op.rm == 0xFF) ? op.imm : state.reg[op.rm];
        state.reg[op.rd] = ADD(state.reg[op.rn], operand, true);
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
        break;
      }
      case IrOpKind::SubReg: {
        const u32 operand = (op.rm == 0xFF) ? op.imm : state.reg[op.rm];
        state.reg[op.rd] = SUB(state.reg[op.rn], operand, true);
        pipe.access = Access::Code | Access::Sequential;
        state.r15 += 2;
        break;
      }
      case IrOpKind::AluReg: {
        const int dst = op.rd;
        const int src = op.rm;

        // Match Thumb_ALU: advance PC before the body, then maybe override access.
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
        break;
      }
      case IrOpKind::Branch: {
        state.r15 = op.imm;
        ReloadPipeline16();
        return true;
      }
      case IrOpKind::Exit:
        return true;
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
