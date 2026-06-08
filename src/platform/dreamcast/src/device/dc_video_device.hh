// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/device/video_device.hh>

#include <string_view>

#if defined(PLATFORM_DREAMCAST) && __has_include(<kos.h>)
#define NBA_DC_HAS_KOS 1
#include <dc/pvr.h>
#include <kos.h>
#else
#define NBA_DC_HAS_KOS 0
#endif

namespace nba {

// Dreamcast video device using PVR hardware scaling for gameplay frames.
// Converts the GBA 240x160 framebuffer to an RGB565 texture, then draws a
// 2x scaled quad centered on the 640x480 display. UI overlays still use
// direct VRAM writes into the letterbox margins.
struct DCVideoDevice : VideoDevice {
  DCVideoDevice();
  ~DCVideoDevice() override;

  bool Initialize();
  void Draw(u32* buffer) override;
  void ShowFatalError(const char* message);

  void ClearScreen();
  void DrawText(int x, int y, std::string_view text);
  void DrawTextCentered(int y, std::string_view text);
  void DrawTextMultiline(int x, int y, std::string_view text);
  void DrawStatusBar(std::string_view text);
  void DrawOverlay(std::string_view text);
  void Present();

private:
  static constexpr int kGBAWidth  = 240;
  static constexpr int kGBAHeight = 160;
  static constexpr int kScreenWidth  = 640;
  static constexpr int kScreenHeight = 480;
  static constexpr int kScale = 2;
  static constexpr int kOffsetX = (kScreenWidth  - kGBAWidth  * kScale) / 2;
  static constexpr int kOffsetY = (kScreenHeight - kGBAHeight * kScale) / 2;
  static constexpr int kFontWidth = 8;
  static constexpr int kLineHeight = 18;
  static constexpr int kStatusBarY = 448;
  // PVR stride textures must be a multiple of 32 pixels wide.
  static constexpr int kTextureStride = 256;
  static constexpr int kTextureBytes = kTextureStride * kGBAHeight * static_cast<int>(sizeof(uint16));

#if NBA_DC_HAS_KOS
  bool InitializePvr();
  void ShutdownPvr();
  void ConvertFrameToTexture(u32* buffer);
  void RenderScaledFramePvr();
  void DrawSoftwareScaled(u32* buffer);

  uint16* vram_base_ = nullptr;
  bool pvr_ready_ = false;
  bool frame_ready_ = false;
  pvr_ptr_t texture_vram_ = nullptr;
  alignas(32) uint16 texture_staging_[kTextureStride * kGBAHeight]{};
  pvr_poly_hdr_t poly_hdr_{};
#endif
};

} // namespace nba
