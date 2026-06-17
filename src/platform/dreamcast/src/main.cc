// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Dreamcast frontend for NanoBoyAdvance.

#include <nba/core.hh>
#include <platform/loader/bios.hh>
#include <platform/loader/rom.hh>
#include <platform/frame_limiter.hh>

#include "dc_cheats.hh"
#include "dc_config.hh"
#include "dc_frontend.hh"
#include "dc_gameplay_menu.hh"
#include "dc_memory.hh"
#include "dc_paths.hh"
#include "dc_rom_browser.hh"
#include "dc_ui.hh"
#include "dc_frame_timing.hh"
#include "device/dc_video_device.hh"
#include "device/dc_audio_device.hh"
#include "device/dc_input.hh"
#include "open_bios.hh"
#include "version.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
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

namespace {

auto BuildBootInfoLine() -> std::string {
  std::string line = "v";
  line += VERSION_STRING;

  if(VERSION_GIT_HASH[0] != '\0') {
    line += " (";
    line += VERSION_GIT_HASH;
    line += ')';
  }

#if NBA_DC_HAS_KOS
  line += "  |  ";
  line += HasExtendedRAM() ? "32 MB RAM" : "16 MB RAM";
#endif

  return line;
}

} // namespace

auto BIOSLoader::LoadEmbedded(std::unique_ptr<CoreBase>& core) -> Result {
  std::vector<u8> file_data(kOpenBIOS, kOpenBIOS + 16384);
  core->Attach(file_data);
  return Result::Success;
}

static constexpr float kGBAFrameRate =
  static_cast<float>(16777216) / static_cast<float>(280896);
static constexpr size_t kMaxGBAROMSize = 32 * 1024 * 1024;
// Launch breadcrumbs always log to stdout for Flycast diagnosis.  Full-screen
// holds are disabled in normal builds because they add several seconds of boot
// delay and can mask timing-sensitive launch failures.
static constexpr bool kDreamcastLaunchScreenBreadcrumbs = false;
static constexpr bool kDreamcastAutobootTekken = false;
static constexpr char kDreamcastAutobootROM[] = "/cd/tekken.gba";
static constexpr char kDreamcastAutobootROMFallback[] = "/cd/Tekken.gba";

static auto GetROMSourceName(fs::path const& path) -> const char* {
  const auto path_string = path.string();
  if(path_string.rfind("/cd/", 0) == 0) {
    return "CD root";
  }

  if(path_string.rfind("/pc/roms/", 0) == 0) {
    return "PC ROMs";
  }

  if(path_string.rfind("/pc/", 0) == 0) {
    return "PC";
  }

  return "Unknown";
}

static auto ParsePositiveEnvInt(char const* name) -> int {
  if(const char* value = std::getenv(name)) {
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if(end != value && parsed > 0) {
      return static_cast<int>(parsed);
    }
  }
  return 0;
}

static auto FormatROMSize(size_t size) -> std::string {
  char message[48];
  std::snprintf(
    message,
    sizeof(message),
    "%lu bytes (%lu MiB)",
    static_cast<unsigned long>(size),
    static_cast<unsigned long>(size / (1024 * 1024))
  );
  return message;
}

static auto UpdateAutoFrameSkip(
  DreamcastConfig& config,
  float measured_fps,
  int current_frame_skip,
  int& recovery_ticks,
  double emu_ms_per_display_frame = 0.0
) -> int {
  if(!config.auto_frame_skip || measured_fps <= 0.0f) {
    recovery_ticks = 0;
    return config.frame_skip;
  }

  const bool speed_profile =
    config.performance_profile == DreamcastConfig::PerformanceProfile::Speed;
  const float raise_threshold = speed_profile ? 56.0f : 55.0f;
  const float lower_threshold = speed_profile ? 58.5f : 57.5f;
  const int recovery_required = speed_profile ? 2 : 3;
  const int emulated_fps = static_cast<int>(
    measured_fps * static_cast<float>(current_frame_skip + 1) + 0.5f
  );

  int next_frame_skip = current_frame_skip;
  if(emulated_fps > 62 && current_frame_skip > 0) {
    next_frame_skip--;
    recovery_ticks = 0;
  } else if(
    speed_profile &&
    emu_ms_per_display_frame > 0.0 &&
    emu_ms_per_display_frame < 14.5 &&
    current_frame_skip > 0
  ) {
    next_frame_skip--;
    recovery_ticks = 0;
  } else if(measured_fps < raise_threshold && current_frame_skip < 3) {
    next_frame_skip++;
    recovery_ticks = 0;
  } else if(measured_fps > lower_threshold && current_frame_skip > 0) {
    recovery_ticks++;
    if(recovery_ticks >= recovery_required) {
      next_frame_skip--;
      recovery_ticks = 0;
    }
  } else {
    recovery_ticks = 0;
  }

  if(next_frame_skip != current_frame_skip) {
    std::printf(
      "[NBA-DC] Auto frame skip: %d -> %d (FPS %.1f)\n",
      current_frame_skip,
      next_frame_skip,
      static_cast<double>(measured_fps)
    );
    std::fflush(stdout);
  }

  config.frame_skip = std::clamp(next_frame_skip, 0, 3);
  return config.frame_skip;
}

