// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/integer.hh>
#include <cstddef>
#include <cstdint>

namespace nba::core::arm::dynarec {

// Bump allocator for SH4 dynarec machine code. Reset on cache invalidation.
class CodeArena {
public:
  static constexpr std::size_t kCapacity = 512 * 1024;

  void Reset() { offset_ = 0; }

  auto Allocate(std::size_t size, std::size_t alignment = 4) -> u8* {
    const std::size_t aligned = (offset_ + alignment - 1) & ~(alignment - 1);
    if(aligned + size > kCapacity) {
      return nullptr;
    }
    offset_ = aligned + size;
    return &buffer_[aligned];
  }

  auto Used() const -> std::size_t { return offset_; }
  auto Capacity() const -> std::size_t { return kCapacity; }

  // After writing executable code, flush the SH4 instruction cache.
  static void FlushExecutable(u8 const* ptr, std::size_t size);

private:
  alignas(4) u8 buffer_[kCapacity]{};
  std::size_t offset_ = 0;
};

} // namespace nba::core::arm::dynarec
