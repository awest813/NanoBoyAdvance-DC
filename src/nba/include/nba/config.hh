// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/device/audio_device.hh>
#include <nba/device/video_device.hh>
#include <nba/integer.hh>
#include <memory>
#include <string>

namespace nba {

struct Config {
  bool skip_bios = false;

  // gpSP-style idle loop address (game_config.txt). When non-zero and the CPU PC
  // matches, the core halts until the next IRQ instead of spinning the loop.
  u32 idle_loop_eliminate_target = 0;

  // Experimental ARM Thumb dynarec (Dreamcast). When true, Core::Run tries
  // compiled IR/native blocks before falling back to the interpreter.
  bool cpu_dynarec = false;

  // When true, selecting the Dreamcast Speed performance profile also enables
  // cpu_dynarec (A/B testing). Default off — Speed does not silently opt in.
  bool cpu_dynarec_on_speed = false;

  // When true, the PPU keeps timing/IRQ/DMA state but skips scanline pixel
  // compositing and the final video-device blit.  Raster timestamps and affine
  // scroll still advance each scanline.  The Dreamcast frontend sets this on
  // non-final frame-skip iterations so skipped frames avoid PPU rasterization
  // and PVR texture conversion.
  bool suppress_video_draw = false;

  // When true, the PPU writes RGB565 pixels directly and calls
  // VideoDevice::DrawRgb565 instead of expanding to RGBA8888 first.
  bool video_rgb565_output = false;

  // When true, the PPU may use scanline-batch fast paths when display state is
  // simple (no windows, blend SFX, or mosaic).  Enabled on Dreamcast Speed.
  bool ppu_fast_mode = false;

#if defined(PLATFORM_DREAMCAST)
  using DcPpuTimingCallback = void (*)(long long microseconds);
  DcPpuTimingCallback dc_ppu_timing_callback = nullptr;

  enum class DcMergePath { Slow, Text, Bitmap };
  using DcMergePathCallback = void (*)(DcMergePath path);
  DcMergePathCallback dc_merge_path_callback = nullptr;
#endif

  enum class BackupType {
    Detect,
    None,
    SRAM,
    FLASH_64,
    FLASH_128,
    EEPROM_4,
    EEPROM_64,
    EEPROM_DETECT // for internal use
  };

  struct Audio {
    enum class Interpolation {
      Cosine,
      Cubic,
      Sinc_64,
      Sinc_128,
      Sinc_256
    } interpolation = Interpolation::Cubic;

    int volume = 100; // between 0 and 100
    bool mp2k_hle_enable = false;
    bool mp2k_hle_cubic = true;
    bool mp2k_hle_force_reverb = true;
  } audio;

  std::shared_ptr<AudioDevice> audio_dev = std::make_shared<NullAudioDevice>();
  std::shared_ptr<VideoDevice> video_dev = std::make_shared<NullVideoDevice>();
};

} // namespace nba

namespace std {

using BackupType = nba::Config::BackupType;

inline auto to_string(BackupType value) -> std::string {
  switch(value) {
    case BackupType::Detect: return "Detect";
    case BackupType::None: return "None";
    case BackupType::SRAM: return "SRAM";
    case BackupType::FLASH_64: return "FLASH_64";
    case BackupType::FLASH_128: return "FLASH_128";
    case BackupType::EEPROM_4: return "EEPROM_4";
    default: return "EEPROM_64";
  }
}

} // namespace std
