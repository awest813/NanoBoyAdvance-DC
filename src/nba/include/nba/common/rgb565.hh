// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <nba/integer.hh>

namespace nba {

inline auto RGB555ToRGB565(u16 rgb555) -> u16 {
  static const std::array<u16, 32768> table = []() constexpr {
    std::array<u16, 32768> values{};
    for(int index = 0; index < 32768; index++) {
      const int r = (index >> 0) & 31;
      const int g = (index >> 5) & 31;
      const int b = (index >> 10) & 31;
      values[index] = static_cast<u16>((r << 11) | (g << 6) | b);
    }
    return values;
  }();

  return table[rgb555 & 0x7FFF];
}

} // namespace nba
