// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(PLATFORM_DREAMCAST) && __has_include(<kos.h>)
#define NBA_DC_HAS_KOS 1
#include <kos.h>
#else
#define NBA_DC_HAS_KOS 0
#endif

#include "dc_session.hh"

#include "dc_autoboot.hh"
#include "dc_auto_frameskip.hh"
#include "dc_cheats.hh"
#include "dc_frame_timing.hh"
#include "dc_gameplay_menu.hh"
#include "dc_log.hh"
#include "dc_memory.hh"
#include "dc_paths.hh"
#include "dc_rom_browser.hh"
#include "device/dc_audio_device.hh"
#include "open_bios.hh"

#include <nba/core.hh>
#include <platform/frame_limiter.hh>
#include <platform/loader/bios.hh>
#include <platform/loader/rom.hh>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

namespace nba {

namespace {

// Launch breadcrumbs always log to stdout for Flycast diagnosis.  Full-screen
// holds are disabled in normal builds because they add several seconds of boot
// delay and can mask timing-sensitive launch failures.
static constexpr bool kDreamcastLaunchScreenBreadcrumbs = false;
static constexpr size_t kMaxGBAROMSize = 32 * 1024 * 1024;

} // namespace

auto RunGameSession(
  DCUI& ui,
  DCInput& input,
  std::shared_ptr<DreamcastConfig>& config,
  std::shared_ptr<DCVideoDevice>& video_device,
  fs::path const& rom_path
) -> bool {
  if(!config || !video_device) {
    DCLog("[NBA-DC] RunGameSession: null config or video device\n");
    return false;
  }

  const auto bios_path = config->bios_path;

  auto breadcrumb = [&](const char* phase, std::string const& detail = {}) {
    if(detail.empty()) {
      DCLog("[NBA-DC] %s\n", phase);
    } else {
      DCLog("[NBA-DC] %s: %s\n", phase, detail.c_str());
    }

    if(!kDreamcastLaunchScreenBreadcrumbs) {
      return;
    }

    ui.ClearScreen();
    ui.DrawTitle("Debug");
    ui.DrawTextMultiline(48, 96, std::string{phase} + (detail.empty() ? "" : "\n" + detail));
    ui.DrawStatusBar("Launching ROM...");
    ui.Present();
    HoldDebugBreadcrumbFrames(30);
  };

  breadcrumb("Phase 1: BIOS check", bios_path);
  auto bios_result = BIOSLoader::Validate(bios_path);
  bool using_embedded_bios = false;

  if(bios_result != BIOSLoader::Result::Success) {
    char msg[160];
    std::snprintf(msg, sizeof(msg), "%s not found - using OpenBIOS", bios_path.c_str());
    ui.ClearScreen();
    ui.DrawTitle("OpenBIOS");
    ui.DrawTextMultiline(48, 120, msg);
    ui.DrawStatusBar("Continuing with built-in BIOS...");
    ui.Present();
    using_embedded_bios = true;
  }

  auto rom_result = ROMLoader::Result::Success;
  size_t rom_size = 0;
  auto size_result = ROMLoader::GetFileSize(rom_path, rom_size);
  if(size_result != ROMLoader::Result::Success) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s\n%s",
                  ROMLoader::Describe(size_result), rom_path.c_str());
    ui.ShowFatalError(message, input);
    return false;
  }

  {
    char detail[320];
    std::snprintf(
      detail,
      sizeof(detail),
      "Path: %s\nSource: %s\nSize: %s",
      rom_path.string().c_str(),
      GetROMSourceName(rom_path),
      FormatROMSize(rom_size).c_str()
    );
    breadcrumb("ROM selected", detail);
  }

  if(rom_size > kMaxGBAROMSize) {
    char message[192];
    std::snprintf(
      message,
      sizeof(message),
      "ROM is too large for GBA\n%s\n%s",
      FormatROMSize(rom_size).c_str(),
      rom_path.c_str()
    );
    ui.ShowFatalError(message, input);
    return false;
  }

