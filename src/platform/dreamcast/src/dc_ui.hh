// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "device/dc_video_device.hh"
#include "device/dc_input.hh"

#include <string>
#include <string_view>
#include <vector>

namespace nba {

inline constexpr int kMenuVisibleRows = 10;
inline constexpr int kMenuCharWidth = 8;
inline constexpr int kMenuTextX = 48;
inline constexpr int kMenuPositionX = 544;
inline constexpr int kMenuMaxChars =
  (kMenuPositionX - kMenuTextX) / kMenuCharWidth;
inline constexpr int kStatusBarMaxChars = 74;

inline auto TruncateText(std::string_view text, size_t max_chars) -> std::string {
  if(text.size() <= max_chars) {
    return std::string{text};
  }

  if(max_chars <= 3) {
    return std::string{text.substr(0, max_chars)};
  }

  return std::string{text.substr(0, max_chars - 3)} + "...";
}

inline void SyncMenuScrollOffset(
  int selection,
  int& scroll_offset,
  int visible_rows = kMenuVisibleRows
) {
  if(selection < scroll_offset) {
    scroll_offset = selection;
  } else if(selection >= scroll_offset + visible_rows) {
    scroll_offset = selection - visible_rows + 1;
  }
}

struct DCUI {
  explicit DCUI(DCVideoDevice& video);

  void ClearScreen();
  void DrawText(int x, int y, std::string_view text);
  void DrawTextCentered(int y, std::string_view text);
  void DrawTextMultiline(int x, int y, std::string_view text);
  void DrawTitle(std::string_view title);
  void DrawSplash(
    std::string_view subtitle,
    std::string_view version_line = {},
    std::string_view status = {}
  );
  void DrawMenu(
    std::string_view title,
    std::vector<std::string> const& items,
    int selection,
    int scroll_offset,
    std::string_view status = "A=Select  B=Back  Y=Settings  Start=Loader",
    DCInput* input = nullptr
  );
  void DrawStatusBar(std::string_view text);
  void DrawOverlay(std::string_view text);
  void Present();

  auto ShowBriefBanner(
    std::string_view title,
    std::string_view message,
    DCInput& input,
    int max_frames = 90
  ) -> void;

  auto ShowMessage(
    std::string_view title,
    std::string_view message,
    DCInput& input,
    bool wait_for_start = true
  ) -> void;

  auto ShowFatalError(
    std::string_view message,
    DCInput& input
  ) -> void;

private:
  DCVideoDevice& video_;
};

} // namespace nba
