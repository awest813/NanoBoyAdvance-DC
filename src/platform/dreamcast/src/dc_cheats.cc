// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_cheats.hh"

#include <platform/loader/dc_virtual_fs.hh>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace nba {

namespace {

auto TrimInPlace(std::string& text) -> void {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }

  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
}

auto ParseVariant(std::string const& header) -> DCCheatDatabase::Entry::Variant {
  std::string variant = header;
  for(auto& character : variant) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }

  if(variant == "gameshark_v1" || variant == "gameshark_v2" ||
     variant == "par_v1" || variant == "par_v2") {
    return DCCheatDatabase::Entry::Variant::GameSharkV1;
  }

  if(variant == "gameshark_v3" || variant == "par_v3") {
    return DCCheatDatabase::Entry::Variant::GameSharkV3;
  }

  return DCCheatDatabase::Entry::Variant::GameSharkV3;
}

auto OpenCheatStream(fs::path const& path) -> std::unique_ptr<std::FILE, decltype(&std::fclose)> {
#if defined(PLATFORM_DREAMCAST)
  if(IsDreamcastVirtualPath(path)) {
    if(auto* file = OpenDreamcastVirtualFile(path)) {
      return {file, &std::fclose};
    }
    return {nullptr, &std::fclose};
  }
#endif

  if(auto* file = std::fopen(path.string().c_str(), "rb")) {
    return {file, &std::fclose};
  }

  return {nullptr, &std::fclose};
}

auto ReadLine(std::FILE* file, std::string& line) -> bool {
  line.clear();
  char buffer[256];

  while(std::fgets(buffer, sizeof(buffer), file)) {
    line += buffer;
    if(!line.empty() && line.back() == '\n') {
      break;
    }
  }

  if(line.empty()) {
    return false;
  }

  while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }

  return true;
}

} // namespace

