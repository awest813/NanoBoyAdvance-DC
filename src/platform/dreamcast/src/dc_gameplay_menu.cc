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
  Controls,
  Reset,
  ExitToBrowser,
  Count
};

// Modal yes/no prompt used for actions that discard progress (reset, exit).
// Defaults the selection to "No" so a stray A press cannot drop the session.
auto ConfirmAction(
  DCUI& ui,
  DCInput& input,
  std::string_view title,
  std::string_view prompt
) -> bool {
  std::vector<std::string> items{"No", "Yes"};
  int selection = 0;

  while(true) {
    ui.DrawMenu(title, items, selection, 0, prompt, &input);

    DCMenuInput menu;
    input.PollMenu(menu);

    if(menu.up || menu.down) {
      selection ^= 1;
    } else if(menu.confirm) {
      return selection == 1;
    } else if(menu.cancel) {
      return false;
    }

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#endif
  }
}

auto ShowControlsHelp(DCUI& ui, DCInput& input) -> void {
  ui.ShowMessage(
    "Controls",
    "In-game shortcuts:\n\n"
    "Start + B        Pause menu\n"
    "X + Y + Start    Quick save\n"
    "X + Y + Select   Quick load\n"
    "X + Y + Left/Right  Change slot\n"
    "Start+A+B+X+Y   Exit (hold)",
    input,
    true
  );
}

auto DescribeSaveStateWriteResult(SaveStateWriter::Result result) -> const char* {
  switch(result) {
    case SaveStateWriter::Result::CannotOpenFile:
      return "Cannot create state file.\nCheck state folder is writable.";
    case SaveStateWriter::Result::CannotWrite:
      return "Write failed.\nDisk may be full.";
    case SaveStateWriter::Result::Success:
      break;
  }

  return "Save state failed";
}

auto DescribeSaveStateLoadResult(SaveStateLoader::Result result) -> const char* {
  switch(result) {
    case SaveStateLoader::Result::CannotFindFile:
      return "No save state in this slot.\nSave first or pick another slot.";
    case SaveStateLoader::Result::CannotOpenFile:
      return "Cannot open state file.\nCheck state folder is readable.";
    case SaveStateLoader::Result::BadImage:
      return "State file is corrupt.\nTry another slot.";
    case SaveStateLoader::Result::UnsupportedVersion:
      return "State version not supported.\nSave a new state in this slot.";
    case SaveStateLoader::Result::Success:
      break;
  }

  return "Load state failed";
}

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

  return DescribeSaveStateWriteResult(result);
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

  return DescribeSaveStateLoadResult(result);
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
  int scroll_offset = 0;
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

    SyncMenuScrollOffset(selection, scroll_offset);

    ui.DrawMenu("Cheats", items, selection, scroll_offset, "A/Left/Right=Toggle  B=Back", &input);

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

    SyncMenuScrollOffset(selection, scroll_offset);

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

  items.emplace_back("Controls");
  items.emplace_back("Reset game");
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
  int scroll_offset = 0;
  std::string status_message;
  int status_frames = 0;

  while(true) {
    auto items = BuildPauseMenuItems(config, cheats);
    const auto status = status_frames > 0
      ? std::string_view{status_message}
      : std::string_view{"A=Select  B=Resume  Left/Right=Adjust slot"};
    SyncMenuScrollOffset(selection, scroll_offset);
    ui.DrawMenu("Paused", items, selection, scroll_offset, status, &input);
    if(status_frames > 0) {
      status_frames--;
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

        case PauseRow::SaveState: {
          const auto message = SaveStateMessage(core, config, rom_path);
          if(message.rfind("Saved", 0) == 0) {
            status_message = message;
            status_frames = 90;
          } else {
            ui.ShowMessage("Save Failed", message, input, true);
          }
          break;
        }

        case PauseRow::LoadState: {
          const auto message = LoadStateMessage(core, config, rom_path);
          if(message.rfind("Loaded", 0) == 0) {
            status_message = message;
            status_frames = 90;
          } else {
            ui.ShowMessage("Load Failed", message, input, true);
          }
          break;
        }

        case PauseRow::StateSlot:
          break;

        case PauseRow::Cheats:
          RunCheatMenu(ui, input, cheats);
          break;

        case PauseRow::Controls:
          ShowControlsHelp(ui, input);
          break;

        case PauseRow::Reset:
          if(ConfirmAction(
              ui,
              input,
              "Reset game?",
              "Unsaved progress is lost  A=Confirm  B=Cancel"
          )) {
            core->Reset();
            status_message = "Game reset";
            status_frames = 90;
          }
          break;

        case PauseRow::ExitToBrowser:
          if(ConfirmAction(
              ui,
              input,
              "Exit to browser?",
              "Saves are flushed on exit  A=Confirm  B=Cancel"
          )) {
            return Action::ExitToBrowser;
          }
          break;

        case PauseRow::Count:
          break;
      }
    } else if(menu.cancel) {
      return Action::Resume;
    }

    SyncMenuScrollOffset(selection, scroll_offset);

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#endif
  }
}

} // namespace nba
