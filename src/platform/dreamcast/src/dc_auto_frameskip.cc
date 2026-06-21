// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_auto_frameskip.hh"
#include "dc_log.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace nba {

auto UpdateAutoFrameSkip(
  DreamcastConfig& config,
  float measured_fps,
  int current_frame_skip,
  int& recovery_ticks,
  double emu_ms_per_display_frame
) -> int {
  if(!config.auto_frame_skip || measured_fps <= 0.0f) {
    recovery_ticks = 0;
    return config.frame_skip;
  }

  const bool speed_profile =
    config.performance_profile == DreamcastConfig::PerformanceProfile::Speed;
  const float raise_threshold = speed_profile ? kAutoSkipSpeedRaiseFPS : kAutoSkipRaiseFPS;
  const float lower_threshold = speed_profile ? kAutoSkipSpeedLowerFPS : kAutoSkipLowerFPS;
  const int recovery_required = speed_profile ? 2 : 3;
  const int emulated_fps = static_cast<int>(
    measured_fps * static_cast<float>(current_frame_skip + 1) + 0.5f
  );

  int next_frame_skip = current_frame_skip;
  if(static_cast<float>(emulated_fps) > kAutoSkipEFUpperCap && current_frame_skip > 0) {
    next_frame_skip--;
    recovery_ticks = 0;
  } else if(
    speed_profile &&
    emu_ms_per_display_frame > 0.0 &&
    emu_ms_per_display_frame < kAutoSkipEmuMsHeadroom &&
    current_frame_skip > 0
  ) {
    next_frame_skip--;
    recovery_ticks = 0;
  } else if(measured_fps < raise_threshold && current_frame_skip < 3) {
    next_frame_skip++;
    recovery_ticks = 0;
  } else if(measured_fps > lower_threshold && current_frame_skip > 0) {
    recovery_ticks++;
    if(recovery_ticks >= recovery_required) {
      next_frame_skip--;
      recovery_ticks = 0;
    }
  } else {
    recovery_ticks = 0;
  }

  if(next_frame_skip != current_frame_skip) {
    DCLog(
      "[NBA-DC] Auto frame skip: %d -> %d (FPS %.1f)\n",
      current_frame_skip,
      next_frame_skip,
      static_cast<double>(measured_fps)
    );
  }

  config.frame_skip = std::clamp(next_frame_skip, 0, 3);
  return config.frame_skip;
}

auto FormatGameplayOverlay(std::string const& message) -> std::string {
  std::string line = message;
  for(auto& character : line) {
    if(character == '\n') {
      character = ' ';
    }
  }
  return line;
}

auto ParsePositiveEnvInt(char const* name) -> int {
  if(const char* value = std::getenv(name)) {
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if(end != value && parsed > 0) {
      return static_cast<int>(parsed);
    }
  }
  return 0;
}

} // namespace nba
