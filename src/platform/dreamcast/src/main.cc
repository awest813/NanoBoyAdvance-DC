// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Dreamcast frontend for NanoBoyAdvance.

#include <nba/core.hh>
#include <platform/loader/bios.hh>
#include <platform/loader/rom.hh>

#include "dc_autoboot.hh"
#include "dc_auto_frameskip.hh"
#include "dc_log.hh"
#include "dc_config.hh"
#include "dc_frontend.hh"
#include "dc_memory.hh"
#include "dc_paths.hh"
#include "dc_rom_browser.hh"
#include "dc_session.hh"
#include "dc_ui.hh"
#include "dc_version.hh"
#include "device/dc_video_device.hh"
#include "device/dc_input.hh"
#include "open_bios.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#if defined(PLATFORM_DREAMCAST) && __has_include(<kos.h>)
#define NBA_DC_HAS_KOS 1
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#else
#define NBA_DC_HAS_KOS 0
#endif

using namespace nba;

auto BIOSLoader::LoadEmbedded(std::unique_ptr<CoreBase>& core) -> Result {
  std::vector<u8> file_data(kOpenBIOS, kOpenBIOS + 16384);
  core->Attach(file_data);
  return Result::Success;
}

int main() {
  std::printf("NanoBoyAdvance Dreamcast Edition\n");

  auto video_device = std::make_shared<DCVideoDevice>();
  if(!video_device->Initialize()) {
    std::printf("Error: failed to initialize video device.\n");
    return 1;
  }

  DCUI ui{*video_device};
  DCInput input;

  auto config = std::make_shared<DreamcastConfig>();
  const auto config_load = config->TryLoadDreamcast(DreamcastConfig::kDefaultConfigPath);
  EnsureDirectoryPOSIX(DreamcastConfig::kDefaultSaveFolder);
  EnsureDirectoryPOSIX(DreamcastConfig::kDefaultStateFolder);

  if(config_load == ConfigLoadResult::ParseError) {
    ui.ShowMessage(
      "Settings Error",
      "Could not read /pc/nba-dc.toml.\nUsing default settings.\n\n"
      "Fix the file or adjust options\nin Settings and save again.",
      input,
      true
    );
  } else if(config_load == ConfigLoadResult::EmptyFile) {
    ui.ShowMessage(
      "Settings Empty",
      "/pc/nba-dc.toml is empty.\nUsing default settings.\n\n"
      "Adjust options in Settings and\nsave to recreate the file.",
      input,
      true
    );
  }

  // Dynarec A/B override for smoke/CI. Unset leaves config as-is.
  if(const char* dynarec_env = std::getenv("NBA_DC_DYNAREC")) {
    const bool enable =
      (dynarec_env[0] == '1' && dynarec_env[1] == '\0') ||
      std::strcmp(dynarec_env, "true") == 0 ||
      std::strcmp(dynarec_env, "yes") == 0 ||
      std::strcmp(dynarec_env, "on") == 0;
    const bool disable =
      (dynarec_env[0] == '0' && dynarec_env[1] == '\0') ||
      std::strcmp(dynarec_env, "false") == 0 ||
      std::strcmp(dynarec_env, "no") == 0 ||
      std::strcmp(dynarec_env, "off") == 0;
    if(enable) {
      config->cpu_dynarec = true;
      DCLog("[NBA-DC] NBA_DC_DYNAREC=%s — dynarec enabled\n", dynarec_env);
    } else if(disable) {
      config->cpu_dynarec = false;
      DCLog("[NBA-DC] NBA_DC_DYNAREC=%s — dynarec disabled\n", dynarec_env);
    } else {
      DCLog("[NBA-DC] NBA_DC_DYNAREC=%s ignored (use 1/0, true/false, on/off)\n", dynarec_env);
    }
  }

  // Autoboot via environment variable (used by host smoke tests / CI).
  if(const char* autoboot_env = std::getenv("NBA_DC_AUTOBOOT_ROM")) {
    DCLog("[NBA-DC] Autoboot env ROM: %s\n", autoboot_env);
    const bool loaded = RunGameSession(
      ui, input, config, video_device, fs::path{autoboot_env}
    );
    return loaded ? 0 : 1;
  }

  // Tekken autoboot for CDI-only testing.
  if(kDreamcastAutobootTekken) {
    std::string autoboot_report;
    const auto autoboot_path = ResolveAutobootROMPath(*config, autoboot_report);
    DCLog("[NBA-DC] Autoboot ROM: %s\n", autoboot_path.string().c_str());

    ui.ClearScreen();
    ui.DrawTitle("Autoboot");
    ui.DrawTextMultiline(
      48, 88,
      std::string{"AUTOBOOT\n"} + autoboot_path.string() + "\n\n" + autoboot_report
    );
    ui.DrawStatusBar("CDI-only Tekken stability test");
    ui.Present();
    HoldDebugBreadcrumbFrames(120);

    const bool loaded = RunGameSession(ui, input, config, video_device, autoboot_path);
    ui.ShowMessage(
      loaded ? "Autoboot ended" : "Autoboot failed",
      loaded ? "Session returned from emulator." : "RunGameSession returned false.",
      input, true
    );
    return loaded ? 0 : 1;
  }

  // Main loop: scan ROMs, show browser, launch sessions.
  while(true) {
    DCLog("[NBA-DC] Frontend: scanning ROMs\n");

    char scan_status[80];
    std::snprintf(
      scan_status, sizeof(scan_status),
      "Scanning %s, /cd, /cd/gbaDC",
      config->rom_folder.c_str()
    );
    ui.DrawSplash("Loading library...", BuildDreamcastBootInfo(), scan_status);

    auto entries = ROMBrowser::Scan(*config);

    DCLog(
      "[NBA-DC] Frontend: found %lu ROM%s\n",
      static_cast<unsigned long>(entries.size()),
      entries.size() == 1 ? "" : "s"
    );

    if(!entries.empty()) {
      const auto rom_count = entries.size();
      int unavailable = 0;
      for(auto const& entry : entries) {
        if(!entry.launchable) {
          unavailable++;
        }
      }

      char banner[64];
      if(unavailable > 0) {
        std::snprintf(
          banner, sizeof(banner),
          "%zu ROM%s found (%d need Large ROMs)",
          rom_count, rom_count == 1 ? "" : "s", unavailable
        );
      } else {
        std::snprintf(
          banner, sizeof(banner),
          "%zu ROM%s found",
          rom_count, rom_count == 1 ? "" : "s"
        );
      }

      ui.ShowBriefBanner("Library ready", banner, input);
    }

    auto frontend_result = DCFrontend::Run(ui, input, *config, entries);

    if(frontend_result.action == DCFrontend::Action::ReturnToLoader) {
      ui.ShowMessage("Goodbye", "Returning to loader.", input, false);
      break;
    }

    if(frontend_result.action == DCFrontend::Action::OpenSettings) {
      continue;
    }

    if(frontend_result.action == DCFrontend::Action::LaunchROM) {
      if(!config->SaveDreamcastSafe(DreamcastConfig::kDefaultConfigPath)) {
        ui.ShowMessage(
          "Settings Not Saved",
          "Could not write /pc/nba-dc.toml.\nLaunching anyway.",
          input, true
        );
      }
      const bool session_ok = RunGameSession(
        ui, input, config, video_device, frontend_result.rom_path
      );
      config->SaveDreamcastSafe(DreamcastConfig::kDefaultConfigPath);
      if(session_ok) {
        ui.ShowBriefBanner("Session ended", "Returning to ROM browser...", input);
      }
    }
  }

  return 0;
}
