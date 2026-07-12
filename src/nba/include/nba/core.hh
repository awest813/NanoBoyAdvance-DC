// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/rom/gpio/rtc.hh>
#include <nba/rom/gpio/solar_sensor.hh>
#include <nba/rom/rom.hh>
#include <nba/config.hh>
#include <nba/integer.hh>
#include <nba/save_state.hh>
#include <nba/scheduler.hh>
#include <memory>
#include <vector>

namespace nba {

enum class Key : u8 {
  A = 0,
  B = 1,
  Select = 2,
  Start = 3,
  Right = 4,
  Left = 5,
  Up = 6,
  Down = 7,
  R = 8,
  L = 9,
  Count = 10
};

struct CoreBase {
  static constexpr int kCyclesPerFrame = 280896;

  virtual ~CoreBase() = default;

  virtual void Reset() = 0;

  virtual void Attach(std::vector<u8> const& bios) = 0;
  virtual void Attach(ROM&& rom) = 0;
  virtual auto CreateRTC() -> std::unique_ptr<RTC> = 0;
  virtual auto CreateSolarSensor() -> std::unique_ptr<SolarSensor> = 0;
  virtual void LoadState(SaveState const& state) = 0;
  virtual void CopyState(SaveState& state) = 0;
  virtual void SetKeyStatus(Key key, bool pressed) = 0;
  virtual void Run(int cycles) = 0;

  virtual auto GetROM() -> ROM& = 0;
  virtual auto GetPRAM() -> u8* = 0;
  virtual auto GetVRAM() -> u8* = 0;
  virtual auto GetOAM() -> u8* = 0;
  // @todo: come up with a solution for reading write-only registers.
  virtual auto PeekByteIO(u32 address) -> u8  = 0;
  virtual auto PeekHalfIO(u32 address) -> u16 = 0;
  virtual auto PeekWordIO(u32 address) -> u32 = 0;
  virtual auto PeekByte(u32 address) -> u8 = 0;
  virtual auto PeekHalf(u32 address) -> u16 = 0;
  virtual auto PeekWord(u32 address) -> u32 = 0;
  virtual void PokeByte(u32 address, u8 value) = 0;
  virtual void PokeHalf(u32 address, u16 value) = 0;
  virtual void PokeWord(u32 address, u32 value) = 0;
  virtual auto GetBGHOFS(int id) -> u16 = 0;
  virtual auto GetBGVOFS(int id) -> u16 = 0;

  virtual core::Scheduler& GetScheduler() = 0;

  // Dynarec block-cache hit/miss/invalidation counters since the previous take.
  // Default stub returns zeros (host tools that do not build a Core).
  struct DynarecTelemetry {
    u64 hits = 0;
    u64 misses = 0;
    u32 invalidations = 0;
    int cache_blocks = 0;
  };

  virtual auto TakeDynarecTelemetry() -> DynarecTelemetry {
    return {};
  }

  void RunForOneFrame() {
    Run(kCyclesPerFrame);
  }

  // Run one presented frame plus `skipped_frames` suppressed emulated frames.
  template<typename Callback>
  void RunForDisplayFrame(Config& config, int skipped_frames, Callback&& per_frame) {
    const int skip = skipped_frames < 0 ? 0 : skipped_frames;

    for(int index = 0; index < skip; index++) {
      config.suppress_video_draw = true;
      per_frame();
      Run(kCyclesPerFrame);
    }

    config.suppress_video_draw = false;
    per_frame();
    Run(kCyclesPerFrame);
  }
};

auto CreateCore(
  std::shared_ptr<Config> config
) -> std::unique_ptr<CoreBase>;

} // namespace nba
