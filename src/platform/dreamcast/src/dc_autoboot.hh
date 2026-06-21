// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_config.hh"
#include "dc_rom_browser.hh"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace nba {

// Autoboot Tekken constants.  Only used when kDreamcastAutobootTekken is
// enabled for CDI-only stability testing.
static constexpr bool kDreamcastAutobootTekken = false;
static constexpr char kDreamcastAutobootROM[] = "/cd/tekken.gba";
static constexpr char kDreamcastAutobootROMFallback[] = "/cd/Tekken.gba";

// Classifies a ROM path into a human-readable source label.
auto GetROMSourceName(fs::path const& path) -> const char*;

// Formats a size in bytes to a short string (e.g. "16 MiB" / "512 KiB").
auto FormatROMSize(size_t size) -> std::string;

// Probes /cd/ for a Tekken ROM, falling back to a full ROM browser scan.
// The report string receives per-probe success/failure lines for display.
auto ResolveAutobootROMPath(DreamcastConfig const& config, std::string& report) -> fs::path;

// Busy-waits N VBLANK periods on KOS, or no-ops on host.
void HoldDebugBreadcrumbFrames(int frames);

} // namespace nba
