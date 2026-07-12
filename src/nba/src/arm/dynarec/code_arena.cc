// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/code_arena.hh"

#if defined(__SH4__)
#if __has_include(<arch/cache.h>)
#include <arch/cache.h>
#elif __has_include(<kos/cache.h>)
#include <kos/cache.h>
#endif
#endif

namespace nba::core::arm::dynarec {

void CodeArena::FlushExecutable(u8 const* ptr, std::size_t size) {
  if(ptr == nullptr || size == 0) {
    return;
  }

#if defined(__SH4__)
#if defined(__arch_cache_h) || defined(__kos_cache_h)
  icache_flush_range(reinterpret_cast<uintptr_t>(ptr), size);
#elif __has_include(<arch/cache.h>)
  icache_flush_range(reinterpret_cast<uintptr_t>(ptr), size);
#endif
#endif
  (void)ptr;
  (void)size;
}

} // namespace nba::core::arm::dynarec
