// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/ir.hh"

#include <array>
#include <cstdint>

namespace nba::core::arm::dynarec {

// Fixed-capacity open-addressed cache. Dreamcast RAM is tight; keep this small
// and predictable (no heap growth during gameplay).
inline constexpr int kBlockCacheCapacity = 2048;

struct BlockCache {
  BlockCache() { Clear(); }

  void Clear() {
    for(auto& slot : slots_) {
      slot.key = kEmpty;
      slot.block = {};
    }
    size_ = 0;
    // Keep hit/miss counters across Clear so InvalidateAll does not distort
    // per-second DR telemetry; TakeStats resets them explicitly.
  }

  auto TakeStats(u64& hits, u64& misses) -> void {
    hits = hits_;
    misses = misses_;
    hits_ = 0;
    misses_ = 0;
  }

  auto Find(u32 key) -> CompiledBlock* {
    const u32 start = Hash(key);
    for(u32 i = 0; i < kBlockCacheCapacity; ++i) {
      auto& slot = slots_[(start + i) & (kBlockCacheCapacity - 1)];
      if(slot.key == kEmpty) {
        ++misses_;
        return nullptr;
      }
      if(slot.key == key) {
        ++hits_;
        return &slot.block;
      }
    }
    ++misses_;
    return nullptr;
  }

  // Probe without bumping hit/miss counters (soft block linking).
  auto Contains(u32 key) const -> bool {
    const u32 start = Hash(key);
    for(u32 i = 0; i < kBlockCacheCapacity; ++i) {
      auto const& slot = slots_[(start + i) & (kBlockCacheCapacity - 1)];
      if(slot.key == kEmpty) {
        return false;
      }
      if(slot.key == key) {
        return true;
      }
    }
    return false;
  }

  // Insert or replace. When a different key is overwritten (full-table eviction
  // or same-slot replace), copies the previous block into *evicted when non-null
  // and returns true via *did_evict.
  auto Insert(
    u32 key,
    CompiledBlock const& block,
    CompiledBlock* evicted = nullptr,
    bool* did_evict = nullptr
  ) -> CompiledBlock* {
    if(did_evict) {
      *did_evict = false;
    }
    const u32 start = Hash(key);
    for(u32 i = 0; i < kBlockCacheCapacity; ++i) {
      auto& slot = slots_[(start + i) & (kBlockCacheCapacity - 1)];
      if(slot.key == kEmpty || slot.key == key) {
        if(slot.key == key && evicted != nullptr) {
          *evicted = slot.block;
          if(did_evict) {
            *did_evict = true;
          }
        }
        if(slot.key == kEmpty) {
          ++size_;
        }
        slot.key = key;
        slot.block = block;
        return &slot.block;
      }
    }
    auto& slot = slots_[start];
    if(evicted != nullptr) {
      *evicted = slot.block;
    }
    if(did_evict) {
      *did_evict = true;
    }
    slot.key = key;
    slot.block = block;
    return &slot.block;
  }

  auto size() const -> int { return size_; }
  auto hits() const -> u64 { return hits_; }
  auto misses() const -> u64 { return misses_; }

private:
  static constexpr u32 kEmpty = 0xFFFFFFFFu;

  static auto Hash(u32 key) -> u32 {
    // multiplicative hash; capacity is power-of-two
    return (key * 2654435761u) & (kBlockCacheCapacity - 1);
  }

  struct Slot {
    u32 key = kEmpty;
    CompiledBlock block{};
  };

  std::array<Slot, kBlockCacheCapacity> slots_{};
  int size_ = 0;
  u64 hits_ = 0;
  u64 misses_ = 0;
};

} // namespace nba::core::arm::dynarec
