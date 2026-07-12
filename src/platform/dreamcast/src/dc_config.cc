// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_config.hh"
#include "dc_log.hh"

#include <platform/loader/dc_virtual_fs.hh>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace nba {

void DreamcastConfig::ApplyDefaults() {
  bios_path = kDefaultBIOSPath;
  save_folder = kDefaultSaveFolder;
  state_folder = kDefaultStateFolder;
  rom_folder = kDefaultROMFolder;
  save_state_slot = 0;
  video.filter = PlatformConfig::Video::Filter::Nearest;
  video.color = PlatformConfig::Video::Color::No;
  video_rgb565_output = true;
  show_fps = false;
  allow_large_roms = false;
  pvr_dma_upload = true;
  auto_frame_skip = false;

  // The Balanced profile seeds the CPU/audio/video knobs (mp2k HLE,
  // interpolation, frame skip, audio buffer, LCD ghosting).
  ApplyPerformanceProfile(PerformanceProfile::Balanced);
}

void DreamcastConfig::ApplyPerformanceProfile(PerformanceProfile profile) {
  performance_profile = profile;
  auto_frame_skip = false;

  switch(profile) {
    case PerformanceProfile::Accuracy:
      // Cycle-accurate audio mixing and high-order interpolation. No frame
      // skipping; a larger audio buffer absorbs the heavier CPU load.
      audio.mp2k_hle_enable = false;
      audio.interpolation = Config::Audio::Interpolation::Sinc_64;
      video.lcd_ghosting = true;
      ppu_fast_mode = false;
      skip_bios = false;
      frame_skip = 0;
      audio_buffer_size = 8192;
      break;

    case PerformanceProfile::Balanced:
      // Native audio mixing with cheap interpolation and no frame skipping.
      audio.mp2k_hle_enable = false;
      audio.interpolation = Config::Audio::Interpolation::Cosine;
      video.lcd_ghosting = false;
      ppu_fast_mode = false;
      skip_bios = false;
      frame_skip = 0;
      audio_buffer_size = 4096;
      break;

    case PerformanceProfile::Speed:
      // HLE audio skips the GBA sound CPU; auto frame skip scales under load
      // and a deeper buffer absorbs the remaining CPU spikes.
      audio.mp2k_hle_enable = true;
      audio.mp2k_hle_cubic = false;
      audio.mp2k_hle_force_reverb = false;
      audio.interpolation = Config::Audio::Interpolation::Cosine;
      video.lcd_ghosting = false;
      ppu_fast_mode = true;
      skip_bios = true;
      frame_skip = 0;
      auto_frame_skip = true;
      audio_buffer_size = 8192;
      // A/B: only enable dynarec when explicitly opted in via
      // cpu_dynarec_on_speed (settings / toml) — never the silent default.
      if(cpu_dynarec_on_speed) {
        cpu_dynarec = true;
      }
      break;
  }
}

auto DreamcastConfig::ProfileName(PerformanceProfile profile) -> const char* {
  switch(profile) {
    case PerformanceProfile::Accuracy: return "Accuracy";
    case PerformanceProfile::Speed:    return "Speed";
    case PerformanceProfile::Balanced:
    default:                           return "Balanced";
  }
}

auto DreamcastConfig::ProfileFromName(std::string const& name, PerformanceProfile fallback)
  -> PerformanceProfile {
  if(name == "Accuracy") return PerformanceProfile::Accuracy;
  if(name == "Balanced") return PerformanceProfile::Balanced;
  if(name == "Speed")    return PerformanceProfile::Speed;
  return fallback;
}

auto DreamcastConfig::TryLoadDreamcast(std::string const& path) -> ConfigLoadResult {
  ApplyDefaults();

  std::string content;
  if(!ReadDreamcastTextFile(path, content)) {
    DCLog("[NBA-DC] Config: using defaults (%s not found or unreadable)\n", path.c_str());
    return ConfigLoadResult::UsingDefaults;
  }

  if(content.empty()) {
    DCLog("[NBA-DC] Config: empty file at %s, using defaults\n", path.c_str());
    return ConfigLoadResult::EmptyFile;
  }

  try {
    LoadFromToml(toml::parse_str(content));
    DCLog("[NBA-DC] Config: loaded %s\n", path.c_str());
    return ConfigLoadResult::Loaded;
  } catch(std::exception const& ex) {
    DCLog("[NBA-DC] Config: parse error in %s (%s), using defaults\n", path.c_str(), ex.what());
    ApplyDefaults();
    return ConfigLoadResult::ParseError;
  }
}

