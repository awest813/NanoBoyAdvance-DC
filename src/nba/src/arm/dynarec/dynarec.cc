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

auto Dynarec::LookupOrCompile(u32 pc, bool thumb) -> CompiledBlock* {
  const u32 key = BlockKey(pc, thumb);
  if(auto* block = cache_.Find(key)) {
    return block;
  }

  CompiledBlock compiled{};
  const bool ok = thumb ? CompileThumbBlock(cpu_.bus, pc, compiled)
                        : CompileArmBlock(cpu_.bus, pc, compiled);
  if(!ok) {
    return nullptr;
  }

  const auto native = TryCompileSh4Block(compiled, arena_);
  if(native.entry != nullptr) {
    compiled.native = native.entry;
    compiled.native_code = native.code;
    compiled.native_size = native.size;
  }

  return cache_.Insert(key, compiled);
}

auto Dynarec::ExecuteBlock(CompiledBlock& block) -> bool {
  if(block.native != nullptr) {
    return block.native(&cpu_, block.ir.data(), block.ir_count);
  }
  return ExecuteIrBlock(cpu_, block);
}

auto Dynarec::TryRunBlock() -> bool {
  bool ran_any = false;

  for(int chain = 0; chain < kMaxBlockChain; ++chain) {
    const bool thumb = cpu_.state.cpsr.f.thumb != 0;
    const u32 pc = thumb ? ((cpu_.state.r15 - 4) & ~1u)
                         : ((cpu_.state.r15 - 8) & ~3u);

    CompiledBlock* block = LookupOrCompile(pc, thumb);
    if(block == nullptr) {
      return ran_any;
    }

    ran_any = true;
    if(!ExecuteBlock(*block)) {
      // IRQ taken mid-block — leave the dispatcher.
      return true;
    }

    // Soft block linking: if the next PC matches a known exit and is cached,
    // continue chaining without returning to Core::Run.
    const bool next_thumb = cpu_.state.cpsr.f.thumb != 0;
    const u32 next_pc = next_thumb ? ((cpu_.state.r15 - 4) & ~1u)
                                   : ((cpu_.state.r15 - 8) & ~3u);

    const bool matches_taken =
      block->exit_taken != 0 &&
      next_pc == block->exit_taken &&
      next_thumb == block->exit_taken_thumb;
    const bool matches_fallthrough =
      block->exit_fallthrough != 0 &&
      next_pc == block->exit_fallthrough &&
      next_thumb == block->exit_fallthrough_thumb;

    if(!matches_taken && !matches_fallthrough) {
      break;
    }

    // Prefer Find (no miss counter bump on chain) — LookupOrCompile still works.
    if(cache_.Find(BlockKey(next_pc, next_thumb)) == nullptr) {
      break;
    }
  }

  return ran_any;
}

} // namespace nba::core::arm::dynarec
