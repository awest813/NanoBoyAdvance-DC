// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/device/video_device.hh>

#include <array>
#include <string_view>

#if defined(PLATFORM_DREAMCAST) && __has_include(<kos.h>)
#define NBA_DC_HAS_KOS 1
#include <dc/pvr.h>
#include <kos.h>
#else
#define NBA_DC_HAS_KOS 0
#endif

#if defined(NBA_DC_ENABLE_SDL_MENU) && !NBA_DC_HAS_KOS && __has_include(<SDL3/SDL.h>)
#define NBA_DC_HAS_SDL_MENU 1
#include <SDL3/SDL.h>
#else
#define NBA_DC_HAS_SDL_MENU 0
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
  void DrawRgb565(u16* buffer) override;
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
  static constexpr int kTextureHeight = 256;
  static constexpr int kTextureBytes = kTextureStride * kTextureHeight * static_cast<int>(sizeof(u16));
  static constexpr int kTextureUploadBytes = kTextureStride * kGBAHeight * static_cast<int>(sizeof(u16));

#if NBA_DC_HAS_KOS
  bool InitializePvr();
  void ShutdownPvr();
  void ConvertFrameToTexture(u32* buffer);
  void UploadRgb565Frame(u16* buffer);
  void UploadStagingToVram();
  void WaitForUploadDma();
  void RenderScaledFramePvr();
  void DrawSoftwareScaled(u32* buffer);
  void DrawSoftwareScaledRgb565(u16* buffer);

  u16* vram_base_ = nullptr;
  bool pvr_ready_ = false;
  bool frame_ready_ = false;
  bool pvr_scene_submitted_ = false;
  // Upload the staging texture to PVR RAM via asynchronous TA DMA so the SH4 is
  // free during the ~82 KiB copy; falls back to the blocking store-queue copy
  // automatically if a transfer cannot be started.
  bool use_dma_upload_ = true;
  bool upload_dma_in_flight_ = false;
  pvr_ptr_t texture_vram_ = nullptr;
  alignas(32) u16 texture_staging_[kTextureStride * kGBAHeight]{};
  pvr_poly_hdr_t poly_hdr_{};
#endif

#if NBA_DC_HAS_SDL_MENU
  bool InitializeSDL();
  void ShutdownSDL();
  void DrawSoftwareScaledSDL(u32* buffer);
  void DrawSoftwareScaledRgb565SDL(u16* buffer);
  void PutPixel(int x, int y, u16 color);

  SDL_Window* sdl_window_ = nullptr;
  SDL_Renderer* sdl_renderer_ = nullptr;
  SDL_Texture* sdl_texture_ = nullptr;
  std::array<u16, kScreenWidth * kScreenHeight> sdl_pixels_{};
#endif
};

} // namespace nba
