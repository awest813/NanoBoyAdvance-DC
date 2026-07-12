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

  // Insert or replace. On a full table, evict the probed slot (simple).
  auto Insert(u32 key, CompiledBlock const& block) -> CompiledBlock* {
    const u32 start = Hash(key);
    for(u32 i = 0; i < kBlockCacheCapacity; ++i) {
      auto& slot = slots_[(start + i) & (kBlockCacheCapacity - 1)];
      if(slot.key == kEmpty || slot.key == key) {
        if(slot.key == kEmpty) {
          ++size_;
        }
        slot.key = key;
        slot.block = block;
        return &slot.block;
      }
    }
    auto& slot = slots_[start];
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
