// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <functional>

namespace nba {

class FrameLimiter {
  public:
    explicit FrameLimiter(float fps = 60.f) {
      Reset(fps);
    }

    void Reset();
    void Reset(float fps);
    // Drop the current pacing window. Call this after a blocking pause so the
    // time spent in the menu is not treated as a late frame (which both
    // collapses the FPS sample and makes the limiter run uncapped until it
    // catches up).
    void Resync();
    bool GetFastForward() const;
    void SetFastForward(bool value);
    void SetFastForwardSpeed(int speed);

    void Run(
      std::function<void(void)> frame_advance,
      std::function<void(float)> update_fps
    );

  private:
    static constexpr int k_milliseconds_per_second = 1000;
    static constexpr int k_microseconds_per_second = 1000000;

    int m_frame_count{0};
    int m_frame_duration{};
    float m_frames_per_second{};
    bool m_fast_forward{false};
    int m_fast_forward_speed{0};

    std::chrono::time_point<std::chrono::steady_clock> m_timestamp_target{};
    std::chrono::time_point<std::chrono::steady_clock> m_timestamp_fps_update{};
};

} // namespace nba
