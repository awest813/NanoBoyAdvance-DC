// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/dynarec.hh"
#include "arm/dynarec/ir_exec.hh"
#include "arm/dynarec/thumb_compiler.hh"
#include "arm/arm7tdmi.hh"

namespace nba::core::arm::dynarec {

auto Dynarec::TryRunBlock() -> bool {
  if(!cpu_.state.cpsr.f.thumb) {
    return false;
  }

  // Guest PC of the next instruction to execute is r15 - 4 in Thumb.
  const u32 pc = (cpu_.state.r15 - 4) & ~1u;
  const u32 key = BlockKey(pc, true);

  CompiledBlock* block = cache_.Find(key);
  if(block == nullptr) {
    CompiledBlock compiled{};
    if(!CompileThumbBlock(cpu_.bus, pc, compiled)) {
      return false;
    }
    block = cache_.Insert(key, compiled);
  }

  if(block->native != nullptr) {
    block->native(&cpu_);
    return true;
  }

  return ExecuteIrBlock(cpu_, *block);
}

} // namespace nba::core::arm::dynarec
