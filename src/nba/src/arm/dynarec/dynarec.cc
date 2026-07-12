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
  pending_invalidate_ = false;
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

void Dynarec::ApplyPendingInvalidation() {
  if(!pending_invalidate_) {
    return;
  }
  pending_invalidate_ = false;
  // InvalidateAll clears pending again; keep the counter already bumped.
  cache_.Clear();
  arena_.Reset();
  ClearCodePages();
}

void Dynarec::AdjustCompiledPages(CompiledBlock const& block, int delta) {
  if(block.guest_insns == 0 || delta == 0) {
    return;
  }

  const u32 bytes = static_cast<u32>(block.guest_insns) * (block.thumb ? 2u : 4u);
  if(bytes == 0) {
    return;
  }

  const u32 page = block.pc >> 24;

  auto bump = [delta](u8& cell) {
    if(delta > 0) {
      if(cell < 255) {
        ++cell;
      }
    } else if(cell > 0) {
      --cell;
    }
  };

  if(page == 0x03) {
    const u32 start = block.pc & (kIwramBytes - 1);
    const u32 first = start >> kPageShift;
    const u32 last = (start + bytes - 1) >> kPageShift;
    for(u32 p = first; p <= last; ++p) {
      bump(iwram_code_pages_[p & (kIwramPages - 1)]);
    }
  } else if(page == 0x02) {
    const u32 start = block.pc & (kEwramBytes - 1);
    const u32 first = start >> kPageShift;
    const u32 last = (start + bytes - 1) >> kPageShift;
    for(u32 p = first; p <= last; ++p) {
      bump(ewram_code_pages_[p & (kEwramPages - 1)]);
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
    const u32 first = start >> kPageShift;
    const u32 last = (start + size - 1) >> kPageShift;
    for(u32 p = first; p <= last; ++p) {
      if(iwram_code_pages_[p & (kIwramPages - 1)] != 0) {
        return true;
      }
    }
    return false;
  }

  if(page == 0x02) {
    const u32 start = address & (kEwramBytes - 1);
    const u32 first = start >> kPageShift;
    const u32 last = (start + size - 1) >> kPageShift;
    for(u32 p = first; p <= last; ++p) {
      if(ewram_code_pages_[p & (kEwramPages - 1)] != 0) {
        return true;
      }
    }
    return false;
  }

  return false;
}

void Dynarec::InvalidateRange(u32 address, u32 size) {
  if(!PageHasCode(address, size)) {
    return;
  }

  if(!pending_invalidate_) {
    ++invalidations_;
  }
  pending_invalidate_ = true;

  // Outside the dispatcher, flush immediately (cheats / Poke* / interpreter).
  // Inside TryRunBlock, wait until the current block finishes so the live
  // CompiledBlock is not cleared mid-execution.
  if(dispatch_depth_ == 0) {
    ApplyPendingInvalidation();
  }
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
  if(native.code != nullptr) {
    compiled.native = native.entry;
    compiled.native_code = native.code;
    compiled.native_size = native.size;
  } else if(compiled.ir_count > 0) {
    // Emit succeeded but the bump arena is full — run IR now and reclaim
    // native space at the next block boundary.
    pending_invalidate_ = true;
  }

  CompiledBlock evicted{};
  bool did_evict = false;
  auto* inserted = cache_.Insert(key, compiled, &evicted, &did_evict);
  if(did_evict) {
    AdjustCompiledPages(evicted, -1);
  }
  AdjustCompiledPages(*inserted, +1);
  return inserted;
}

auto Dynarec::ExecuteBlock(CompiledBlock& block) -> bool {
  if(block.native != nullptr) {
    return block.native(&cpu_, block.ir.data(), block.ir_count);
  }
  return ExecuteIrBlock(cpu_, block);
}

auto Dynarec::TryRunBlock() -> bool {
  struct DepthGuard {
    int& depth;
    explicit DepthGuard(int& d) : depth(d) { ++depth; }
    ~DepthGuard() { --depth; }
  } guard{dispatch_depth_};

  // Interpreter / prior deferred work may have left a pending flush.
  ApplyPendingInvalidation();

  bool ran_any = false;

  for(int chain = 0; chain < kMaxBlockChain; ++chain) {
    const bool thumb = cpu_.state.cpsr.f.thumb != 0;
    const u32 pc = thumb ? ((cpu_.state.r15 - 4) & ~1u)
                         : ((cpu_.state.r15 - 8) & ~3u);

    CompiledBlock* block = LookupOrCompile(pc, thumb);
    if(block == nullptr) {
      break;
    }

    // Snapshot soft-link targets before ExecuteBlock: an SMC store may schedule
    // a flush that clears *block before we read these fields again.
    const u32 exit_taken = block->exit_taken;
    const u32 exit_fallthrough = block->exit_fallthrough;
    const bool exit_taken_thumb = block->exit_taken_thumb;
    const bool exit_fallthrough_thumb = block->exit_fallthrough_thumb;

    ran_any = true;
    const bool irq_exit = !ExecuteBlock(*block);

    const bool flushed = pending_invalidate_;
    ApplyPendingInvalidation();
    if(irq_exit) {
      return true;
    }
    if(flushed) {
      break;
    }

    const bool next_thumb = cpu_.state.cpsr.f.thumb != 0;
    const u32 next_pc = next_thumb ? ((cpu_.state.r15 - 4) & ~1u)
                                   : ((cpu_.state.r15 - 8) & ~3u);

    const bool matches_taken =
      exit_taken != 0 &&
      next_pc == exit_taken &&
      next_thumb == exit_taken_thumb;
    const bool matches_fallthrough =
      exit_fallthrough != 0 &&
      next_pc == exit_fallthrough &&
      next_thumb == exit_fallthrough_thumb;

    if(!matches_taken && !matches_fallthrough) {
      break;
    }

    if(!cache_.Contains(BlockKey(next_pc, next_thumb))) {
      break;
    }
  }

  ApplyPendingInvalidation();
  return ran_any;
}

} // namespace nba::core::arm::dynarec
