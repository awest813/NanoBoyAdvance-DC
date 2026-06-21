// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(PLATFORM_DREAMCAST) && __has_include(<kos.h>)
#define NBA_DC_HAS_KOS 1
#else
#define NBA_DC_HAS_KOS 0
#endif

#include "dc_version.hh"
#include "dc_memory.hh"
#include "version.hh"

namespace nba {

auto BuildDreamcastBootInfo() -> std::string {
  std::string line = "v";
  line += VERSION_STRING;

  if(VERSION_GIT_HASH[0] != '\0') {
    line += " (";
    line += VERSION_GIT_HASH;
    line += ')';
  }

#if NBA_DC_HAS_KOS
  line += "  |  ";
  line += HasExtendedRAM() ? "32 MB RAM" : "16 MB RAM";
#endif

  return line;
}

} // namespace nba
