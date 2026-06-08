// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(PLATFORM_DREAMCAST)

#include <platform/loader/dc_idle_config.hh>
#include <platform/loader/dc_virtual_fs.hh>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace nba {

namespace {

struct GameConfigEntry {
  std::string game_name;
  std::string game_code;
  std::string vendor_code;
  u32 idle_loop_target = 0;
};

auto Trim(std::string value) -> std::string {
  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }

  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }

  return value;
}

auto ParseHexU32(std::string const& text) -> u32 {
  u32 value = 0;
  std::sscanf(text.c_str(), "%x", &value);
  return value;
}

auto ParseConfigContent(std::string const& content) -> std::vector<GameConfigEntry> {
  std::vector<GameConfigEntry> entries;
  GameConfigEntry current;

  auto commit = [&]() {
    if(!current.game_code.empty() && current.idle_loop_target != 0) {
      entries.push_back(current);
    }
    current = {};
  };

  std::istringstream stream{content};
  std::string line;
  while(std::getline(stream, line)) {
    line = Trim(line);
    if(line.empty() || line[0] == '#') {
      continue;
    }

    const auto equals = line.find('=');
    if(equals == std::string::npos) {
      continue;
    }

    auto key = Trim(line.substr(0, equals));
    auto value = Trim(line.substr(equals + 1));

    for(auto& character : key) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    if(key == "game_name") {
      if(!current.game_code.empty()) {
        commit();
      }
      current.game_name = value;
    } else if(key == "game_code") {
      current.game_code = value;
    } else if(key == "vender_code" || key == "vendor_code") {
      current.vendor_code = value;
    } else if(key == "idle_loop_eliminate_target") {
      current.idle_loop_target = ParseHexU32(value);
    }
  }

  commit();
  return entries;
}

auto ConfigSearchPaths() -> std::vector<std::string> {
  return {
    "/cd/gbaDC/game_config.txt",
    "/pc/game_config.txt",
    "/cd/game_config.txt"
  };
}

auto LoadConfigEntries() -> std::vector<GameConfigEntry> const& {
  static std::vector<GameConfigEntry> entries;
  static bool loaded = false;

  if(loaded) {
    return entries;
  }

  loaded = true;

  for(auto const& path : ConfigSearchPaths()) {
    std::string content;
    if(!ReadDreamcastTextFile(path, content)) {
      continue;
    }

    entries = ParseConfigContent(content);
    std::printf(
      "[NBA-DC] Idle config: loaded %lu entries from %s\n",
      static_cast<unsigned long>(entries.size()),
      path.c_str()
    );
    std::fflush(stdout);
    break;
  }

  return entries;
}

auto HeaderField(std::string_view field, size_t length) -> std::string {
  std::string value{field.substr(0, length)};
  while(!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  return value;
}

} // namespace

auto DreamcastIdleConfig::LookupIdleLoopTarget(Header const& header) -> u32 {
  const auto code = HeaderField(header.game.code, sizeof(header.game.code));
  const auto maker = HeaderField(header.game.maker, sizeof(header.game.maker));

  for(auto const& entry : LoadConfigEntries()) {
    if(entry.game_code != code) {
      continue;
    }

    if(!entry.vendor_code.empty() && entry.vendor_code != maker) {
      continue;
    }

    return entry.idle_loop_target;
  }

  return 0;
}

auto DreamcastIdleConfig::ApplyTo(Config& config, Header const& header) -> void {
  config.idle_loop_eliminate_target = LookupIdleLoopTarget(header);
  if(config.idle_loop_eliminate_target != 0) {
    std::printf(
      "[NBA-DC] Idle loop target for %s: 0x%08X\n",
      HeaderField(header.game.code, sizeof(header.game.code)).c_str(),
      config.idle_loop_eliminate_target
    );
    std::fflush(stdout);
  }
}

} // namespace nba

#endif