auto DreamcastConfig::SaveDreamcastSafe(std::string const& path) -> bool {
  std::string content;
  try {
    toml::value data;
    SaveToData(data);
    std::ostringstream stream;
    stream << data;
    content = stream.str();
  } catch(std::exception const& ex) {
    DCLog("[NBA-DC] Config: serialize error for %s (%s)\n", path.c_str(), ex.what());
    return false;
  }

  const bool ok = WriteDreamcastTextFile(path, content);

  DCLog("[NBA-DC] Config: save %s %s (%lu bytes)\n",
        path.c_str(), ok ? "ok" : "failed",
        static_cast<unsigned long>(content.size()));
  return ok;
}

void DreamcastConfig::LoadCustomData(toml::value const& data) {
  if(!data.contains("dreamcast")) {
    return;
  }

  auto dreamcast = data.at("dreamcast");
  performance_profile = ProfileFromName(
    toml::find_or<std::string>(dreamcast, "performance_profile", ProfileName(performance_profile)),
    performance_profile
  );
  frame_skip = toml::find_or<int>(dreamcast, "frame_skip", frame_skip);
  auto_frame_skip = toml::find_or<bool>(dreamcast, "auto_frame_skip", auto_frame_skip);
  audio_buffer_size = toml::find_or<int>(dreamcast, "audio_buffer_size", audio_buffer_size);
  show_fps = toml::find_or<bool>(dreamcast, "show_fps", show_fps);
  allow_large_roms = toml::find_or<bool>(dreamcast, "allow_large_roms", allow_large_roms);
  pvr_dma_upload = toml::find_or<bool>(dreamcast, "pvr_dma_upload", pvr_dma_upload);
  cpu_dynarec = toml::find_or<bool>(dreamcast, "cpu_dynarec", cpu_dynarec);
  cpu_dynarec_on_speed = toml::find_or<bool>(dreamcast, "cpu_dynarec_on_speed", cpu_dynarec_on_speed);
  rom_folder = toml::find_or<std::string>(dreamcast, "rom_folder", rom_folder);
  state_folder = toml::find_or<std::string>(dreamcast, "state_folder", state_folder);
  last_rom = toml::find_or<std::string>(dreamcast, "last_rom", last_rom);
  save_state_slot = toml::find_or<int>(dreamcast, "save_state_slot", save_state_slot);

  frame_skip = std::clamp(frame_skip, 0, 3);
  save_state_slot = std::clamp(save_state_slot, 0, kSaveStateSlotCount - 1);

  if(audio_buffer_size != 2048 && audio_buffer_size != 4096 && audio_buffer_size != 8192) {
    audio_buffer_size = 4096;
  }

  // Re-apply Speed A/B opt-in after loading the profile name + flag.
  if(performance_profile == PerformanceProfile::Speed && cpu_dynarec_on_speed) {
    cpu_dynarec = true;
  }
}

void DreamcastConfig::SaveCustomData(toml::value& data) {
  data["dreamcast"]["performance_profile"] = ProfileName(performance_profile);
  data["dreamcast"]["frame_skip"] = frame_skip;
  data["dreamcast"]["auto_frame_skip"] = auto_frame_skip;
  data["dreamcast"]["audio_buffer_size"] = audio_buffer_size;
  data["dreamcast"]["show_fps"] = show_fps;
  data["dreamcast"]["allow_large_roms"] = allow_large_roms;
  data["dreamcast"]["pvr_dma_upload"] = pvr_dma_upload;
  data["dreamcast"]["cpu_dynarec"] = cpu_dynarec;
  data["dreamcast"]["cpu_dynarec_on_speed"] = cpu_dynarec_on_speed;
  data["dreamcast"]["rom_folder"] = rom_folder;
  data["dreamcast"]["state_folder"] = state_folder;
  data["dreamcast"]["last_rom"] = last_rom;
  data["dreamcast"]["save_state_slot"] = save_state_slot;
}

} // namespace nba