#if NBA_DC_HAS_KOS
  if(IsDreamcastVirtualPath(rom_path) &&
      rom_size > kStockDreamcastMaxROMSize &&
      !CanLoadLargeROM(*config)) {
    char message[320];
    std::snprintf(
      message,
      sizeof(message),
      "ROM exceeds 8 MiB stock limit\n%s\n%s\n\nEnable Large ROMs in Settings\nor use a 32 MB RAM mod.",
      FormatROMSize(rom_size).c_str(),
      rom_path.c_str()
    );
    ui.ShowFatalError(message, input);
    return false;
  }
#endif

#if NBA_DC_HAS_KOS
  if(IsDreamcastVirtualPath(rom_path)) {
    std::string phase2_detail = FormatROMSize(rom_size);
#if NBA_DC_VFS_HAS_ARCH
    phase2_detail += HasExtendedRAM() ? "\nSystem RAM: 32 MB" : "\nSystem RAM: 16 MB";
#endif
    breadcrumb("Phase 2: ROM size precheck", phase2_detail);
  } else
#endif
  {
    breadcrumb("Phase 2: ROM precheck", FormatROMSize(rom_size));
    rom_result = ROMLoader::Validate(rom_path);
  }
  if(rom_result != ROMLoader::Result::Success) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s\n%s",
                  ROMLoader::Describe(rom_result), rom_path.c_str());
    ui.ShowFatalError(message, input);
    return false;
  }

  const auto save_path = GetSavePath(*config, rom_path);

  {
    const auto save_dir = save_path.parent_path().string();
    if(!save_dir.empty()) {
      const bool dir_ok = IsVMUSaveFolder(save_dir) || EnsureDirectoryPOSIX(save_dir);
      breadcrumb("Phase 3: Save path",
                 save_path.string() + (dir_ok ? " [dir ok]" : " [dir failed]"));
    } else {
      breadcrumb("Phase 3: Save path", save_path.string());
    }
  }

  ui.ClearScreen();
  ui.DrawTitle("Loading");
  ui.DrawTextMultiline(48, 120, std::string{"BIOS: "} + bios_path + "\nROM: " + rom_path.string() +
                                   "\nSave: " + save_path.string());
  ui.DrawStatusBar("Loading game data...");
  ui.Present();

  breadcrumb("Phase 4: Audio device");
  auto audio_device = std::make_shared<DCAudioDevice>();
  breadcrumb("Phase 4A: Audio allocated");
  audio_device->SetBufferSize(config->audio_buffer_size);
  breadcrumb("Phase 4B: Audio buffer set");
  config->audio_dev = audio_device;
  breadcrumb("Phase 4C: Audio config attached");
  config->video_dev = video_device;
  config->video_rgb565_output = true;
  video_device->SetDmaUpload(config->pvr_dma_upload);
  config->dc_ppu_timing_callback = [](long long microseconds) {
    DCFrameTiming::Instance().AddPpuMicros(std::chrono::microseconds(microseconds));
  };
  config->dc_merge_path_callback = [](Config::DcMergePath path) {
    auto& timing = DCFrameTiming::Instance();
    if(!timing.IsEnabled()) return;
    switch(path) {
      case Config::DcMergePath::Slow:   timing.AddMergePathSlow();   break;
      case Config::DcMergePath::Text:   timing.AddMergePathText();   break;
      case Config::DcMergePath::Bitmap: timing.AddMergePathBitmap(); break;
    }
  };
  breadcrumb("Phase 4D: Video config attached");

  breadcrumb("Phase 5: Core create");
  auto core = CreateCore(config);
  if(!core || !audio_device->IsOpened()) {
    ui.ShowFatalError("Failed to initialize emulator core", input);
    return false;
  }

  breadcrumb("Phase 6: BIOS load", using_embedded_bios ? "Embedded OpenBIOS" : bios_path);
  if(using_embedded_bios) {
    bios_result = BIOSLoader::LoadEmbedded(core);
  } else {
    bios_result = BIOSLoader::Load(core, bios_path);
  }
  if(bios_result != BIOSLoader::Result::Success) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s\n%s",
                  BIOSLoader::Describe(bios_result), bios_path.c_str());
    ui.ShowFatalError(message, input);
    return false;
  }

  try {
    breadcrumb("Phase 7: ROM load", rom_path.string());
    rom_result = ROMLoader::Load(
      core,
      rom_path,
      save_path,
      config->cartridge.backup_type,
      GPIODeviceType::None,
      config.get()
    );
  } catch(const std::exception& exception) {
    char message[192];
    std::snprintf(message, sizeof(message), "ROM load error\n%s", exception.what());
    ui.ShowFatalError(message, input);
    return false;
  }

  if(rom_result != ROMLoader::Result::Success) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s\n%s",
                  ROMLoader::Describe(rom_result), rom_path.c_str());
    ui.ShowFatalError(message, input);
    return false;
  }

  breadcrumb("Phase 8: Core reset");
  core->Reset();

  DCCheatDatabase cheats;
  cheats.LoadForROM(rom_path);
  breadcrumb("Phase 9: Enter frame loop");

  static constexpr float kGBAFrameRate =
    static_cast<float>(16777216) / static_cast<float>(280896);
  const int max_frames = ParsePositiveEnvInt("NBA_DC_MAX_FRAMES");
  int frames_run = 0;
  bool running = true;
  bool rom_read_failed = false;
  float measured_fps = 0.0f;
  u32 measured_page_misses = 0;
  std::string gameplay_overlay;
  int gameplay_overlay_frames = 0;
  int active_frame_skip = config->frame_skip;
  int auto_frame_skip_recovery_ticks = 0;
  const bool frame_timing_enabled =
    DCFrameTiming::EnabledFromEnvironment() || config->show_fps;
  DCFrameTiming::Instance().SetEnabled(frame_timing_enabled);
