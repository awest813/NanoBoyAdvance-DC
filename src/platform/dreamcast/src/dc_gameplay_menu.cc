// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_gameplay_menu.hh"

#include "dc_paths.hh"

#include <platform/loader/save_state.hh>
#include <platform/writer/save_state.hh>

#include <algorithm>
#include <cstdio>
#include <vector>

#if NBA_DC_HAS_KOS
#include <dc/video.h>
#endif

namespace nba {

namespace {

enum class PauseRow {
  Resume,
  SaveState,
  LoadState,
  StateSlot,
  Cheats,
  ExitToBrowser,
  Count
};

auto SaveStateMessage(
  std::unique_ptr<CoreBase>& core,
  DreamcastConfig& config,
  fs::path const& rom_path
) -> std::string {
  const auto state_path = GetSaveStatePath(config, rom_path, config.save_state_slot);
  const auto state_dir = state_path.parent_path().string();
  if(!state_dir.empty()) {
    EnsureDirectoryPOSIX(state_dir);
  }

  const auto result = SaveStateWriter::Write(core, state_path);
  if(result == SaveStateWriter::Result::Success) {
    char message[48];
    std::snprintf(message, sizeof(message), "Saved state slot %d", config.save_state_slot);
    return message;
  }

  return "Save state failed";
}

auto LoadStateMessage(
  std::unique_ptr<CoreBase>& core,
  DreamcastConfig& config,
  fs::path const& rom_path
) -> std::string {
  const auto state_path = GetSaveStatePath(config, rom_path, config.save_state_slot);
  const auto result = SaveStateLoader::Load(core, state_path);
  if(result == SaveStateLoader::Result::Success) {
    char message[48];
    std::snprintf(message, sizeof(message), "Loaded state slot %d", config.save_state_slot);
    return message;
  }

  return "Load state failed";
}

auto RunCheatMenu(
  DCUI& ui,
  DCInput& input,
  DCCheatDatabase& cheats
) -> void {
  if(cheats.empty()) {
    ui.ShowMessage("Cheats", "No .cht file found for\nthis ROM.\n\nPlace <rom>.cht next\nto the ROM or in\n/cd/gbaDC/.", input, true);
    return;
  }

  int selection = 0;
  const int item_count = static_cast<int>(cheats.size());

  while(true) {
    std::vector<std::string> items;
    items.reserve(item_count + 1);

    for(int i = 0; i < item_count; i++) {
      auto const* entry = cheats.GetEntry(i);
      if(!entry) {
        continue;
      }

      std::string label = entry->name;
      if(label.size() > 24) {
        label.resize(21);
        label += "...";
      }

      label += entry->enabled ? ": On" : ": Off";
      items.push_back(std::move(label));
    }
    items.emplace_back("Back");

    ui.DrawMenu("Cheats", items, selection, 0);

    DCMenuInput menu;
    input.PollMenu(menu);

    if(menu.up) {
      selection = (selection + item_count) % (item_count + 1);
    } else if(menu.down) {
      selection = (selection + 1) % (item_count + 1);
    } else if(menu.left || menu.right) {
      if(selection < item_count) {
        cheats.Toggle(selection);
      }
    } else if(menu.confirm) {
      if(selection == item_count) {
        return;
      }

      cheats.Toggle(selection);
    } else if(menu.cancel) {
      return;
    }

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#endif
  }
}

auto BuildPauseMenuItems(
  DreamcastConfig const& config,
  DCCheatDatabase const& cheats
) -> std::vector<std::string> {
  std::vector<std::string> items;
  items.reserve(static_cast<size_t>(PauseRow::Count));

  items.emplace_back("Resume");

  char slot_line[48];
  std::snprintf(slot_line, sizeof(slot_line), "Save state (slot %d)", config.save_state_slot);
  items.emplace_back(slot_line);

  std::snprintf(slot_line, sizeof(slot_line), "Load state (slot %d)", config.save_state_slot);
  items.emplace_back(slot_line);

  std::snprintf(slot_line, sizeof(slot_line), "State slot: %d", config.save_state_slot);
  items.emplace_back(slot_line);

  if(cheats.empty()) {
    items.emplace_back("Cheats (none)");
  } else {
    items.emplace_back("Cheats");
  }

  items.emplace_back("Exit to browser");
  return items;
}

} // namespace

auto DCGameplayMenu::SaveState(
  std::unique_ptr<CoreBase>& core,
  DreamcastConfig& config,
  fs::path const& rom_path
) -> std::string {
  return SaveStateMessage(core, config, rom_path);
}

auto DCGameplayMenu::LoadState(
  std::unique_ptr<CoreBase>& core,
  DreamcastConfig& config,
  fs::path const& rom_path
) -> std::string {
  return LoadStateMessage(core, config, rom_path);
}

auto DCGameplayMenu::Run(
  DCUI& ui,
  DCInput& input,
  DreamcastConfig& config,
  std::unique_ptr<CoreBase>& core,
  DCCheatDatabase& cheats,
  fs::path const& rom_path
) -> Action {
  int selection = 0;
  std::string status_message;
  int status_frames = 0;

  while(true) {
    auto items = BuildPauseMenuItems(config, cheats);
    ui.DrawMenu("Paused", items, selection, 0);

    if(status_frames > 0) {
      ui.DrawStatusBar(status_message);
      status_frames--;
    } else {
      ui.DrawStatusBar("A=select  B=back  L/R=adjust slot");
    }

    DCMenuInput menu;
    input.PollMenu(menu);

    if(menu.up) {
      selection = (selection + static_cast<int>(PauseRow::Count) - 1) %
        static_cast<int>(PauseRow::Count);
    } else if(menu.down) {
      selection = (selection + 1) % static_cast<int>(PauseRow::Count);
    } else if(menu.left && selection == static_cast<int>(PauseRow::StateSlot)) {
      config.save_state_slot = std::clamp(
        config.save_state_slot - 1,
        0,
        DreamcastConfig::kSaveStateSlotCount - 1
      );
    } else if(menu.right && selection == static_cast<int>(PauseRow::StateSlot)) {
      config.save_state_slot = std::clamp(
        config.save_state_slot + 1,
        0,
        DreamcastConfig::kSaveStateSlotCount - 1
      );
    } else if(menu.confirm) {
      switch(static_cast<PauseRow>(selection)) {
        case PauseRow::Resume:
          return Action::Resume;

        case PauseRow::SaveState:
          status_message = SaveStateMessage(core, config, rom_path);
          status_frames = 90;
          break;

        case PauseRow::LoadState:
          status_message = LoadStateMessage(core, config, rom_path);
          status_frames = 90;
          break;

        case PauseRow::StateSlot:
          break;

        case PauseRow::Cheats:
          RunCheatMenu(ui, input, cheats);
          break;

        case PauseRow::ExitToBrowser:
          return Action::ExitToBrowser;

        case PauseRow::Count:
          break;
      }
    } else if(menu.cancel) {
      return Action::Resume;
    }

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#endif
  }
}

} // namespace nba
