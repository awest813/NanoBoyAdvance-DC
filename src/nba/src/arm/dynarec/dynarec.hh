// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/block_cache.hh"
#include "arm/dynarec/code_arena.hh"
#include "arm/dynarec/ir.hh"

#include <array>
#include <cstddef>

namespace nba::core::arm {

struct ARM7TDMI;

namespace dynarec {

struct Dynarec {
  explicit Dynarec(ARM7TDMI& cpu) : cpu_(cpu) {}

  void Reset();
  void InvalidateAll();

  // Schedule a flush when a write overlaps a page that holds compiled code
  // (IWRAM / EWRAM self-modifying code). Applied immediately when not inside
  // TryRunBlock; deferred to the block boundary while a block is executing so
  // the live CompiledBlock / IR / native buffer is not wiped mid-op.
  void InvalidateRange(u32 address, u32 size);

  // Try to run one compiled block at the current PC. Returns true if a block
  // ran (caller should continue the outer loop). Returns false if the
  // interpreter should handle the next instruction.
  auto TryRunBlock() -> bool;

  auto CacheHits() const -> u64 { return cache_.hits(); }
  auto CacheMisses() const -> u64 { return cache_.misses(); }
  auto CacheSize() const -> int { return cache_.size(); }
  auto CodeBytesUsed() const -> std::size_t { return arena_.Used(); }
  auto InvalidationCount() const -> u32 { return invalidations_; }

  // Snapshot and reset interval counters for FPS overlay / logs.
  void TakeTelemetry(u64& hits, u64& misses, u32& invalidations);

private:
  static constexpr u32 kPageShift = 8; // 256-byte pages
  static constexpr u32 kPageSize = 1u << kPageShift;
  static constexpr u32 kIwramBytes = 0x8000;
  static constexpr u32 kEwramBytes = 0x40000;
  static constexpr u32 kIwramPages = kIwramBytes >> kPageShift; // 128
  static constexpr u32 kEwramPages = kEwramBytes >> kPageShift; // 1024

  auto LookupOrCompile(u32 pc, bool thumb) -> CompiledBlock*;
  auto ExecuteBlock(CompiledBlock& block) -> bool;

  void AdjustCompiledPages(CompiledBlock const& block, int delta);
  auto PageHasCode(u32 address, u32 size) const -> bool;
  void ClearCodePages();
  void ApplyPendingInvalidation();

  ARM7TDMI& cpu_;
  BlockCache cache_;
  CodeArena arena_;
  u32 invalidations_ = 0;
  int dispatch_depth_ = 0;
  bool pending_invalidate_ = false;
  // Reference counts so eviction can unmark pages without a full rebuild.
  std::array<u8, kIwramPages> iwram_code_pages_{};
  std::array<u8, kEwramPages> ewram_code_pages_{};
};

} // namespace dynarec
} // namespace nba::core::arm
