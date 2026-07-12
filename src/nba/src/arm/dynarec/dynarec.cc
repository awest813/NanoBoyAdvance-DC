// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/dynarec.hh"
#include "arm/dynarec/ir_exec.hh"
#include "arm/dynarec/sh4_compile.hh"
#include "arm/dynarec/thumb_compiler.hh"
#include "arm/arm7tdmi.hh"

namespace nba::core::arm::dynarec {

void Dynarec::Reset() {
  InvalidateAll();
  invalidations_ = 0;
  u64 discard_hits = 0;
  u64 discard_misses = 0;
  cache_.TakeStats(discard_hits, discard_misses);
}

void Dynarec::InvalidateAll() {
  cache_.Clear();
  arena_.Reset();
  ClearCodePages();
}

void Dynarec::ClearCodePages() {
  iwram_code_pages_.fill(0);
  ewram_code_pages_.fill(0);
}

void Dynarec::TakeTelemetry(u64& hits, u64& misses, u32& invalidations) {
  cache_.TakeStats(hits, misses);
  invalidations = invalidations_;
  invalidations_ = 0;
}

void Dynarec::MarkCompiledPages(CompiledBlock const& block) {
  if(block.guest_insns == 0) {
    return;
  }

  const u32 bytes = static_cast<u32>(block.guest_insns) * (block.thumb ? 2u : 4u);
  const u32 page = block.pc >> 24;

  if(page == 0x03) {
    const u32 start = block.pc & (kIwramBytes - 1);
    const u32 end = start + bytes;
    for(u32 off = start; off < end; off += kPageSize) {
      iwram_code_pages_[(off & (kIwramBytes - 1)) >> kPageShift] = 1;
    }
    if(bytes > 0) {
      iwram_code_pages_[((end - 1) & (kIwramBytes - 1)) >> kPageShift] = 1;
    }
  } else if(page == 0x02) {
    const u32 start = block.pc & (kEwramBytes - 1);
    const u32 end = start + bytes;
    for(u32 off = start; off < end; off += kPageSize) {
      ewram_code_pages_[(off & (kEwramBytes - 1)) >> kPageShift] = 1;
    }
    if(bytes > 0) {
      ewram_code_pages_[((end - 1) & (kEwramBytes - 1)) >> kPageShift] = 1;
    }
  }
}

auto Dynarec::PageHasCode(u32 address, u32 size) const -> bool {
  if(size == 0) {
    return false;
  }

  const u32 page = address >> 24;
  if(page == 0x03) {
    const u32 start = address & (kIwramBytes - 1);
    const u32 end = start + size;
    for(u32 off = start; off < end; off += kPageSize) {
      if(iwram_code_pages_[(off & (kIwramBytes - 1)) >> kPageShift] != 0) {
        return true;
      }
    }
    return iwram_code_pages_[((end - 1) & (kIwramBytes - 1)) >> kPageShift] != 0;
  }

  if(page == 0x02) {
    const u32 start = address & (kEwramBytes - 1);
    const u32 end = start + size;
    for(u32 off = start; off < end; off += kPageSize) {
      if(ewram_code_pages_[(off & (kEwramBytes - 1)) >> kPageShift] != 0) {
        return true;
      }
    }
    return ewram_code_pages_[((end - 1) & (kEwramBytes - 1)) >> kPageShift] != 0;
  }

  return false;
}

void Dynarec::InvalidateRange(u32 address, u32 size) {
  if(!PageHasCode(address, size)) {
    return;
  }

  ++invalidations_;
  // Full flush: CodeArena is a bump allocator and cannot reclaim individual
  // native blocks. Page bitmap early-out keeps this rare for data-only writes.
  InvalidateAll();
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

  auto* inserted = cache_.Insert(key, compiled);
  MarkCompiledPages(*inserted);
  return inserted;
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

    // Prefer Contains (no miss counter bump on chain) — LookupOrCompile still works.
    if(!cache_.Contains(BlockKey(next_pc, next_thumb))) {
      break;
    }
  }

  return ran_any;
}

} // namespace nba::core::arm::dynarec
