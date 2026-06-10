// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_rom_browser.hh"

#include "dc_memory.hh"
#include <platform/loader/dc_virtual_fs.hh>
#include <platform/loader/rom.hh>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <set>

namespace nba {

static auto NormalizeExtension(std::string extension) -> std::string {
  const auto version = extension.find(';');
  if(version != std::string::npos) {
    extension.resize(version);
  }

  for(auto& character : extension) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }

  return extension;
}

static auto HasLoadableROMExtension(std::string const& path) -> bool {
  const auto dot = path.find_last_of('.');
  if(dot == std::string::npos) {
    return false;
  }

  const auto extension = NormalizeExtension(path.substr(dot));
  return extension == ".gba" || extension == ".bin" || extension == ".zip";
}

static auto FormatROMSizeLabel(size_t size) -> std::string {
  if(size >= 1024 * 1024) {
    char buffer[32];
    std::snprintf(
      buffer,
      sizeof(buffer),
      "%lu MiB",
      static_cast<unsigned long>(size / (1024 * 1024))
    );
    return buffer;
  }

  char buffer[32];
  std::snprintf(
    buffer,
    sizeof(buffer),
    "%lu KiB",
    static_cast<unsigned long>((size + 1023) / 1024)
  );
  return buffer;
}

static auto NeedsLargeROMs(size_t size, DreamcastConfig const& config) -> bool {
  return size > kStockDreamcastMaxROMSize && !CanLoadLargeROM(config);
}

static auto BuildROMLabel(
  std::string base_label,
  size_t size,
  DreamcastConfig const& config
) -> std::string {
  auto label = std::move(base_label);
  label += " (";
  label += FormatROMSizeLabel(size);
  label += ')';

  if(NeedsLargeROMs(size, config)) {
    // Marked unavailable on the current RAM configuration; the browser refuses
    // to launch these and explains why when selected.
    label += " [Needs Large ROMs]";
  }

  return label;
}

static auto IsROMEntryLaunchable(size_t size, DreamcastConfig const& config) -> bool {
  if(size == 0) {
    return true;
  }

  return size <= kStockDreamcastMaxROMSize || CanLoadLargeROM(config);
}

static auto AddROMEntry(
  fs::path const& path,
  std::set<fs::path>& seen,
  std::vector<ROMEntry>& entries,
  DreamcastConfig const& config,
  bool validate = true,
  std::string label_override = {}
) -> bool {
#if defined(PLATFORM_DREAMCAST)
  const fs::path entry_path{ResolveDreamcastVirtualPath(path)};
#else
  const fs::path entry_path = path;
#endif

  if(seen.find(entry_path) != seen.end()) {
    return false;
  }

  if(validate && ROMLoader::Validate(entry_path) != ROMLoader::Result::Success) {
    return false;
  }

  size_t size = 0;
  const bool have_size = ROMLoader::GetFileSize(entry_path, size) == ROMLoader::Result::Success;

  // Strip the ISO9660 version suffix (e.g. ";1") from the display label so
  // disc filenames like "GAME.GBA;1" appear as "GAME.GBA" in the menu.
  auto label = label_override.empty() ? entry_path.filename().string() : std::move(label_override);
  const auto semicolon = label.rfind(';');
  if(semicolon != std::string::npos) {
    label.resize(semicolon);
  }

  const bool launchable = IsROMEntryLaunchable(have_size ? size : 0, config);

  seen.insert(entry_path);
  if(have_size) {
    entries.push_back(ROMEntry{
      entry_path,
      BuildROMLabel(std::move(label), size, config),
      size,
      launchable
    });
  } else {
    entries.push_back(ROMEntry{
      entry_path,
      std::move(label) + " (size unknown)",
      0,
      launchable
    });
  }
  return true;
}

