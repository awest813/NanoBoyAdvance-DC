// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_config.hh"

#include <platform/loader/dc_virtual_fs.hh>

namespace nba {

static constexpr size_t kStockDreamcastMaxROMSize = 8 * 1024 * 1024;

inline auto CanLoadLargeROM(DreamcastConfig const& config) -> bool {
  return config.allow_large_roms || HasExtendedRAM();
}

} // namespace nba
