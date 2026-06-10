// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dc_config.hh"

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
  show_fps = false;
  allow_large_roms = false;
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
      frame_skip = 0;
      audio_buffer_size = 8192;
      break;

    case PerformanceProfile::Balanced:
      // Native audio mixing with cheap interpolation and no frame skipping.
      audio.mp2k_hle_enable = false;
      audio.interpolation = Config::Audio::Interpolation::Cosine;
      video.lcd_ghosting = false;
      frame_skip = 0;
      audio_buffer_size = 4096;
      break;

    case PerformanceProfile::Speed:
      // HLE audio skips the GBA sound CPU; auto frame skip scales under load
      // and a deeper buffer absorbs the remaining CPU spikes.
      audio.mp2k_hle_enable = true;
      audio.mp2k_hle_cubic = false;
      audio.interpolation = Config::Audio::Interpolation::Cosine;
      video.lcd_ghosting = false;
      skip_bios = true;
      frame_skip = 0;
      auto_frame_skip = true;
      audio_buffer_size = 8192;
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

void DreamcastConfig::LoadDreamcast(std::string const& path) {
  ApplyDefaults();

  if(std::filesystem::exists(path)) {
    Load(path);
    return;
  }

  SaveDreamcast(path);
}

void DreamcastConfig::TryLoadDreamcast(std::string const& path) {
  ApplyDefaults();

  std::string content;
  if(!ReadDreamcastTextFile(path, content)) {
    std::printf("[NBA-DC] Config: using defaults (%s not found or unreadable)\n", path.c_str());
    std::fflush(stdout);
    return;
  }

  if(content.empty()) {
    std::printf("[NBA-DC] Config: empty file at %s, using defaults\n", path.c_str());
    std::fflush(stdout);
    return;
  }

  try {
    LoadFromToml(toml::parse_str(content));
    std::printf("[NBA-DC] Config: loaded %s\n", path.c_str());
    std::fflush(stdout);
  } catch(std::exception const& ex) {
    std::printf("[NBA-DC] Config: parse error in %s (%s), using defaults\n", path.c_str(), ex.what());
    std::fflush(stdout);
    ApplyDefaults();
  }
}

void DreamcastConfig::SaveDreamcast(std::string const& path) {
  const auto path_string = path;
  if(path_string.rfind("/pc/", 0) == 0 || path_string.rfind("/cd/", 0) == 0) {
    SaveDreamcastSafe(path_string);
    return;
  }

  Save(path);
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
    std::printf("[NBA-DC] Config: serialize error for %s (%s)\n", path.c_str(), ex.what());
    std::fflush(stdout);
    return false;
  }

  const bool ok = WriteDreamcastTextFile(path, content);

  std::printf("[NBA-DC] Config: save %s %s (%lu bytes)\n",
              path.c_str(), ok ? "ok" : "failed",
              static_cast<unsigned long>(content.size()));
  std::fflush(stdout);
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
  rom_folder = toml::find_or<std::string>(dreamcast, "rom_folder", rom_folder);
  state_folder = toml::find_or<std::string>(dreamcast, "state_folder", state_folder);
  last_rom = toml::find_or<std::string>(dreamcast, "last_rom", last_rom);
  save_state_slot = toml::find_or<int>(dreamcast, "save_state_slot", save_state_slot);

  frame_skip = std::clamp(frame_skip, 0, 3);
  save_state_slot = std::clamp(save_state_slot, 0, kSaveStateSlotCount - 1);

  if(audio_buffer_size != 2048 && audio_buffer_size != 4096 && audio_buffer_size != 8192) {
    audio_buffer_size = 4096;
  }
}

void DreamcastConfig::SaveCustomData(toml::value& data) {
  data["dreamcast"]["performance_profile"] = ProfileName(performance_profile);
  data["dreamcast"]["frame_skip"] = frame_skip;
  data["dreamcast"]["auto_frame_skip"] = auto_frame_skip;
  data["dreamcast"]["audio_buffer_size"] = audio_buffer_size;
  data["dreamcast"]["show_fps"] = show_fps;
  data["dreamcast"]["allow_large_roms"] = allow_large_roms;
  data["dreamcast"]["rom_folder"] = rom_folder;
  data["dreamcast"]["state_folder"] = state_folder;
  data["dreamcast"]["last_rom"] = last_rom;
  data["dreamcast"]["save_state_slot"] = save_state_slot;
}

} // namespace nba
