// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit test for DCFrameTiming: accumulation, EmuMsPerDisplayFrame,
// OnSecondTick reset, enable/disable gating, and early-exit paths.

#include "dc_frame_timing.hh"

#include <cstdio>

namespace {

int failures = 0;

void expect_near(const char* label, double expected, double actual) {
  if((expected - actual) > 0.0001 || (actual - expected) > 0.0001) {
    std::printf("FAIL %s: expected %.6f, got %.6f\n", label, expected, actual);
    failures++;
  }
}

void expect_eq(const char* label, int expected, int actual) {
  if(expected != actual) {
    std::printf("FAIL %s: expected %d, got %d\n", label, expected, actual);
    failures++;
  }
}

} // namespace

int main() {
  using nba::DCFrameTiming;

  auto& timing = DCFrameTiming::Instance();
  timing.SetEnabled(false);
  timing.ResetInterval();

  // 1 — disabled by default
  expect_eq("default disabled", false, timing.IsEnabled());

  // 2 — zero when no presented frames
  timing.SetEnabled(true);
  expect_near("no frames", 0.0, timing.EmuMsPerDisplayFrame());

  // 3 — single frame, 16 ms emu
  timing.AddEmulatedFrames(2);
  timing.AddPresentedFrames(1);
  timing.AddEmuMicros(std::chrono::microseconds(16'000));
  expect_near("16ms/1frame", 16.0, timing.EmuMsPerDisplayFrame());

  // 4 — OnSecondTick prints and resets counters
  timing.AddEmulatedFrames(1);
  timing.AddPresentedFrames(2);
  timing.AddEmuMicros(std::chrono::microseconds(8'000));
  timing.AddPpuMicros(std::chrono::microseconds(4'000));
  timing.AddConvMicros(std::chrono::microseconds(1'000));
  timing.AddPvrMicros(std::chrono::microseconds(2'000));
  timing.AddPresentMicros(std::chrono::microseconds(500));
  timing.OnSecondTick();
  expect_near("after OnSecondTick", 0.0, timing.EmuMsPerDisplayFrame());

  // 5 — OnSecondTick with no data early-exits (no crash, no output)
  timing.OnSecondTick();

  // 6 — disabled does not accumulate
  timing.SetEnabled(false);
  timing.AddEmuMicros(std::chrono::microseconds(100));
  expect_near("disabled no accumulate", 0.0, timing.EmuMsPerDisplayFrame());

  // 7 — ResetInterval clears counters
  timing.SetEnabled(true);
  timing.AddEmulatedFrames(5);
  timing.AddPresentedFrames(3);
  timing.AddEmuMicros(std::chrono::microseconds(30'000));
  timing.ResetInterval();
  expect_near("after ResetInterval", 0.0, timing.EmuMsPerDisplayFrame());

  // 8 — multiple AddEmuMicros accumulate correctly over several presented frames
  timing.AddEmuMicros(std::chrono::microseconds(10'000));
  timing.AddEmuMicros(std::chrono::microseconds(20'000));
  timing.AddPresentedFrames(3);
  expect_near("accrued 30ms/3frames", 10.0, timing.EmuMsPerDisplayFrame());

  // 9 — enable/disable toggle while data exists
  timing.AddEmuMicros(std::chrono::microseconds(5'000));
  timing.AddPresentedFrames(1);
  timing.SetEnabled(false);
  timing.AddEmuMicros(std::chrono::microseconds(99'999));
  timing.SetEnabled(true);
  // 5,000 more us, 1 more frame → 35,000 / 4 = 8.75 ms
  expect_near("toggle during accumulation", 8.75, timing.EmuMsPerDisplayFrame());

  // 10 — many presented frames, zero emu (should not crash)
  timing.ResetInterval();
  timing.AddPresentedFrames(100);
  expect_near("0 emu / 100 frames", 0.0, timing.EmuMsPerDisplayFrame());

  if(failures == 0) {
    std::printf("[dc-frame-timing] all tests passed\n");
    return 0;
  }
  std::printf("[dc-frame-timing] %d test(s) FAILED\n", failures);
  return 1;
}
