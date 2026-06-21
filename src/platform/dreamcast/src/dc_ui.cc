// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_ui.hh"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#if NBA_DC_HAS_KOS
#include <dc/video.h>
#elif NBA_DC_HAS_SDL_MENU
#include <SDL3/SDL.h>
#endif

namespace nba {

DCUI::DCUI(DCVideoDevice& video) : video_(video) {}

void DCUI::ClearScreen() {
  video_.ClearScreen();
}

void DCUI::DrawText(int x, int y, std::string_view text) {
  video_.DrawText(x, y, text);
}

void DCUI::DrawTextCentered(int y, std::string_view text) {
  video_.DrawTextCentered(y, text);
}

void DCUI::DrawTextMultiline(int x, int y, std::string_view text) {
  video_.DrawTextMultiline(x, y, text);
}

void DCUI::DrawTitle(std::string_view title) {
  DrawTextCentered(24, title);
  video_.DrawFilledRect(80, 52, 480, 2, 0x1084);
}

void DCUI::DrawSplash(
  std::string_view subtitle,
  std::string_view version_line,
  std::string_view status
) {
  ClearScreen();
  DrawTitle("NanoBoyAdvance");
  DrawTextCentered(120, subtitle);

  if(!version_line.empty()) {
    DrawTextCentered(152, version_line);
  }

  if(!status.empty()) {
    DrawStatusBar(status);
  }

  Present();
}

void DCUI::DrawMenu(
  std::string_view title,
  std::vector<std::string> const& items,
  int selection,
  int scroll_offset,
  std::string_view status,
  DCInput* input
) {
  ClearScreen();
  DrawTitle(title);

  static constexpr int kRowHeight = 24;
  static constexpr int kListTop = 72;
  static constexpr int kListLeft = 40;
  static constexpr int kListWidth = 560;
  static constexpr std::uint16_t kSelectionColor = 0x294A;

  const int item_count = static_cast<int>(items.size());
  const int visible = std::min(item_count - scroll_offset, kMenuVisibleRows);

  for(int row = 0; row < visible; row++) {
    const int index = scroll_offset + row;
    const int y = kListTop + row * kRowHeight;
    const bool selected = index == selection;

    if(selected) {
      video_.DrawFilledRect(kListLeft, y - 2, kListWidth, kRowHeight, kSelectionColor);
    }

    const char* prefix = selected ? "> " : "  ";
    std::string line = TruncateText(std::string{prefix} + items[index], kMenuMaxChars);
    DrawText(kMenuTextX, y, line);
  }

  if(item_count == 0) {
    DrawTextCentered(kListTop, "No items found");
  } else {
    if(scroll_offset > 0) {
      DrawText(520, kListTop - 18, "^");
    }
    if(scroll_offset + kMenuVisibleRows < item_count) {
      DrawText(520, kListTop + kMenuVisibleRows * kRowHeight - 6, "v");
    }

    if(item_count > 1) {
      char position[24];
      std::snprintf(
        position,
        sizeof(position),
        "%d/%d",
        std::clamp(selection, 0, item_count - 1) + 1,
        item_count
      );
      DrawText(544, kListTop, position);
    }
  }

  DrawStatusBar(status);
  if(input && !input->IsControllerConnected()) {
    DrawTextCentered(400, "Connect a controller");
  }
  Present();
}

void DCUI::DrawStatusBar(std::string_view text) {
  video_.DrawStatusBar(TruncateText(text, kStatusBarMaxChars));
}

void DCUI::DrawOverlay(std::string_view text) {
  video_.DrawOverlay(text);
}

void DCUI::Present() {
  video_.Present();
}

void DCUI::ShowBriefBanner(
  std::string_view title,
  std::string_view message,
  DCInput& input,
  int max_frames
) {
  ClearScreen();
  DrawTitle(title);
  DrawTextCentered(120, message);
  Present();

  for(int frame = 0; frame < max_frames; frame++) {
    DCMenuInput menu;
    input.PollMenu(menu);
    if(menu.confirm || menu.cancel || menu.start || menu.settings) {
      break;
    }

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#elif NBA_DC_HAS_SDL_MENU
    SDL_Delay(16);
#endif
  }
}

void DCUI::ShowMessage(
  std::string_view title,
  std::string_view message,
  DCInput& input,
  bool wait_for_start
) {
  ClearScreen();
  DrawTitle(title);
  DrawTextMultiline(48, 96, message);

  if(wait_for_start) {
    DrawStatusBar("Press Start or B to continue");
  }

  Present();

  if(!wait_for_start) {
    return;
  }

  while(true) {
    DCMenuInput menu;
    input.PollMenu(menu);
    if(menu.start || menu.cancel) {
      break;
    }

    if(!input.IsControllerConnected()) {
      ClearScreen();
      DrawTitle(title);
      DrawTextMultiline(48, 96, message);
      DrawStatusBar("Connect a controller");
      Present();
    }

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#elif NBA_DC_HAS_SDL_MENU
    SDL_Delay(16);
#endif
  }
}

void DCUI::ShowFatalError(std::string_view message, DCInput& input) {
  ShowMessage("Error", message, input, true);
}

} // namespace nba
