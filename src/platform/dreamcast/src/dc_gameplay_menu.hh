// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_cheats.hh"
#include "dc_config.hh"
#include "dc_ui.hh"
#include "device/dc_input.hh"

#include <filesystem>
#include <memory>
#include <string>

namespace nba {

namespace fs = std::filesystem;

struct DCGameplayMenu {
  enum class Action {
    Resume,
    ExitToBrowser
  };

  static auto SaveState(
    std::unique_ptr<CoreBase>& core,
    DreamcastConfig& config,
    fs::path const& rom_path
  ) -> std::string;

  static auto LoadState(
    std::unique_ptr<CoreBase>& core,
    DreamcastConfig& config,
    fs::path const& rom_path
  ) -> std::string;

  static auto Run(
    DCUI& ui,
    DCInput& input,
    DreamcastConfig& config,
    std::unique_ptr<CoreBase>& core,
    DCCheatDatabase& cheats,
    fs::path const& rom_path
  ) -> Action;
};

} // namespace nba
