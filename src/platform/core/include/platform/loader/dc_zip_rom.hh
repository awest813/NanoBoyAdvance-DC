// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace fs = std::filesystem;

namespace nba {

#if defined(PLATFORM_DREAMCAST)

auto IsDreamcastZipROM(fs::path const& path) -> bool;

// Returns uncompressed size of the first .gba/.bin entry without buffering the
// whole archive in RAM (uses miniz streaming from the host file).
auto GetDreamcastZipROMSize(fs::path const& zip_path, size_t& rom_size) -> bool;

// Extracts the inner ROM to /pc/roms/.cache/<stem>.gba when needed and returns
// the cached ROM path suitable for paged loading.
auto ResolveDreamcastZipROM(fs::path const& zip_path, fs::path& rom_path) -> bool;

#endif

} // namespace nba