static auto AddDirectoryEntries(
  fs::path const& directory,
  std::set<fs::path>& seen,
  std::vector<ROMEntry>& entries,
  DreamcastConfig const& config
) -> void {
#if defined(PLATFORM_DREAMCAST)
  const auto directory_string = directory.string();

  // Use POSIX opendir/readdir for all Dreamcast paths.  On real KallistiOS
  // hardware this enumerates both /pc and /cd correctly.  ISO9660 discs may
  // expose filenames with version suffixes (e.g. "GAME.GBA;1"); strip the
  // suffix from the path so that fopen always receives a clean name.
  if(auto* dir = opendir(directory_string.c_str())) {
    while(auto* entry = readdir(dir)) {
      if(std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      auto raw_name = std::string{entry->d_name};
      auto clean_name = raw_name;

      // Strip ISO9660 version suffix for extension checks and display labels.
      const auto semicolon_pos = clean_name.rfind(';');
      if(semicolon_pos != std::string::npos) {
        clean_name.resize(semicolon_pos);
      }

      if(!HasLoadableROMExtension(clean_name)) {
        continue;
      }

      // KOS/Flycast ISO9660 drivers differ on whether fopen wants "GAME.GBA"
      // or the raw "GAME.GBA;1" directory entry. Try the clean path first for
      // stable save names, then fall back to the raw path if needed.
      if(!AddROMEntry(directory / clean_name, seen, entries, config, true, clean_name) &&
          raw_name != clean_name) {
        AddROMEntry(directory / raw_name, seen, entries, config, true, clean_name);
      }
    }

    closedir(dir);
    return;
  }
  // opendir returned null; directory is inaccessible or does not exist.
  return;
#endif

  if(directory.empty() || !fs::exists(directory) || !fs::is_directory(directory)) {
    return;
  }

  std::error_code error;
  for(auto const& entry : fs::directory_iterator(directory, error)) {
    if(!entry.is_regular_file()) {
      continue;
    }

    const auto path = entry.path();
    if(!HasLoadableROMExtension(path.filename().string())) {
      continue;
    }

    AddROMEntry(path, seen, entries, config);
  }
}

auto ROMBrowser::Scan(DreamcastConfig const& config) -> std::vector<ROMEntry> {
  std::set<fs::path> seen;
  std::vector<ROMEntry> entries;

  AddDirectoryEntries(config.rom_folder, seen, entries, config);
  AddDirectoryEntries("/cd", seen, entries, config);
  // gpSPDC convention: ROMs and game_config.txt live under /cd/gbaDC on disc.
  AddDirectoryEntries("/cd/gbaDC", seen, entries, config);

  if(!config.last_rom.empty()) {
    const fs::path last_path{config.last_rom};

    // For Dreamcast virtual paths (/cd, /pc) std::filesystem::exists may
    // behave incorrectly because those paths are backed by KOS filesystem
    // drivers rather than the host VFS.  Use fopen as a portable existence
    // probe instead.
    bool last_rom_exists = false;
#if defined(PLATFORM_DREAMCAST)
    {
      if(auto* file = OpenDreamcastVirtualFile(last_path)) {
        std::fclose(file);
        last_rom_exists = true;
      } else {
        last_rom_exists = fs::exists(last_path);
      }
    }
#else
    last_rom_exists = fs::exists(last_path);
#endif

    if(last_rom_exists &&
        ROMLoader::Validate(last_path) == ROMLoader::Result::Success) {
#if defined(PLATFORM_DREAMCAST)
      const fs::path entry_path{ResolveDreamcastVirtualPath(last_path)};
#else
      const fs::path entry_path = last_path;
#endif

      if(seen.insert(entry_path).second) {
        size_t size = 0;
        auto last_label = entry_path.filename().string();
        const auto sc = last_label.rfind(';');
        if(sc != std::string::npos) {
          last_label.resize(sc);
        }
        if(ROMLoader::GetFileSize(entry_path, size) == ROMLoader::Result::Success) {
          entries.push_back(ROMEntry{
            entry_path,
            BuildROMLabel(std::move(last_label), size, config) + " (last)",
            size,
            IsROMEntryLaunchable(size, config)
          });
        } else {
          entries.push_back(ROMEntry{
            entry_path,
            std::move(last_label) + " (size unknown) (last)",
            0,
            true
          });
        }
      }
    }
  }

  std::sort(entries.begin(), entries.end(), [](ROMEntry const& a, ROMEntry const& b) {
    return a.label < b.label;
  });

  return entries;
}

} // namespace nba
