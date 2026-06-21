// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hw/timer/timer.hh"

namespace nba::core {

void Timer::LoadState(SaveState const& state) {
  for(int i = 0; i < 4; i++) {
    u16 reload  = state.timer[i].reload;
    u16 control = state.timer[i].control;

    channels[i].reload  = reload;
    channels[i].counter = state.timer[i].counter;

    channels[i].control.frequency = control & 3;
    channels[i].control.cascade = control & 4;
    channels[i].control.interrupt = control & 64;
    channels[i].control.enable = control & 128;

    channels[i].shift = g_ticks_shift[channels[i].control.frequency];
    channels[i].mask = g_ticks_mask[channels[i].control.frequency];

    channels[i].running = false;
    channels[i].event_overflow = scheduler.GetEventByUID(state.timer[i].event_uid);

    channels[i].pending.reload = state.timer[i].pending.reload;
    channels[i].pending.control = state.timer[i].pending.control;
  }
}

void Timer::CopyState(SaveState& state) {
  for(int i = 0; i < 4; i++) {
    state.timer[i].counter = ReadCounter(channels[i]);
    state.timer[i].reload = channels[i].reload;
    state.timer[i].control = ReadControl(channels[i]);
    state.timer[i].pending.reload = channels[i].pending.reload;
    state.timer[i].pending.control = channels[i].pending.control;
    state.timer[i].event_uid = GetEventUID(channels[i].event_overflow);
  }
}

} // namespace nba::core
