// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/code_arena.hh"

#if defined(__SH4__)
#if __has_include(<arch/cache.h>)
#include <arch/cache.h>
#define NBA_DR_HAS_ICACHE_FLUSH 1
#elif __has_include(<kos/cache.h>)
#include <kos/cache.h>
#define NBA_DR_HAS_ICACHE_FLUSH 1
#else
#warning "SH4 dynarec: no icache_flush_range header; native blocks may execute stale I-cache lines"
#define NBA_DR_HAS_ICACHE_FLUSH 0
#endif
#else
#define NBA_DR_HAS_ICACHE_FLUSH 0
#endif

namespace nba::core::arm::dynarec {

void CodeArena::FlushExecutable(u8 const* ptr, std::size_t size) {
  if(ptr == nullptr || size == 0) {
    return;
  }

#if NBA_DR_HAS_ICACHE_FLUSH
  icache_flush_range(reinterpret_cast<uintptr_t>(ptr), size);
#else
  (void)ptr;
  (void)size;
#endif
}

} // namespace nba::core::arm::dynarec