static auto ResolveAutobootROMPath(DreamcastConfig const& config, std::string& report) -> fs::path {
  report.clear();

  auto probe = [&](fs::path const& candidate, char const* label) -> bool {
    const auto result = ROMLoader::Validate(candidate);
    char line[192];
    std::snprintf(
      line,
      sizeof(line),
      "%s: %s -> %s\n",
      label,
      candidate.string().c_str(),
      ROMLoader::Describe(result)
    );
    report += line;
    std::printf("[NBA-DC] Autoboot probe %s", line);
    std::fflush(stdout);
    return result == ROMLoader::Result::Success;
  };

  fs::path lower{kDreamcastAutobootROM};
  if(probe(lower, "lower")) {
    return lower;
  }

  fs::path mixed{kDreamcastAutobootROMFallback};
  if(probe(mixed, "mixed")) {
    return mixed;
  }

  fs::path upper{"/cd/TEKKEN.GBA"};
  if(probe(upper, "upper")) {
    return upper;
  }

  fs::path upper_version{"/cd/TEKKEN.GBA;1"};
  if(probe(upper_version, "upper;1")) {
    return upper_version;
  }

  auto entries = ROMBrowser::Scan(config);
  char count_line[80];
  std::snprintf(
    count_line,
    sizeof(count_line),
    "scan entries: %lu\n",
    static_cast<unsigned long>(entries.size())
  );
  report += count_line;
  std::printf("[NBA-DC] Autoboot %s", count_line);
  std::fflush(stdout);

  for(auto const& entry : entries) {
    char line[256];
    std::snprintf(
      line,
      sizeof(line),
      "scan: %s | %s | %lu bytes\n",
      entry.path.string().c_str(),
      entry.label.c_str(),
      static_cast<unsigned long>(entry.size)
    );
    report += line;
    std::printf("[NBA-DC] Autoboot %s", line);
    std::fflush(stdout);

    if(entry.path.string().rfind("/cd/", 0) == 0) {
      return entry.path;
    }
  }

  return lower;
}

static void HoldDebugBreadcrumbFrames(int frames) {
#if NBA_DC_HAS_KOS
  for(int i = 0; i < frames; i++) {
    vid_waitvbl();
  }
#else
  (void)frames;
#endif
}

static auto FormatGameplayOverlay(std::string const& message) -> std::string {
  std::string line = message;
  for(auto& character : line) {
    if(character == '\n') {
      character = ' ';
    }
  }
  return line;
}

static auto LoadEmulator(
  DCUI& ui,
  DCInput& input,
  std::shared_ptr<DreamcastConfig>& config,
  std::shared_ptr<DCVideoDevice>& video_device,
  fs::path const& rom_path
) -> bool {
  const auto bios_path = config->bios_path;

  auto breadcrumb = [&](const char* phase, std::string const& detail = {}) {
    std::printf("[NBA-DC] %s", phase);
    if(!detail.empty()) {
      std::printf(": %s", detail.c_str());
    }
    std::printf("\n");
    std::fflush(stdout);

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

  // Save directory may not be writable on FlyCast; continue anyway

  const auto save_path = GetSavePath(*config, rom_path);

  // Ensure the save directory exists before creating backup files.
  // EnsureDirectoryPOSIX uses POSIX mkdir which works through the KOS
  // virtual filesystem on both real hardware and Flycast.
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
            std::printf(
              "[NBA-DC] Smoke test: ran %d frame(s), exiting\n",
              frames_run
            );
            std::fflush(stdout);
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
          measured_fps = fps_frame_count * 1000.0f / static_cast<float>(elapsed_ms);
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
          fps_frame_count = 0;
          fps_last_update = now;
          const int emulated_fps = static_cast<int>(
            measured_fps * static_cast<float>(active_frame_skip + 1) + 0.5f
          );
          std::printf(
            "[NBA-DC] Runtime: FPS %.1f EF %d FS %s%d PG %lu\n",
            static_cast<double>(measured_fps),
            emulated_fps,
            config->auto_frame_skip ? "A" : "",
            active_frame_skip,
            static_cast<unsigned long>(measured_page_misses)
          );
          std::fflush(stdout);
        }
      }
#else
    }, [&](float fps) {
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
      std::printf(
        "[NBA-DC] Runtime: FPS %.1f EF %d FS %s%d PG %lu\n",
        static_cast<double>(measured_fps),
        emulated_fps,
        config->auto_frame_skip ? "A" : "",
        active_frame_skip,
        static_cast<unsigned long>(measured_page_misses)
      );
      std::fflush(stdout);
    });
#endif

