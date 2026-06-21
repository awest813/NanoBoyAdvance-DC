// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_config.hh"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <sys/stat.h>

namespace nba {

namespace fs = std::filesystem;

inline auto IsVMUSaveFolder(std::string const& path) -> bool {
  return path.rfind("/vmu/", 0) == 0;
}

inline auto BuildVMUSaveFilename(fs::path const& rom_path) -> std::string {
  // VMU filenames are short; use a stable hash of the display stem so saves
  // remain per-ROM without depending on long filenames or extensions.
  const auto stem = rom_path.stem().string();
  std::uint32_t hash = 2166136261u;
  for(unsigned char character : stem) {
    hash ^= character;
    hash *= 16777619u;
  }

  char filename[12];
  std::snprintf(filename, sizeof(filename), "NBA%08lX", static_cast<unsigned long>(hash));
  return filename;
}

inline auto EnsureDirectory(fs::path const& path) -> bool {
  if(path.empty()) {
    return false;
  }

  std::error_code error;
  if(fs::exists(path, error)) {
    return fs::is_directory(path, error);
  }

  return fs::create_directories(path, error);
}

// POSIX mkdir-based directory creator for Dreamcast virtual paths where
// std::filesystem may not work correctly through KOS filesystem drivers.
// Returns true if the directory exists or was created successfully.
// Creates intermediate parent directories as needed (e.g., /pc/saves/foo/bar).
inline auto EnsureDirectoryPOSIX(std::string const& path) -> bool {
  if(path.empty()) {
    return false;
  }

  // Walk forward from the root creating each ancestor as needed so that the
  // final ::mkdir does not fail with ENOENT when parent paths are missing.
  for(size_t pos = 1; pos < path.size(); pos++) {
    if(path[pos] == '/') {
      auto ancestor = path.substr(0, pos);
      ::mkdir(ancestor.c_str(), 0755);  // ignore EEXIST/ENOENT for partial paths
    }
  }

  if(::mkdir(path.c_str(), 0755) == 0) {
    return true; // created
  }
  // errno EEXIST means the directory already exists.
  return errno == EEXIST;
}

inline auto GetSaveStatePath(
  DreamcastConfig const& config,
  fs::path const& rom_path,
  int slot
) -> fs::path {
  const auto stem = rom_path.stem().string();
  const auto slot_suffix = std::to_string(std::clamp(slot, 0, DreamcastConfig::kSaveStateSlotCount - 1));

  if(!config.state_folder.empty()) {
    return fs::path{config.state_folder} / (stem + ".ss" + slot_suffix);
  }

  return fs::path{DreamcastConfig::kDefaultStateFolder} / (stem + ".ss" + slot_suffix);
}

inline auto GetSavePath(DreamcastConfig const& config, fs::path const& rom_path) -> fs::path {
  const auto stem = rom_path.stem().string();

  if(!config.save_folder.empty()) {
    if(IsVMUSaveFolder(config.save_folder)) {
      return fs::path{config.save_folder} / BuildVMUSaveFilename(rom_path);
    }

    return fs::path{config.save_folder} / (stem + ".sav");
  }

  const auto parent = rom_path.parent_path().string();
  if(parent.rfind("/cd", 0) == 0) {
    return fs::path{DreamcastConfig::kDefaultSaveFolder} / (stem + ".sav");
  }

  auto save_path = rom_path;
  return save_path.replace_extension(".sav");
}

} // namespace nba
