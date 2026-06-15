// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/config.hh>

#include <string_view>

namespace nba {

enum class GPIODeviceType {
  None = 0,
  RTC = 1,
  SolarSensor = 2
};

struct GameInfo {
  Config::BackupType backup_type = Config::BackupType::Detect;
  GPIODeviceType gpio = GPIODeviceType::None;
  bool mirror = false;
};

auto LookupGameInfo(std::string_view game_code, GameInfo& game_info) -> bool;

constexpr GPIODeviceType operator|(GPIODeviceType lhs, GPIODeviceType rhs) {
  return (GPIODeviceType)((int)lhs | (int)rhs);
}

constexpr int operator&(GPIODeviceType lhs, GPIODeviceType rhs) {
  return (int)lhs & (int)rhs;
}

} // namespace nba