#if NBA_DC_HAS_KOS
  int fps_frame_count = 0;
  auto fps_last_update = std::chrono::steady_clock::now();
  auto last_save_flush = fps_last_update;
#else
  FrameLimiter frame_limiter(kGBAFrameRate);
#endif
  static constexpr int kSaveFlushIntervalSeconds = 60;

  auto on_second_elapsed = [&](float fps) {
    measured_fps = fps;
    measured_page_misses = core->GetROM().TakePageMissCount();
    const double emu_ms_per_display = DCFrameTiming::Instance().EmuMsPerDisplayFrame();
    DCFrameTiming::Instance().OnSecondTick();
    active_frame_skip = UpdateAutoFrameSkip(
      *config,
      measured_fps,
      active_frame_skip,
      auto_frame_skip_recovery_ticks,
      emu_ms_per_display
    );
    const int emulated_fps = static_cast<int>(
      measured_fps * static_cast<float>(active_frame_skip + 1) + 0.5f
    );
    DCLog(
      "[NBA-DC] Runtime: FPS %.1f EF %d FS %s%d PG %lu\n",
      static_cast<double>(measured_fps),
      emulated_fps,
      config->auto_frame_skip ? "A" : "",
      active_frame_skip,
      static_cast<unsigned long>(measured_page_misses)
    );
  };

  while(running) {
#if !NBA_DC_HAS_KOS
    frame_limiter.Run([&]() {
#endif
      DCGameplayRequest gameplay_request;
      if(input.PollInput(*core, gameplay_request)) {
        running = false;
#if !NBA_DC_HAS_KOS
        return;
#endif
      }

      if(gameplay_request.open_pause_menu) {
        input.ClearKeys(*core);
        const auto menu_action = DCGameplayMenu::Run(
          ui,
          input,
          *config,
          core,
          cheats,
          rom_path
        );
        if(menu_action == DCGameplayMenu::Action::ExitToBrowser) {
          running = false;
        }

        if(!config->auto_frame_skip) {
          active_frame_skip = config->frame_skip;
        }
#if !NBA_DC_HAS_KOS
        return;
#else
        continue;
#endif
      }

      if(gameplay_request.save_slot_delta != 0) {
        config->save_state_slot = std::clamp(
          config->save_state_slot + gameplay_request.save_slot_delta,
          0,
          DreamcastConfig::kSaveStateSlotCount - 1
        );
        char slot_text[32];
        std::snprintf(slot_text, sizeof(slot_text), "State slot %d", config->save_state_slot);
        gameplay_overlay = slot_text;
        gameplay_overlay_frames = 90;
      }

      if(gameplay_request.save_state) {
        gameplay_overlay = DCGameplayMenu::SaveState(core, *config, rom_path);
        gameplay_overlay_frames = 90;
      } else if(gameplay_request.load_state) {
        gameplay_overlay = DCGameplayMenu::LoadState(core, *config, rom_path);
        gameplay_overlay_frames = 90;
      }

      if(running) {
        {
          NBA_DC_FRAME_TIMING_SCOPE(Emu);
          DCFrameTiming::Instance().AddEmulatedFrames(active_frame_skip + 1);
          core->RunForDisplayFrame(*config, active_frame_skip, [&]() {
            cheats.Apply(*core);
          });
        }
        DCFrameTiming::Instance().AddPresentedFrames();

        if(core->GetROM().HasReadError()) {
          rom_read_failed = true;
          running = false;
        }

        if(max_frames > 0) {
          frames_run++;
          if(frames_run >= max_frames) {
            DCLog(
              "[NBA-DC] Smoke test: ran %d frame(s), exiting\n",
              frames_run
            );
            running = false;
          }
        }
      }

#if NBA_DC_HAS_KOS
      if(running && core->GetROM().HasBackup() && core->GetROM().IsBackupPersistent()) {
        const auto now = std::chrono::steady_clock::now();
        const auto save_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          now - last_save_flush
        ).count();
        if(save_elapsed >= kSaveFlushIntervalSeconds) {
          core->GetROM().FlushBackup();
          last_save_flush = now;
        }
      }

      if(running) {
        fps_frame_count++;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - fps_last_update
        ).count();
        if(elapsed_ms >= 1000) {
          on_second_elapsed(fps_frame_count * 1000.0f / static_cast<float>(elapsed_ms));
          fps_frame_count = 0;
          fps_last_update = now;
        }
      }
#else
    }, [&](float fps) {
      on_second_elapsed(fps);
    });
