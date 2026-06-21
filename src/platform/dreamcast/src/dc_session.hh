// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_config.hh"
#include "dc_ui.hh"
#include "device/dc_video_device.hh"
#include "device/dc_input.hh"

#include <filesystem>
#include <memory>

namespace nba {

namespace fs = std::filesystem;

// Runs a full emulation session: BIOS check, ROM load, core init, gameplay
// loop with pause menu, save-state hotkeys, auto frame-skip, FPS overlay,
// periodic save flushes, and post-session save-persistence UX.
//
// Returns true if the session ended normally (user exit or max_frames
// smoke-test completion).  Returns false on fatal errors before gameplay.
auto RunGameSession(
  DCUI& ui,
  DCInput& input,
  std::shared_ptr<DreamcastConfig>& config,
  std::shared_ptr<DCVideoDevice>& video_device,
  fs::path const& rom_path
) -> bool;

} // namespace nba
