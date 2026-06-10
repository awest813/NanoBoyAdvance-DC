// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/config.hh>
#include <nba/rom/header.hh>

namespace nba {

#if defined(PLATFORM_DREAMCAST)

// gpSP-compatible game_config.txt idle-loop hints (data only, MIT implementation).
struct DreamcastIdleConfig {
  static auto LookupIdleLoopTarget(Header const& header) -> u32;
  static auto ApplyTo(Config& config, Header const& header) -> void;
};

#endif

} // namespace nba
