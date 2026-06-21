// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/core.hh>

#include <filesystem>
#include <string>
#include <vector>

namespace nba {

namespace fs = std::filesystem;

// gpSP-compatible GameShark / PAR cheat file support (.cht).
struct DCCheatDatabase {
  static constexpr int kMaxCheats = 16;
  static constexpr int kMaxLinesPerCheat = 128;
  static constexpr int kNameLength = 32;

  struct Entry {
    std::string name;
    bool enabled = false;
    enum class Variant {
      GameSharkV1,
      GameSharkV3
    } variant = Variant::GameSharkV3;
    std::vector<u32> codes;
  };

  auto LoadForROM(fs::path const& rom_path) -> bool;
  auto Apply(CoreBase& core) const -> void;

  auto empty() const -> bool { return entries_.empty(); }
  auto size() const -> size_t { return entries_.size(); }
  auto GetEntry(size_t index) const -> Entry const*;
  auto SetEnabled(size_t index, bool enabled) -> void;
  auto Toggle(size_t index) -> void;

private:
  static auto DecryptCode(u32& address, u32& value, Entry::Variant variant) -> void;
  static auto ProcessGameSharkV1(Entry const& entry, CoreBase& core) -> void;
  static auto ProcessGameSharkV3(Entry const& entry, CoreBase& core) -> void;

  std::vector<Entry> entries_;
};

auto FindCheatFile(fs::path const& rom_path) -> fs::path;

} // namespace nba
