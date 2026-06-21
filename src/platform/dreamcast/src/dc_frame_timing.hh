// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dc_log.hh"

#include <chrono>
#include <cstdlib>

namespace nba {

// Per-display-frame segment accumulators for PPU/GPU profiling on Dreamcast.
// Enable with NBA_DC_FRAME_TIMING=1 or when Show FPS is on during gameplay.
class DCFrameTiming {
public:
  static auto Instance() -> DCFrameTiming& {
    static DCFrameTiming timing;
    return timing;
  }

  void SetEnabled(bool value) {
    enabled_ = value;
  }

  auto IsEnabled() const -> bool {
    return enabled_;
  }

  static auto EnabledFromEnvironment() -> bool {
    const char* value = std::getenv("NBA_DC_FRAME_TIMING");
    return value && value[0] == '1';
  }

  void AddPpuMicros(std::chrono::microseconds duration) {
    if(!enabled_) return;
    ppu_us_ += static_cast<u64>(duration.count());
  }

  void AddConvMicros(std::chrono::microseconds duration) {
    if(!enabled_) return;
    conv_us_ += static_cast<u64>(duration.count());
  }

  void AddPvrMicros(std::chrono::microseconds duration) {
    if(!enabled_) return;
    pvr_us_ += static_cast<u64>(duration.count());
  }

  void AddPresentMicros(std::chrono::microseconds duration) {
    if(!enabled_) return;
    present_us_ += static_cast<u64>(duration.count());
  }

  void AddEmuMicros(std::chrono::microseconds duration) {
    if(!enabled_) return;
    emu_us_ += static_cast<u64>(duration.count());
  }

  void AddEmulatedFrames(int count) {
    if(!enabled_) return;
    emulated_frames_ += count;
  }

  void AddPresentedFrames(int count = 1) {
    if(!enabled_) return;
    presented_frames_ += count;
  }

  auto EmuMsPerDisplayFrame() const -> double {
    if(presented_frames_ == 0) {
      return 0.0;
    }

    return static_cast<double>(emu_us_) / 1000.0 / static_cast<double>(presented_frames_);
  }

  void OnSecondTick() {
    if(!enabled_) {
      return;
    }

    if(presented_frames_ == 0 && emulated_frames_ == 0) {
      return;
    }

    DCLog(
      "[NBA-DC] Frame timing: PPU %4.1fms CONV %4.1fms PVR %4.1fms PRESENT %4.1fms "
      "EMU %4.1fms (%d emu / %d display frames)\n",
      ppu_us_ / 1000.0,
      conv_us_ / 1000.0,
      pvr_us_ / 1000.0,
      present_us_ / 1000.0,
      emu_us_ / 1000.0,
      emulated_frames_,
      presented_frames_
    );
    ResetInterval();
  }

  void ResetInterval() {
    ppu_us_ = 0;
    conv_us_ = 0;
    pvr_us_ = 0;
    present_us_ = 0;
    emu_us_ = 0;
    emulated_frames_ = 0;
    presented_frames_ = 0;
  }

private:
  using u64 = unsigned long long;

  bool enabled_ = false;
  u64 ppu_us_ = 0;
  u64 conv_us_ = 0;
  u64 pvr_us_ = 0;
  u64 present_us_ = 0;
  u64 emu_us_ = 0;
  int emulated_frames_ = 0;
  int presented_frames_ = 0;
};

class DCFrameTimingScope {
public:
  explicit DCFrameTimingScope(
    DCFrameTiming* timing,
    void (DCFrameTiming::*adder)(std::chrono::microseconds)
  )
    : timing_(timing)
    , adder_(adder)
    , start_(std::chrono::steady_clock::now()) {}

  ~DCFrameTimingScope() {
    if(!timing_ || !timing_->IsEnabled()) {
      return;
    }

    const auto end = std::chrono::steady_clock::now();
    (timing_->*adder_)(std::chrono::duration_cast<std::chrono::microseconds>(end - start_));
  }

private:
  DCFrameTiming* timing_;
  void (DCFrameTiming::*adder_)(std::chrono::microseconds);
  std::chrono::steady_clock::time_point start_;
};

#if defined(PLATFORM_DREAMCAST)
#define NBA_DC_FRAME_TIMING_SCOPE(field) \
  ::nba::DCFrameTimingScope _nba_dc_frame_timing_scope_##field( \
    &::nba::DCFrameTiming::Instance(), \
    &::nba::DCFrameTiming::Add##field##Micros \
  )
#else
#define NBA_DC_FRAME_TIMING_SCOPE(field)
#endif

} // namespace nba
