// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/block_cache.hh"
#include "arm/dynarec/ir.hh"

namespace nba::core::arm {

struct ARM7TDMI;

namespace dynarec {

struct Dynarec {
  explicit Dynarec(ARM7TDMI& cpu) : cpu_(cpu) {}

  void Reset() { cache_.Clear(); }
  void InvalidateAll() { cache_.Clear(); }

  // Try to run one compiled block at the current PC. Returns true if a block
  // ran (caller should continue the outer loop). Returns false if the
  // interpreter should handle the next instruction.
  auto TryRunBlock() -> bool;

  auto CacheHits() const -> u64 { return cache_.hits(); }
  auto CacheMisses() const -> u64 { return cache_.misses(); }
  auto CacheSize() const -> int { return cache_.size(); }

private:
  ARM7TDMI& cpu_;
  BlockCache cache_;
};

} // namespace dynarec
} // namespace nba::core::arm