#if NBA_DC_HAS_KOS
    snd_stream_poll(SND_STREAM_INVALID);

    // Present the PVR-scaled gameplay frame first, then draw overlays into the
    // letterbox margins so they are not cleared by the next scene render.
    video_device->Present();

    if(config->show_fps) {
      // Fixed width keeps stale digits from lingering as the value changes.
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

  // Persist saves before tearing the core down.  On read-only media (e.g.
  // Flycast's /pc/ stream) backup writes never reached disk during play; make
  // one clean attempt now and tell the user whether their progress survived.
  bool save_in_memory_only = false;
  bool save_flush_ok = true;
  if(!rom_read_failed && core->GetROM().HasBackup()) {
    save_in_memory_only = !core->GetROM().IsBackupPersistent();
    save_flush_ok = core->GetROM().FlushBackup();
    std::printf(
      "[NBA-DC] Save flush: in_memory_only=%d flush_ok=%d\n",
      save_in_memory_only ? 1 : 0,
      save_flush_ok ? 1 : 0
    );
    std::fflush(stdout);
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

static auto RunLoadEmulator(
  DCUI& ui,
  DCInput& input,
  std::shared_ptr<DreamcastConfig>& config,
  std::shared_ptr<DCVideoDevice>& video_device,
  fs::path const& rom_path
) -> bool {
  return LoadEmulator(ui, input, config, video_device, rom_path);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  std::printf("NanoBoyAdvance Dreamcast Edition\n");

  auto video_device = std::make_shared<DCVideoDevice>();
  if(!video_device->Initialize()) {
    std::printf("Error: failed to initialize video device.\n");
    return 1;
  }

  DCUI ui{*video_device};

  ui.DrawSplash("Dreamcast Edition", BuildBootInfoLine(), "Loading settings...");

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

  if(const char* autoboot_env = std::getenv("NBA_DC_AUTOBOOT_ROM")) {
    std::printf("[NBA-DC] Autoboot env ROM: %s\n", autoboot_env);
    std::fflush(stdout);
    const bool loaded = RunLoadEmulator(
      ui,
      input,
      config,
      video_device,
      fs::path{autoboot_env}
    );
    return loaded ? 0 : 1;
  }

  if(kDreamcastAutobootTekken) {
    std::string autoboot_report;
    const auto autoboot_path = ResolveAutobootROMPath(*config, autoboot_report);
    std::printf("[NBA-DC] Autoboot ROM: %s\n", autoboot_path.string().c_str());
    std::fflush(stdout);

    ui.ClearScreen();
    ui.DrawTitle("Autoboot");
    ui.DrawTextMultiline(
      48,
      88,
      std::string{"AUTOBOOT\n"} + autoboot_path.string() + "\n\n" + autoboot_report
    );
    ui.DrawStatusBar("CDI-only Tekken stability test");
    ui.Present();
    HoldDebugBreadcrumbFrames(120);

    const bool loaded = RunLoadEmulator(ui, input, config, video_device, autoboot_path);
    ui.ShowMessage(
      loaded ? "Autoboot ended" : "Autoboot failed",
      loaded ? "Session returned from emulator." : "LoadEmulator returned false after diagnostics.",
      input,
      true
    );
    return 0;
  }

  while(true) {
    std::printf("[NBA-DC] Frontend: scanning ROMs\n");
    std::fflush(stdout);

    char scan_status[80];
    std::snprintf(
      scan_status,
      sizeof(scan_status),
      "Scanning %s, /cd, /cd/gbaDC",
      config->rom_folder.c_str()
    );
    ui.DrawSplash("Loading library...", {}, scan_status);

    auto entries = ROMBrowser::Scan(*config);

    std::printf(
      "[NBA-DC] Frontend: found %lu ROM%s\n",
      static_cast<unsigned long>(entries.size()),
      entries.size() == 1 ? "" : "s"
    );
    std::fflush(stdout);

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
          banner,
          sizeof(banner),
          "%zu ROM%s found (%d need Large ROMs)",
          rom_count,
          rom_count == 1 ? "" : "s",
          unavailable
        );
      } else {
        std::snprintf(
          banner,
          sizeof(banner),
          "%zu ROM%s found",
          rom_count,
          rom_count == 1 ? "" : "s"
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
      // Persist settings (notably last_rom) at this quiet point with the
      // guarded one-shot writer, so the choice survives a reboot without
      // risking a filesystem hang mid-launch.
      if(!config->SaveDreamcastSafe(DreamcastConfig::kDefaultConfigPath)) {
        ui.ShowMessage(
          "Settings Not Saved",
          "Could not write /pc/nba-dc.toml.\nLaunching anyway.",
          input,
          true
        );
      }
      RunLoadEmulator(ui, input, config, video_device, frontend_result.rom_path);
      config->SaveDreamcastSafe(DreamcastConfig::kDefaultConfigPath);
      ui.ShowBriefBanner("Session ended", "Returning to ROM browser...", input);
    }
  }

  return 0;
}
