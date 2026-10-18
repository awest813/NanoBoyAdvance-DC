// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/dc_audio_device.hh"

#include "dc_log.hh"

#include <algorithm>
#include <cstring>

namespace nba {

#if NBA_DC_HAS_KOS
DCAudioDevice* DCAudioDevice::s_instance = nullptr;
#endif

void DCAudioDevice::SetBufferSize(int bytes) {
  if(bytes == 2048 || bytes == 4096 || bytes == 8192) {
    buffer_size_ = bytes;
  } else {
    DCLog("[NBA-DC] Audio: ignoring invalid buffer size %d (valid: 2048, 4096, 8192)\n", bytes);
  }
}

bool DCAudioDevice::Open(void* userdata, Callback callback) {
  callback_ = callback;
  userdata_ = userdata;
  paused_ = false;

#if NBA_DC_HAS_KOS
  s_instance = this;

  snd_stream_init();
  stream_handle_ = snd_stream_alloc(StreamCallback, buffer_size_);

  if(stream_handle_ == SND_STREAM_INVALID) {
    return false;
  }

  snd_stream_start(stream_handle_, 44100, 1); // 1 = stereo
  opened_ = true;
  return true;
#else
  opened_ = true;
  return true;
#endif
}

void DCAudioDevice::Close() {
#if NBA_DC_HAS_KOS
  if(stream_handle_ != SND_STREAM_INVALID) {
    snd_stream_stop(stream_handle_);
    snd_stream_destroy(stream_handle_);
    stream_handle_ = SND_STREAM_INVALID;
  }
  snd_stream_shutdown();
  s_instance = nullptr;
#endif
  opened_ = false;
}

void DCAudioDevice::Reset() {
#if NBA_DC_HAS_KOS
  if(stream_handle_ != SND_STREAM_INVALID) {
    snd_stream_stop(stream_handle_);
    // A reset during the pause menu must not resume sound. SetPause(false)
    // starts the stream again when gameplay continues.
    if(!paused_) {
      snd_stream_start(stream_handle_, 44100, 1);
    }
  }
#endif
}

auto DCAudioDevice::GetSampleRate() -> int {
  return 44100;
}

void DCAudioDevice::SetPause(bool value) {
  paused_ = value;
#if NBA_DC_HAS_KOS
  if(stream_handle_ == SND_STREAM_INVALID) return;

  if(value) {
    snd_stream_stop(stream_handle_);
  } else {
    snd_stream_start(stream_handle_, 44100, 1);
  }
#endif
}

#if NBA_DC_HAS_KOS
void* DCAudioDevice::StreamCallback(snd_stream_hnd_t hnd, int req, int* done) {
  (void)hnd;

  // The callback contract is a byte count. The static buffer is stereo s16,
  // so the largest fill is its byte size, rounded down to a whole frame.
  static s16 stream_buffer[8192];
  int request = std::min(req, static_cast<int>(sizeof(stream_buffer)));
  request &= ~3;

  std::memset(stream_buffer, 0, static_cast<size_t>(request));
  if(s_instance && s_instance->callback_ && request > 0) {
    s_instance->callback_(s_instance->userdata_, stream_buffer, request);
  }

  *done = request;
  return stream_buffer;
}
#endif

} // namespace nba
