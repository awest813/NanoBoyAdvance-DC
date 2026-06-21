// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/arm7tdmi.hh"

namespace nba::core::arm {

void ARM7TDMI::LoadState(SaveState const& save_state) {
  for(int i = 0; i < 16; i++) {
    state.reg[i] = save_state.arm.regs.gpr[i];
  }

  for(int i = 0; i < BANK_COUNT; i++) {
    for(int j = 0; j < 7; j++) {
      state.bank[i][j] = save_state.arm.regs.bank[i][j];
    }
    state.spsr[i].v = save_state.arm.regs.spsr[i];
  }

  state.cpsr.v = save_state.arm.regs.cpsr;

  auto bank = GetRegisterBankByMode(state.cpsr.f.mode);
  if(bank != BANK_NONE) {
    p_spsr = &state.spsr[bank];
  } else {
    p_spsr = &state.cpsr;
  }

  pipe.access = save_state.arm.pipe.access;

  for(int i = 0; i < 2; i++) {
    pipe.opcode[i] = save_state.arm.pipe.opcode[i];
  }

  irq_line = save_state.arm.irq_line;

  ldm_usermode_conflict = save_state.arm.ldm_usermode_conflict;
  cpu_mode_is_invalid = save_state.arm.cpu_mode_is_invalid;
  latch_irq_disable = save_state.arm.latch_irq_disable;
}

void ARM7TDMI::CopyState(SaveState& save_state) {
  for(int i = 0; i < 16; i++) {
    save_state.arm.regs.gpr[i] = state.reg[i];
  }

  for(int i = 0; i < BANK_COUNT; i++) {
    for(int j = 0; j < 7; j++) {
      save_state.arm.regs.bank[i][j] = state.bank[i][j];
    }
    save_state.arm.regs.spsr[i] = state.spsr[i].v;
  }

  save_state.arm.regs.cpsr = state.cpsr.v;

  save_state.arm.pipe.access = pipe.access;

  for(int i = 0; i < 2; i++) {
    save_state.arm.pipe.opcode[i] = pipe.opcode[i];
  }

  save_state.arm.irq_line = irq_line;

  save_state.arm.ldm_usermode_conflict = ldm_usermode_conflict;
  save_state.arm.cpu_mode_is_invalid = cpu_mode_is_invalid;
  save_state.arm.latch_irq_disable = latch_irq_disable;
}

} // namespace nba::core::arm
