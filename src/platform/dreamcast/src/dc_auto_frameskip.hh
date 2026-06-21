// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_config.hh"

#include <string>

namespace nba {

// Auto frame-skip thresholds — named constants so the policy is auditable
// in one place.  See UpdateAutoFrameSkip for the full policy description.

// When EF (emulated FPS) exceeds this, reduce frame skip even when display
// FPS is below the raise threshold, preventing runaway catch-up.
static constexpr float kAutoSkipEFUpperCap = 62.0f;

// Speed profile thresholds (ppu_fast_mode, auto frame skip on by default):
// raise frame skip when display FPS drops below this; lower when above this.
static constexpr float kAutoSkipSpeedRaiseFPS = 56.0f;
static constexpr float kAutoSkipSpeedLowerFPS = 58.5f;

// Balanced / Accuracy profile thresholds (ppu_fast_mode = false).
static constexpr float kAutoSkipRaiseFPS = 55.0f;
static constexpr float kAutoSkipLowerFPS = 57.5f;

// Speed profile: EMU ms headroom hint.  If the emulated frames per display
// frame finish in less than this many milliseconds, reduce frame skip even
// when display FPS is below target — the core has headroom.
static constexpr float kAutoSkipEmuMsHeadroom = 14.5f;

// Auto frame-skip policy.
//
// Uses display FPS, the emulated-FPS code (EF = FPS × (skip + 1)), and the
// per-display-frame emulation microsecond measurement from DCFrameTiming to
// decide whether to raise or lower frame_skip.  Speed profile uses tighter
// thresholds and an EMU-ms headroom hint.  Returns the new active frame skip
// (0 … 3) and writes it back to config.frame_skip.
auto UpdateAutoFrameSkip(
  DreamcastConfig& config,
  float measured_fps,
  int current_frame_skip,
  int& recovery_ticks,
  double emu_ms_per_display_frame = 0.0
) -> int;

// Collapses newlines to spaces for the gameplay overlay (single-line draw).
auto FormatGameplayOverlay(std::string const& message) -> std::string;

// Reads a positive integer from an environment variable.  Returns 0 when the
// variable is unset, empty, or contains a non-positive value.
auto ParsePositiveEnvInt(char const* name) -> int;

} // namespace nba
