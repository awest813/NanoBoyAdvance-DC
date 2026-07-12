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
  pipe.opcode[1] = ReadHalf(state.r15, pipe.access);

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
    case IrOpKind::Branch: {
      state.r15 = op.imm;
      ReloadPipeline16();
      return IrStepResult::Done;
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