auto FindCheatFile(fs::path const& rom_path) -> fs::path {
  const auto stem = rom_path.stem().string();
  const auto filename = stem + ".cht";

  std::vector<fs::path> candidates;
  candidates.push_back(rom_path.parent_path() / filename);
  candidates.emplace_back("/cd/gbaDC" / fs::path{filename});
  candidates.emplace_back("/pc/cheats" / fs::path{filename});

  for(auto const& candidate : candidates) {
#if defined(PLATFORM_DREAMCAST)
    if(IsDreamcastVirtualPath(candidate)) {
      if(auto* file = OpenDreamcastVirtualFile(candidate)) {
        std::fclose(file);
        return candidate;
      }
      continue;
    }
#endif

    if(std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  return {};
}

auto DCCheatDatabase::GetEntry(size_t index) const -> Entry const* {
  if(index >= entries_.size()) {
    return nullptr;
  }

  return &entries_[index];
}

auto DCCheatDatabase::SetEnabled(size_t index, bool enabled) -> void {
  if(index < entries_.size()) {
    entries_[index].enabled = enabled;
  }
}

auto DCCheatDatabase::Toggle(size_t index) -> void {
  if(index < entries_.size()) {
    entries_[index].enabled = !entries_[index].enabled;
  }
}

auto DCCheatDatabase::DecryptCode(
  u32& address,
  u32& value,
  Entry::Variant variant
) -> void {
  u32 r = 0xc6ef3720;

  static constexpr u32 kSeedsV1[4] = {
    0x09f4fbbd, 0x9681884a, 0x352027e9, 0xf3dee5a7
  };
  static constexpr u32 kSeedsV3[4] = {
    0x7aa9648f, 0x7fae6994, 0xc0efaad5, 0x42712c57
  };

  auto const* seeds = variant == Entry::Variant::GameSharkV1 ? kSeedsV1 : kSeedsV3;

  for(int i = 0; i < 32; i++) {
    value -= ((address << 4) + seeds[2]) ^ (address + r) ^ ((address >> 5) + seeds[3]);
    address -= ((value << 4) + seeds[0]) ^ (value + r) ^ ((value >> 5) + seeds[1]);
    r -= 0x9e3779b9;
  }
}

auto DCCheatDatabase::LoadForROM(fs::path const& rom_path) -> bool {
  entries_.clear();

  const auto cheat_path = FindCheatFile(rom_path);
  if(cheat_path.empty()) {
    return false;
  }

  auto file = OpenCheatStream(cheat_path);
  if(!file) {
    return false;
  }

  std::string line;
  while(ReadLine(file.get(), line)) {
    if(line.empty()) {
      continue;
    }

    auto header_end = line.find(' ');
    if(header_end == std::string::npos) {
      continue;
    }

    auto header = line.substr(0, header_end);
    auto name = line.substr(header_end + 1);
    TrimInPlace(name);

    if(name.empty() || entries_.size() >= kMaxCheats) {
      continue;
    }

    Entry entry;
    entry.name = name.substr(0, kNameLength);
    entry.variant = ParseVariant(header);

    while(ReadLine(file.get(), line)) {
      if(line.size() < 3) {
        break;
      }

      u32 address = 0;
      u32 value = 0;
      if(std::sscanf(line.c_str(), "%08x %08x", &address, &value) != 2) {
        break;
      }

      DecryptCode(address, value, entry.variant);
      entry.codes.push_back(address);
      entry.codes.push_back(value);

      if(entry.codes.size() >= kMaxLinesPerCheat * 2) {
        break;
      }
    }

    if(!entry.codes.empty()) {
      entries_.push_back(std::move(entry));
    }
  }

  std::printf(
    "[NBA-DC] Cheats: loaded %lu entries from %s\n",
    static_cast<unsigned long>(entries_.size()),
    cheat_path.string().c_str()
  );
  std::fflush(stdout);

  return !entries_.empty();
}

auto DCCheatDatabase::ProcessGameSharkV1(Entry const& entry, CoreBase& core) -> void {
  for(size_t i = 0; i + 1 < entry.codes.size(); i += 2) {
    u32 address = entry.codes[i];
    u32 value = entry.codes[i + 1];
    const u32 cheat_opcode = address >> 28;
    address &= 0x0FFFFFFF;

    switch(cheat_opcode) {
      case 0x0:
        core.PokeByte(address, static_cast<u8>(value));
        break;

      case 0x1:
        core.PokeHalf(address, static_cast<u16>(value));
        break;

      case 0x2:
        core.PokeWord(address, value);
        break;

      case 0x3: {
        const u32 num_addresses = address & 0xFFFF;
        for(u32 index = 0; index < num_addresses; index++) {
          if(i + 3 >= entry.codes.size()) {
            return;
          }

          const u32 address1 = entry.codes[i + 2];
          const u32 address2 = entry.codes[i + 3];
          i += 2;

          core.PokeWord(address1, value);
          if(address2 != 0) {
            core.PokeWord(address2, value);
          }
        }
        break;
      }

      case 0xD:
        if(core.PeekHalf(address) != static_cast<u16>(value & 0xFFFF)) {
          i += 2;
        }
        break;

      case 0xE:
        if(core.PeekHalf(value & 0x0FFFFFFF) != static_cast<u16>(address & 0xFFFF)) {
          const u32 skip = (address >> 16) & 0x03;
          i += skip * 2;
        }
        break;

      default:
        break;
    }
  }
}

auto DCCheatDatabase::ProcessGameSharkV3(Entry const& entry, CoreBase& core) -> void {
  for(size_t i = 0; i + 1 < entry.codes.size(); i += 2) {
    u32 address = entry.codes[i];
    u32 value = entry.codes[i + 1];
    u32 cheat_opcode = address >> 28;
    address &= 0x0FFFFFFF;

    switch(cheat_opcode) {
      case 0x0: {
        cheat_opcode = address >> 24;
        address = (address & 0x000FFFFF) + ((address << 4) & 0x0F000000);

        switch(cheat_opcode) {
          case 0x0: {
            const u32 iterations = value >> 24;
            const u8 byte_value = static_cast<u8>(value & 0xFF);
            for(u32 index = 0; index <= iterations; index++) {
              core.PokeByte(address + index, byte_value);
            }
            break;
          }

          case 0x2: {
            const u32 iterations = value >> 16;
            const u16 half_value = static_cast<u16>(value & 0xFFFF);
            for(u32 index = 0; index <= iterations; index++) {
              core.PokeHalf(address + index * 2, half_value);
            }
            break;
          }

          case 0x4:
            core.PokeWord(address, value);
            break;

          default:
            break;
        }
        break;
      }

      case 0x4: {
        cheat_opcode = address >> 24;
        address = (address & 0x000FFFFF) + ((address << 4) & 0x0F000000);

        switch(cheat_opcode) {
          case 0x0: {
            const u32 target = core.PeekWord(address) + (value >> 24);
            core.PokeByte(target, static_cast<u8>(value & 0xFF));
            break;
          }

          case 0x2: {
            const u32 target = core.PeekWord(address) + ((value >> 16) * 2);
            core.PokeHalf(target, static_cast<u16>(value & 0xFFFF));
            break;
          }

          case 0x4: {
            const u32 target = core.PeekWord(address);
            core.PokeWord(target, value);
            break;
          }

          default:
            break;
        }
        break;
      }

      case 0x8: {
        cheat_opcode = address >> 24;
        address = (address & 0x000FFFFF) + ((address << 4) & 0x0F000000);

        switch(cheat_opcode) {
          case 0x0: {
            const u8 byte_value = static_cast<u8>((value & 0xFF) + core.PeekByte(address));
            core.PokeByte(address, byte_value);
            break;
          }

          case 0x2: {
            const u16 half_value = static_cast<u16>((value & 0xFFFF) + core.PeekHalf(address));
            core.PokeHalf(address, half_value);
            break;
          }

          case 0x4: {
            const u32 word_value = value + core.PeekWord(address);
            core.PokeWord(address, word_value);
            break;
          }

          default:
            break;
        }
        break;
      }

      case 0xC: {
        cheat_opcode = address >> 24;
        address = (address & 0x00FFFFFF) + 0x04000000;

        switch(cheat_opcode) {
          case 0x6:
            core.PokeHalf(address, static_cast<u16>(value));
            break;

          case 0x7:
            core.PokeWord(address, value);
            break;

          default:
            break;
        }
        break;
      }

      default:
        break;
    }
  }
}

auto DCCheatDatabase::Apply(CoreBase& core) const -> void {
  for(auto const& entry : entries_) {
    if(!entry.enabled) {
      continue;
    }

    switch(entry.variant) {
      case Entry::Variant::GameSharkV1:
        ProcessGameSharkV1(entry, core);
        break;

      case Entry::Variant::GameSharkV3:
        ProcessGameSharkV3(entry, core);
        break;
    }
  }
}

} // namespace nba