#endif

#if NBA_DC_HAS_KOS
    if(audio_device && audio_device->IsOpened()) {
      snd_stream_poll(SND_STREAM_INVALID);
    }

    video_device->Present();

    if(config->show_fps) {
      const int emulated_fps = static_cast<int>(
        measured_fps * static_cast<float>(active_frame_skip + 1) + 0.5f
      );
      char fps_text[56];
      std::snprintf(
        fps_text,
        sizeof(fps_text),
        "FPS %5.1f EF %3d FS %s%d PG %3lu",
        static_cast<double>(measured_fps),
        emulated_fps,
        config->auto_frame_skip ? "A" : "",
        active_frame_skip,
        static_cast<unsigned long>(measured_page_misses)
      );
      video_device->DrawText(8, 8, fps_text);
    }

    if(!input.IsControllerConnected()) {
      video_device->DrawOverlay("Connect a controller");
    } else if(input.IsExitHintActive()) {
      video_device->DrawOverlay("Hold Start+A+B+X+Y to exit");
    }

    if(gameplay_overlay_frames > 0) {
      const std::string overlay_line = FormatGameplayOverlay(gameplay_overlay);
      video_device->DrawOverlay(overlay_line.c_str());
      gameplay_overlay_frames--;
    }
#endif
  }

  bool save_in_memory_only = false;
  bool save_flush_ok = true;
  if(!rom_read_failed && core->GetROM().HasBackup()) {
    save_in_memory_only = !core->GetROM().IsBackupPersistent();
    save_flush_ok = core->GetROM().FlushBackup();
    DCLog(
      "[NBA-DC] Save flush: in_memory_only=%d flush_ok=%d\n",
      save_in_memory_only ? 1 : 0,
      save_flush_ok ? 1 : 0
    );
  }

  ui.ClearScreen();
  core.reset();
  if(rom_read_failed) {
    ui.ShowFatalError("ROM media read failed\nCheck disc/ODE media and try again.", input);
  } else if(save_in_memory_only) {
    if(save_flush_ok) {
      ui.ShowMessage(
        "Save Written",
        "Saves were in-memory this session\n(read-only media during play).\n\n"
        "Your progress was written to disk\non exit.",
        input,
        true
      );
    } else {
      ui.ShowMessage(
        "Save Not Written",
        "Saves could not be written to disk\n(read-only media).\n\n"
        "Progress from this session may be\nlost.  Use a writable /pc save\nfolder to keep saves.",
        input,
        true
      );
    }
  } else if(!save_flush_ok) {
    ui.ShowMessage(
      "Save Not Written",
      "Could not write save data to disk.\n\n"
      "Progress from this session may be\nlost.  Check the save folder in\nSettings.",
      input,
      true
    );
  }
  return true;
}

} // namespace nba
