// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_config.hh"

#include <filesystem>
#include <string>
#include <vector>

namespace nba {

namespace fs = std::filesystem;

struct ROMEntry {
  fs::path path;
  std::string label;
  size_t size = 0;
  bool launchable = true;
};

struct ROMBrowser {
  static auto Scan(DreamcastConfig const& config) -> std::vector<ROMEntry>;
};

} // namespace nba
