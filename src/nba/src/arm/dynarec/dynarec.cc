// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/dynarec.hh"
#include "arm/dynarec/ir_exec.hh"
#include "arm/dynarec/sh4_compile.hh"
#include "arm/dynarec/thumb_compiler.hh"
#include "arm/arm7tdmi.hh"

namespace nba::core::arm::dynarec {

void Dynarec::Reset() {
  cache_.Clear();
  arena_.Reset();
}

void Dynarec::InvalidateAll() {
  cache_.Clear();
  arena_.Reset();
}

auto Dynarec::TryRunBlock() -> bool {
  if(!cpu_.state.cpsr.f.thumb) {
    return false;
  }

  const u32 pc = (cpu_.state.r15 - 4) & ~1u;
  const u32 key = BlockKey(pc, true);

  CompiledBlock* block = cache_.Find(key);
  if(block == nullptr) {
    CompiledBlock compiled{};
    if(!CompileThumbBlock(cpu_.bus, pc, compiled)) {
      return false;
    }

    const auto native = TryCompileSh4Block(compiled, arena_);
    if(native.entry != nullptr) {
      compiled.native = native.entry;
      compiled.native_code = native.code;
      compiled.native_size = native.size;
    }

    block = cache_.Insert(key, compiled);
  }

  if(block->native != nullptr) {
    return block->native(&cpu_, block->ir.data(), block->ir_count);
  }

  return ExecuteIrBlock(cpu_, *block);
}

} // namespace nba::core::arm::dynarec
