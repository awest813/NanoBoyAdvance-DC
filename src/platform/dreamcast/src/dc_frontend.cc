// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_frontend.hh"

#include "dc_memory.hh"
#include "dc_paths.hh"
#include "dc_rom_browser.hh"

#include <algorithm>
#include <array>
#include <cstdio>

#if NBA_DC_HAS_KOS
#include <dc/video.h>
#endif

namespace nba {

namespace {

struct SettingRow {
  const char* label;
  const char* help;
  std::string (*getter)(DreamcastConfig const&);
  void (*adjust)(DreamcastConfig&, int direction);
  bool (*is_dimmed)(DreamcastConfig const&) = nullptr;
};

auto PerformanceLabel(DreamcastConfig const& config) -> std::string {
  return DreamcastConfig::ProfileName(config.performance_profile);
}

auto ShowFpsLabel(DreamcastConfig const& config) -> std::string {
  return config.show_fps ? "On" : "Off";
}

auto AllowLargeRomsLabel(DreamcastConfig const& config) -> std::string {
  if(HasExtendedRAM()) {
    return "Auto (32 MB)";
  }

  return config.allow_large_roms ? "On" : "Off";
}

auto AllowLargeRomsDimmed(DreamcastConfig const& config) -> bool {
  (void)config;
  return HasExtendedRAM();
}

auto FrameSkipLabel(DreamcastConfig const& config) -> std::string {
  if(config.auto_frame_skip) {
    return "Auto";
  }

  return std::to_string(config.frame_skip);
}

auto AudioBufferLabel(DreamcastConfig const& config) -> std::string {
  switch(config.audio_buffer_size) {
    case 2048: return "Small (2 KiB)";
    case 8192: return "Large (8 KiB)";
    default:   return "Medium (4 KiB)";
  }
}

auto BiosPathLabel(DreamcastConfig const& config) -> std::string {
  return config.bios_path;
}

auto ROMFolderLabel(DreamcastConfig const& config) -> std::string {
  return config.rom_folder;
}

auto SaveFolderLabel(DreamcastConfig const& config) -> std::string {
  return config.save_folder;
}

auto StateFolderLabel(DreamcastConfig const& config) -> std::string {
  return config.state_folder;
}

void AdjustPerformance(DreamcastConfig& config, int direction) {
  static constexpr std::array<DreamcastConfig::PerformanceProfile, 3> kProfiles{
    DreamcastConfig::PerformanceProfile::Accuracy,
    DreamcastConfig::PerformanceProfile::Balanced,
    DreamcastConfig::PerformanceProfile::Speed
  };
  const auto current = std::find(kProfiles.begin(), kProfiles.end(), config.performance_profile);
  int index = current == kProfiles.end() ? 1 : static_cast<int>(current - kProfiles.begin());
  index = std::clamp(index + direction, 0, static_cast<int>(kProfiles.size()) - 1);
  config.ApplyPerformanceProfile(kProfiles[index]);
}

void AdjustShowFps(DreamcastConfig& config, int direction) {
  (void)direction;
  config.show_fps = !config.show_fps;
}

void AdjustAllowLargeRoms(DreamcastConfig& config, int direction) {
  (void)direction;
  if(HasExtendedRAM()) {
    return;
  }
  config.allow_large_roms = !config.allow_large_roms;
}

auto PvrDmaUploadLabel(DreamcastConfig const& config) -> std::string {
  return config.pvr_dma_upload ? "DMA (faster)" : "Copy (safer)";
}

void AdjustPvrDmaUpload(DreamcastConfig& config, int direction) {
  (void)direction;
  config.pvr_dma_upload = !config.pvr_dma_upload;
}

void AdjustFrameSkip(DreamcastConfig& config, int direction) {
  const int current_index = config.auto_frame_skip ? 0 : config.frame_skip + 1;
  const int next_index = std::clamp(current_index + direction, 0, 4);
  config.auto_frame_skip = next_index == 0;
  if(!config.auto_frame_skip) {
    config.frame_skip = next_index - 1;
  }
}

void AdjustAudioBuffer(DreamcastConfig& config, int direction) {
  static constexpr std::array<int, 3> kSizes{2048, 4096, 8192};
  auto current = std::find(kSizes.begin(), kSizes.end(), config.audio_buffer_size);
  if(current == kSizes.end()) {
    current = kSizes.begin() + 1;
  }

  const int index = static_cast<int>(current - kSizes.begin());
  const int next = std::clamp(index + direction, 0, static_cast<int>(kSizes.size()) - 1);
  config.audio_buffer_size = kSizes[next];
}

void AdjustBiosPath(DreamcastConfig& config, int direction) {
  static constexpr std::array<const char*, 2> kPaths{"/cd/bios.bin", "/pc/bios.bin"};
  const auto current = std::find(kPaths.begin(), kPaths.end(), config.bios_path);
  int index = current == kPaths.end() ? 0 : static_cast<int>(current - kPaths.begin());
  index = (index + direction + static_cast<int>(kPaths.size())) % static_cast<int>(kPaths.size());
  config.bios_path = kPaths[index];
}

void AdjustROMFolder(DreamcastConfig& config, int direction) {
  static constexpr std::array<const char*, 2> kPaths{"/pc/roms", "/cd"};
  const auto current = std::find(kPaths.begin(), kPaths.end(), config.rom_folder);
  int index = current == kPaths.end() ? 0 : static_cast<int>(current - kPaths.begin());
  index = (index + direction + static_cast<int>(kPaths.size())) % static_cast<int>(kPaths.size());
  config.rom_folder = kPaths[index];
}

void AdjustSaveFolder(DreamcastConfig& config, int direction) {
  static constexpr std::array<const char*, 3> kPaths{"/pc/saves", "/pc", "/vmu/a1"};
  const auto current = std::find(kPaths.begin(), kPaths.end(), config.save_folder);
  int index = current == kPaths.end() ? 0 : static_cast<int>(current - kPaths.begin());
  index = (index + direction + static_cast<int>(kPaths.size())) % static_cast<int>(kPaths.size());
  config.save_folder = kPaths[index];
}

void AdjustStateFolder(DreamcastConfig& config, int direction) {
  static constexpr std::array<const char*, 3> kPaths{"/pc/states", "/pc", "/vmu/a1"};
  const auto current = std::find(kPaths.begin(), kPaths.end(), config.state_folder);
  int index = current == kPaths.end() ? 0 : static_cast<int>(current - kPaths.begin());
  index = (index + direction + static_cast<int>(kPaths.size())) % static_cast<int>(kPaths.size());
  config.state_folder = kPaths[index];
}

constexpr SettingRow kSettings[] {
  { "Performance", "Accuracy / Balanced / Speed preset", PerformanceLabel, AdjustPerformance },
  { "Show FPS", "On-screen FPS, EF, frame skip, page misses", ShowFpsLabel, AdjustShowFps },
  { "PVR upload", "DMA is faster; Copy is a safer fallback", PvrDmaUploadLabel, AdjustPvrDmaUpload },
  { "Large ROMs (>8 MiB)", "Only needed for 32 MB RAM mods", AllowLargeRomsLabel, AdjustAllowLargeRoms, AllowLargeRomsDimmed },
  { "Frame skip", "Auto scales under load; 0-3 is fixed", FrameSkipLabel, AdjustFrameSkip },
  { "Audio buffer", "Smaller = less latency; larger = safer", AudioBufferLabel, AdjustAudioBuffer },
  { "BIOS path", "GBA BIOS file location", BiosPathLabel, AdjustBiosPath },
  { "Primary ROM folder", "First folder scanned for games", ROMFolderLabel, AdjustROMFolder },
  { "Save folder", "SRAM/FLASH/EEPROM save location", SaveFolderLabel, AdjustSaveFolder },
  { "State folder", "Save-state file location", StateFolderLabel, AdjustStateFolder }
};

auto BuildMenuItems(std::vector<ROMEntry> const& entries) -> std::vector<std::string> {
  std::vector<std::string> items;
  items.reserve(entries.size());

  for(auto const& entry : entries) {
    items.push_back(entry.label);
  }

  return items;
}

auto BuildMenuStyles(std::vector<ROMEntry> const& entries) -> std::vector<MenuItemStyle> {
  std::vector<MenuItemStyle> styles;
  styles.reserve(entries.size());
  for(auto const& entry : entries) {
    styles.push_back(entry.launchable ? MenuItemStyle::Normal : MenuItemStyle::Dim);
  }
  return styles;
}

auto BuildRomBrowserStatus(DreamcastConfig const& config) -> std::string {
  char buffer[96];
  std::snprintf(
    buffer,
    sizeof(buffer),
    "A=Launch  B=Back  Y=Settings  |  %s",
    DreamcastConfig::ProfileName(config.performance_profile)
  );
  return buffer;
}

auto SettingsDirty(DreamcastConfig const& draft, DreamcastConfig const& saved) -> bool {
  return draft.performance_profile != saved.performance_profile ||
         draft.show_fps != saved.show_fps ||
         draft.pvr_dma_upload != saved.pvr_dma_upload ||
         draft.allow_large_roms != saved.allow_large_roms ||
         draft.frame_skip != saved.frame_skip ||
         draft.auto_frame_skip != saved.auto_frame_skip ||
         draft.audio_buffer_size != saved.audio_buffer_size ||
         draft.bios_path != saved.bios_path ||
         draft.rom_folder != saved.rom_folder ||
         draft.save_folder != saved.save_folder ||
         draft.state_folder != saved.state_folder ||
         draft.audio.mp2k_hle_enable != saved.audio.mp2k_hle_enable ||
         draft.ppu_fast_mode != saved.ppu_fast_mode ||
         draft.skip_bios != saved.skip_bios;
}

} // namespace

auto DCSettingsMenu::Run(
  DCUI& ui,
  DCInput& input,
  DreamcastConfig& config
) -> bool {
  DreamcastConfig draft = config;
  int selection = 0;
  int scroll_offset = 0;
  const int item_count = static_cast<int>(std::size(kSettings));

  while(true) {
    std::vector<std::string> items;
    std::vector<MenuItemStyle> styles;
    items.reserve(item_count + 1);
    styles.reserve(item_count + 1);

    for(auto const& setting : kSettings) {
      items.push_back(std::string{setting.label} + ": " + setting.getter(draft));
      const bool dim = setting.is_dimmed && setting.is_dimmed(draft);
      styles.push_back(dim ? MenuItemStyle::Dim : MenuItemStyle::Normal);
    }
    items.emplace_back("Save and return");
    styles.push_back(MenuItemStyle::Normal);

    SyncMenuScrollOffset(selection, scroll_offset);

    char status[96];
    if(selection < item_count) {
      std::snprintf(status, sizeof(status), "%s", kSettings[selection].help);
    } else {
      std::snprintf(
        status,
        sizeof(status),
        "A=Save  B=Cancel  |  %s",
        DreamcastConfig::ProfileName(draft.performance_profile)
      );
    }

    ui.DrawMenu(
      "Settings",
      items,
      selection,
      scroll_offset,
      status,
      &input,
      &styles
    );

    DCMenuInput menu;
    input.PollMenu(menu);

    if(menu.up) {
      selection = (selection + item_count) % (item_count + 1);
    } else if(menu.down) {
      selection = (selection + 1) % (item_count + 1);
    } else if(menu.left && selection < item_count) {
      kSettings[selection].adjust(draft, -1);
    } else if(menu.right && selection < item_count) {
      kSettings[selection].adjust(draft, 1);
    } else if(menu.confirm) {
      if(selection == item_count) {
        const bool saved = draft.SaveDreamcastSafe(DreamcastConfig::kDefaultConfigPath);
        if(saved) {
          config = draft;
          ui.ShowMessage(
            "Settings Saved",
            "Your settings were written to\n/pc/nba-dc.toml.",
            input,
            true
          );
          return true;
        }

        ui.ShowMessage(
          "Save Failed",
          "Could not write settings.\nCheck that /pc is writable.",
          input,
          true
        );
      }
    } else if(menu.cancel) {
      if(SettingsDirty(draft, config)) {
        if(!ui.ConfirmAction(
             "Discard changes?",
             "Unsaved settings will be lost  A=Confirm  B=Keep editing",
             input
           )) {
          continue;
        }
      }
      return false;
    }

    SyncMenuScrollOffset(selection, scroll_offset);

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#endif
  }
}

auto DCFrontend::Run(
  DCUI& ui,
  DCInput& input,
  DreamcastConfig& config,
  std::vector<ROMEntry> const& entries
) -> Result {
  if(entries.empty()) {
    int selection = 0;
    while(true) {
      std::vector<std::string> items{
        "Open Settings",
        "Rescan library",
        "Return to loader"
      };
      ui.DrawMenu(
        "No ROMs Found",
        items,
        selection,
        0,
        "Place .gba files in /pc/roms, /cd, or /cd/gbaDC",
        &input
      );

      DCMenuInput menu;
      input.PollMenu(menu);

      if(menu.up) {
        selection = (selection + 2) % 3;
      } else if(menu.down) {
        selection = (selection + 1) % 3;
      } else if(menu.confirm) {
        switch(selection) {
          case 0:
            if(DCSettingsMenu::Run(ui, input, config)) {
              return Result{Action::OpenSettings, {}};
            }
            break;
          case 1:
            return Result{Action::OpenSettings, {}}; // main loop rescans
          case 2:
            return Result{Action::ReturnToLoader, {}};
          default:
            break;
        }
      } else if(menu.settings) {
        if(DCSettingsMenu::Run(ui, input, config)) {
          return Result{Action::OpenSettings, {}};
        }
      } else if(menu.cancel || menu.start) {
        return Result{Action::ReturnToLoader, {}};
      }

#if NBA_DC_HAS_KOS
      vid_waitvbl();
#endif
    }
  }

  auto menu_items = BuildMenuItems(entries);
  auto menu_styles = BuildMenuStyles(entries);
  int selection = 0;
  int scroll_offset = 0;

  if(!config.last_rom.empty()) {
    for(int i = 0; i < static_cast<int>(entries.size()); i++) {
      if(entries[i].path == config.last_rom) {
        selection = i;
        break;
      }
    }
  }

  SyncMenuScrollOffset(selection, scroll_offset);

  char rom_title[40];
  std::snprintf(
    rom_title,
    sizeof(rom_title),
    "Select ROM (%d)",
    static_cast<int>(entries.size())
  );

  auto browser_status = BuildRomBrowserStatus(config);

  while(true) {
    ui.DrawMenu(
      rom_title,
      menu_items,
      selection,
      scroll_offset,
      browser_status,
      &input,
      &menu_styles
    );

    DCMenuInput menu;
    input.PollMenu(menu);

    if(menu.up) {
      selection = (selection + static_cast<int>(entries.size()) - 1) % static_cast<int>(entries.size());
    } else if(menu.down) {
      selection = (selection + 1) % static_cast<int>(entries.size());
    } else if(menu.confirm) {
      const auto& entry = entries[selection];
      if(!entry.launchable) {
        ui.ShowMessage(
          "Large ROM Unavailable",
          "This ROM is larger than 8 MiB and\nneeds more RAM than stock Dreamcast\n"
          "provides.\n\nEnable \"Large ROMs\" in Settings\n(for a 32 MB RAM mod) to play it.",
          input,
          true
        );
        continue;
      }

      config.last_rom = entry.path.string();
      return Result{Action::LaunchROM, entry.path};
    } else if(menu.settings) {
      if(DCSettingsMenu::Run(ui, input, config)) {
        return Result{Action::OpenSettings, {}};
      }
      browser_status = BuildRomBrowserStatus(config);
    } else if(menu.start) {
      return Result{Action::ReturnToLoader, {}};
    } else if(menu.cancel) {
      return Result{Action::ReturnToLoader, {}};
    }

    SyncMenuScrollOffset(selection, scroll_offset);

#if NBA_DC_HAS_KOS
    vid_waitvbl();
#endif
  }
}

} // namespace nba
