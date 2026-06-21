// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <platform/config.hh>
#include <toml.hpp>
#include <string>

namespace nba {

enum class ConfigLoadResult {
  UsingDefaults, // Config file missing or unreadable; defaults applied.
  Loaded,        // Settings loaded successfully.
  EmptyFile,     // Config file exists but is empty.
  ParseError     // Config file exists but could not be parsed.
};

struct DreamcastConfig : PlatformConfig {
  static constexpr const char* kDefaultConfigPath = "/pc/nba-dc.toml";
  static constexpr const char* kDefaultBIOSPath = "/cd/bios.bin";
  static constexpr const char* kDefaultROMFolder = "/pc/roms";
  static constexpr const char* kDefaultSaveFolder = "/pc/saves";
  static constexpr const char* kDefaultVMUSaveFolder = "/vmu/a1";
  static constexpr const char* kDefaultStateFolder = "/pc/states";
  static constexpr int kSaveStateSlotCount = 10;

  // Performance profiles trade emulation accuracy for speed on the
  // fixed Dreamcast hardware budget. Selecting a profile applies a
  // coherent preset of the CPU/audio/video knobs below.
  enum class PerformanceProfile {
    Accuracy, // Highest fidelity, may not hold full speed on heavy games.
    Balanced, // Default. Good fidelity with headroom on most games.
    Speed     // Favors full speed: HLE audio, light interpolation, frame skip.
  };

  int frame_skip = 0;
  bool auto_frame_skip = false;
  int audio_buffer_size = 4096;
  bool show_fps = false;
  bool allow_large_roms = false;
  // Use asynchronous TA DMA for the PVR texture upload (vs. the blocking
  // store-queue copy). Exposed as a setting for on-device A/B testing.
  bool pvr_dma_upload = true;
  PerformanceProfile performance_profile = PerformanceProfile::Balanced;
  std::string rom_folder = kDefaultROMFolder;
  std::string state_folder = kDefaultStateFolder;
  std::string last_rom;
  int save_state_slot = 0;

  void ApplyDefaults();
  void ApplyPerformanceProfile(PerformanceProfile profile);
  auto TryLoadDreamcast(std::string const& path) -> ConfigLoadResult;
  auto SaveDreamcastSafe(std::string const& path) -> bool;

  static auto ProfileName(PerformanceProfile profile) -> const char*;
  static auto ProfileFromName(std::string const& name, PerformanceProfile fallback)
    -> PerformanceProfile;

protected:
  void LoadCustomData(toml::value const& data) override;
  void SaveCustomData(toml::value& data) override;
};

} // namespace nba
