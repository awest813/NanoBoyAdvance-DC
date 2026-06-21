// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(PLATFORM_DREAMCAST) && __has_include(<kos.h>)
#define NBA_DC_HAS_KOS 1
#include <kos.h>
#else
#define NBA_DC_HAS_KOS 0
#endif

#include "dc_autoboot.hh"
#include "dc_log.hh"

#include "dc_memory.hh"
#include <platform/loader/rom.hh>

#include <cstdio>

#if NBA_DC_HAS_KOS
#include <dc/video.h>
#endif

namespace nba {

auto GetROMSourceName(fs::path const& path) -> const char* {
  const auto path_string = path.string();
  if(path_string.rfind("/cd/", 0) == 0) {
    return "CD root";
  }

  if(path_string.rfind("/pc/roms/", 0) == 0) {
    return "PC ROMs";
  }

  if(path_string.rfind("/pc/", 0) == 0) {
    return "PC";
  }

  return "Unknown";
}

auto FormatROMSize(size_t size) -> std::string {
  char message[48];
  std::snprintf(
    message,
    sizeof(message),
    "%lu bytes (%lu MiB)",
    static_cast<unsigned long>(size),
    static_cast<unsigned long>(size / (1024 * 1024))
  );
  return message;
}

auto ResolveAutobootROMPath(DreamcastConfig const& config, std::string& report) -> fs::path {
  report.clear();

  auto probe = [&](fs::path const& candidate, char const* label) -> bool {
    const auto result = ROMLoader::Validate(candidate);
    char line[192];
    std::snprintf(
      line,
      sizeof(line),
      "%s: %s -> %s\n",
      label,
      candidate.string().c_str(),
      ROMLoader::Describe(result)
    );
    report += line;
    DCLog("[NBA-DC] Autoboot probe %s", line);
    return result == ROMLoader::Result::Success;
  };

  fs::path lower{kDreamcastAutobootROM};
  if(probe(lower, "lower")) {
    return lower;
  }

  fs::path mixed{kDreamcastAutobootROMFallback};
  if(probe(mixed, "mixed")) {
    return mixed;
  }

  fs::path upper{"/cd/TEKKEN.GBA"};
  if(probe(upper, "upper")) {
    return upper;
  }

  fs::path upper_version{"/cd/TEKKEN.GBA;1"};
  if(probe(upper_version, "upper;1")) {
    return upper_version;
  }

  auto entries = ROMBrowser::Scan(config);
  char count_line[80];
  std::snprintf(
    count_line,
    sizeof(count_line),
    "scan entries: %lu\n",
    static_cast<unsigned long>(entries.size())
  );
  report += count_line;
  DCLog("[NBA-DC] Autoboot %s", count_line);

  for(auto const& entry : entries) {
    char line[256];
    std::snprintf(
      line,
      sizeof(line),
      "scan: %s | %s | %lu bytes\n",
      entry.path.string().c_str(),
      entry.label.c_str(),
      static_cast<unsigned long>(entry.size)
    );
    report += line;
    DCLog("[NBA-DC] Autoboot %s", line);

    if(entry.path.string().rfind("/cd/", 0) == 0) {
      return entry.path;
    }
  }

  return lower;
}

void HoldDebugBreadcrumbFrames(int frames) {
#if NBA_DC_HAS_KOS
  for(int i = 0; i < frames; i++) {
    vid_waitvbl();
  }
#else
  (void)frames;
#endif
}

} // namespace nba
