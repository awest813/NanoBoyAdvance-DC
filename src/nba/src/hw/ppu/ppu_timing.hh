// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <memory>

#include <nba/config.hh>

namespace nba::core {

inline void AddPpuTiming(
  const std::shared_ptr<Config>& config,
  std::chrono::microseconds duration
) {
#if defined(PLATFORM_DREAMCAST)
  if(config && config->dc_ppu_timing_callback) {
    config->dc_ppu_timing_callback(duration.count());
  }
#else
  (void)config;
  (void)duration;
#endif
}

} // namespace nba::core
