// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/dc_video_device.hh"

#include <algorithm>
#include <array>
#include <cstring>

#include "../dc_frame_timing.hh"
#include "../font_8x16.hh"

#if NBA_DC_HAS_KOS
#include <dc/pvr.h>
#include <dc/video.h>
#include <kos/cache.h>
#endif

namespace nba {

namespace {

auto FromRgb888(u32 pixel) -> u16 {
  const u32 r5 = (pixel >> 19) & 0x1F;
  const u32 g6 = (pixel >> 10) & 0x3F;
  const u32 b5 = (pixel >>  3) & 0x1F;
  return static_cast<u16>((r5 << 11) | (g6 << 5) | b5);
}

} // namespace

DCVideoDevice::DCVideoDevice() = default;

DCVideoDevice::~DCVideoDevice() {
#if NBA_DC_HAS_KOS
  ShutdownPvr();
#endif
#if NBA_DC_HAS_SDL_MENU
  ShutdownSDL();
#endif
}

bool DCVideoDevice::Initialize() {
#if NBA_DC_HAS_KOS
  vid_set_mode(DM_640x480, PM_RGB565);
  vram_base_ = (u16*)vram_s;
  ClearScreen();
  pvr_ready_ = InitializePvr();
#elif NBA_DC_HAS_SDL_MENU
  return InitializeSDL();
#endif
  return true;
}

#if NBA_DC_HAS_KOS
bool DCVideoDevice::InitializePvr() {
  if(pvr_init_defaults() != 0) {
    return false;
  }

  pvr_set_bg_color(0.0f, 0.0f, 0.0f);

  texture_vram_ = pvr_mem_malloc(kTextureBytes);
  if(!texture_vram_) {
    pvr_shutdown();
    return false;
  }

  // The staging buffer is wider (kTextureStride) than the GBA frame so PVR's
  // 32-pixel stride requirement is met. The padding columns (x >= kGBAWidth)
  // are never sampled (uv_clamp + u_max below clamp UV to kGBAWidth), so we
  // clear them exactly once here instead of on every presented frame.
  std::memset(texture_staging_, 0, sizeof(texture_staging_));

  pvr_poly_cxt_t context{};
  const int texture_format =
    PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE;
  pvr_poly_cxt_txr(
    &context,
    PVR_LIST_OP_POLY,
    texture_format,
    kTextureStride,
    kTextureHeight,
    texture_vram_,
    PVR_FILTER_NEAREST
  );
  context.gen.culling = PVR_CULLING_NONE;
  context.gen.fog_type = PVR_FOG_DISABLE;
  context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
  context.depth.write = false;
  context.txr.uv_clamp = PVR_UVCLAMP_UV;

  pvr_poly_compile(&poly_hdr_, &context);
  return true;
}

void DCVideoDevice::ShutdownPvr() {
  // Don't free VRAM out from under an in-flight DMA transfer.
  WaitForUploadDma();

  if(texture_vram_) {
    pvr_mem_free(texture_vram_);
    texture_vram_ = nullptr;
  }

  if(pvr_ready_) {
    pvr_shutdown();
    pvr_ready_ = false;
  }

  pvr_scene_submitted_ = false;
}

void DCVideoDevice::ConvertFrameToTexture(u32* buffer) {
  NBA_DC_FRAME_TIMING_SCOPE(Conv);

  // Stride padding (x >= kGBAWidth) is cleared once in InitializePvr and is
  // never sampled, so only the visible columns are written per frame.
  for(int y = 0; y < kGBAHeight; y++) {
    u16* row = texture_staging_ + y * kTextureStride;
    const u32* src = buffer + y * kGBAWidth;

    for(int x = 0; x < kGBAWidth; x++) {
      row[x] = FromRgb888(src[x]);
    }
  }

  UploadStagingToVram();
  frame_ready_ = true;
}

void DCVideoDevice::UploadRgb565Frame(u16* buffer) {
  NBA_DC_FRAME_TIMING_SCOPE(Conv);

  // Stride padding (x >= kGBAWidth) is cleared once in InitializePvr and is
  // never sampled, so only the visible columns are copied per frame.
  for(int y = 0; y < kGBAHeight; y++) {
    u16* row = texture_staging_ + y * kTextureStride;
    std::memcpy(row, buffer + y * kGBAWidth, kGBAWidth * sizeof(u16));
  }

  UploadStagingToVram();
  frame_ready_ = true;
}

void DCVideoDevice::UploadStagingToVram() {
  // Never rewrite the staging buffer or VRAM, nor start a store-queue copy,
  // while a previous DMA is still reading the buffer. Unconditional so toggling
  // DMA off mid-session cannot leave a transfer racing the blocking path.
  WaitForUploadDma();

  if(use_dma_upload_) {
    // The DMA engine reads texture_staging_ straight from main RAM and does not
    // snoop the SH4 cache, so write back the dirty lines before starting it.
    dcache_purge_range(
      reinterpret_cast<uintptr_t>(texture_staging_),
      kTextureUploadBytes
    );

    const int started = pvr_txr_load_dma(
      texture_staging_,
      texture_vram_,
      kTextureUploadBytes,
      /*block=*/false,
      /*callback=*/nullptr,
      /*cbdata=*/nullptr
    );
    if(started == 0) {
      upload_dma_in_flight_ = true;
      return;
    }

    // Kickoff failed (e.g. a transfer was unexpectedly busy): fall back to the
    // blocking store-queue copy so the frame still presents correctly.
  }

  pvr_txr_load(texture_staging_, texture_vram_, kTextureUploadBytes);
}

void DCVideoDevice::WaitForUploadDma() {
  if(!upload_dma_in_flight_) {
    return;
  }

  // Texture DMA shares the TA bus with vertex submission and the PVR samples the
  // texture during the render, so the upload must complete before the next scene
  // begins. pvr_dma_ready() is false while a transfer is active.
  while(!pvr_dma_ready()) {
  }

  upload_dma_in_flight_ = false;
}

void DCVideoDevice::RenderScaledFramePvr() {
  NBA_DC_FRAME_TIMING_SCOPE(Pvr);

  // Make sure this frame's texture has finished uploading before we submit the
  // scene that samples it (and before the TA is used for vertex submission).
  WaitForUploadDma();

  if(pvr_scene_submitted_) {
    pvr_wait_ready();
    pvr_scene_submitted_ = false;
  }

  pvr_scene_begin();
  pvr_list_begin(PVR_LIST_OP_POLY);
  pvr_txr_set_stride(kTextureStride);
  pvr_prim(&poly_hdr_, sizeof(poly_hdr_));

  constexpr float left = static_cast<float>(kOffsetX);
  constexpr float top = static_cast<float>(kOffsetY);
  constexpr float right = static_cast<float>(kOffsetX + kGBAWidth * kScale);
  constexpr float bottom = static_cast<float>(kOffsetY + kGBAHeight * kScale);
  constexpr float u_max = static_cast<float>(kGBAWidth) / static_cast<float>(kTextureStride);
  constexpr float v_max = static_cast<float>(kGBAHeight) / static_cast<float>(kTextureHeight);
  constexpr float z = 1.0f;

  alignas(32) pvr_vertex_t vert{};
  vert.z = z;
  vert.argb = 0xFFFFFFFF;
  vert.oargb = 0;

  vert.flags = PVR_CMD_VERTEX;
  vert.x = left;
  vert.y = top;
  vert.u = 0.0f;
  vert.v = 0.0f;
  pvr_prim(&vert, sizeof(vert));

  vert.flags = PVR_CMD_VERTEX;
  vert.x = right;
  vert.y = top;
  vert.u = u_max;
  vert.v = 0.0f;
  pvr_prim(&vert, sizeof(vert));

  vert.flags = PVR_CMD_VERTEX;
  vert.x = left;
  vert.y = bottom;
  vert.u = 0.0f;
  vert.v = v_max;
  pvr_prim(&vert, sizeof(vert));

  vert.flags = PVR_CMD_VERTEX_EOL;
  vert.x = right;
  vert.y = bottom;
  vert.u = u_max;
  vert.v = v_max;
  pvr_prim(&vert, sizeof(vert));

  pvr_list_finish();
  pvr_scene_finish();
  pvr_scene_submitted_ = true;
  frame_ready_ = false;
}

void DCVideoDevice::DrawSoftwareScaled(u32* buffer) {
  vram_base_ = (u16*)vram_s;
  if(!vram_base_ || !buffer) {
    return;
  }

  for(int y = 0; y < kGBAHeight; y++) {
    for(int sy = 0; sy < kScale; sy++) {
      const int screen_y = kOffsetY + y * kScale + sy;
      u16* dst = vram_base_ + screen_y * kScreenWidth + kOffsetX;

      for(int x = 0; x < kGBAWidth; x++) {
        const u16 rgb565 = FromRgb888(buffer[y * kGBAWidth + x]);

        for(int sx = 0; sx < kScale; sx++) {
          dst[x * kScale + sx] = rgb565;
        }
      }
    }
  }
}

void DCVideoDevice::DrawSoftwareScaledRgb565(u16* buffer) {
  vram_base_ = (u16*)vram_s;
  if(!vram_base_ || !buffer) {
    return;
  }

  for(int y = 0; y < kGBAHeight; y++) {
    for(int sy = 0; sy < kScale; sy++) {
      const int screen_y = kOffsetY + y * kScale + sy;
      u16* dst = vram_base_ + screen_y * kScreenWidth + kOffsetX;

      for(int x = 0; x < kGBAWidth; x++) {
        const u16 rgb565 = buffer[y * kGBAWidth + x];

        for(int sx = 0; sx < kScale; sx++) {
          dst[x * kScale + sx] = rgb565;
        }
      }
    }
  }
}
#endif

#if NBA_DC_HAS_SDL_MENU
bool DCVideoDevice::InitializeSDL() {
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    return false;
  }

  sdl_window_ = SDL_CreateWindow(
    "NanoBoyAdvance Dreamcast Menu",
    kScreenWidth,
    kScreenHeight,
    0
  );
  if(!sdl_window_) {
    ShutdownSDL();
    return false;
  }

  sdl_renderer_ = SDL_CreateRenderer(sdl_window_, nullptr);
  if(!sdl_renderer_) {
    ShutdownSDL();
    return false;
  }

  sdl_texture_ = SDL_CreateTexture(
    sdl_renderer_,
    SDL_PIXELFORMAT_RGB565,
    SDL_TEXTUREACCESS_STREAMING,
    kScreenWidth,
    kScreenHeight
  );
  if(!sdl_texture_) {
    ShutdownSDL();
    return false;
  }

  ClearScreen();
  Present();
  return true;
}

void DCVideoDevice::ShutdownSDL() {
  if(sdl_texture_) {
    SDL_DestroyTexture(sdl_texture_);
    sdl_texture_ = nullptr;
  }
  if(sdl_renderer_) {
    SDL_DestroyRenderer(sdl_renderer_);
    sdl_renderer_ = nullptr;
  }
  if(sdl_window_) {
    SDL_DestroyWindow(sdl_window_);
    sdl_window_ = nullptr;
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

void DCVideoDevice::PutPixel(int x, int y, u16 color) {
  if(x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight) {
    return;
  }

  sdl_pixels_[static_cast<size_t>(y) * kScreenWidth + static_cast<size_t>(x)] = color;
}

void DCVideoDevice::DrawSoftwareScaledSDL(u32* buffer) {
  if(!buffer) {
    return;
  }

  for(int y = 0; y < kGBAHeight; y++) {
    for(int x = 0; x < kGBAWidth; x++) {
      const u16 rgb565 = FromRgb888(buffer[y * kGBAWidth + x]);

      for(int sy = 0; sy < kScale; sy++) {
        for(int sx = 0; sx < kScale; sx++) {
          PutPixel(kOffsetX + x * kScale + sx, kOffsetY + y * kScale + sy, rgb565);
        }
      }
    }
  }
}

void DCVideoDevice::DrawSoftwareScaledRgb565SDL(u16* buffer) {
  if(!buffer) {
    return;
  }

  for(int y = 0; y < kGBAHeight; y++) {
    for(int x = 0; x < kGBAWidth; x++) {
      const u16 rgb565 = buffer[y * kGBAWidth + x];

      for(int sy = 0; sy < kScale; sy++) {
        for(int sx = 0; sx < kScale; sx++) {
          PutPixel(kOffsetX + x * kScale + sx, kOffsetY + y * kScale + sy, rgb565);
        }
      }
    }
  }
}
#endif

void DCVideoDevice::ClearScreen() {
#if NBA_DC_HAS_KOS
  RefreshFramebuffer();
  if(!vram_base_) return;
  std::memset(vram_base_, 0, kScreenWidth * kScreenHeight * sizeof(u16));
#elif NBA_DC_HAS_SDL_MENU
  std::fill(sdl_pixels_.begin(), sdl_pixels_.end(), 0);
#endif
}

void DCVideoDevice::DrawText(int x, int y, std::string_view text) {
#if NBA_DC_HAS_KOS || NBA_DC_HAS_SDL_MENU
  RefreshFramebuffer();

  int cursor_x = x;
  int cursor_y = y;
  for(char c : text) {
    if(cursor_y <= -static_cast<int>(kFontHeight) || cursor_y >= kScreenHeight) {
      return;
    }

    if(c < 32 || c > 126) {
      cursor_x += kFontWidth;
      continue;
    }

    if(cursor_x >= kScreenWidth) {
      break;
    }

    if(cursor_x <= -static_cast<int>(kFontWidth)) {
      cursor_x += kFontWidth;
      continue;
    }

    const auto& glyph = kFont8x16[c - 32];
    for(int row = 0; row < static_cast<int>(kFontHeight); row++) {
      const int dst_y = cursor_y + row;
      if(dst_y < 0 || dst_y >= kScreenHeight) {
        continue;
      }

      const std::uint8_t bits = glyph[row];
      for(int col = 0; col < static_cast<int>(kFontWidth); col++) {
        const int dst_x = cursor_x + col;
        if(dst_x < 0 || dst_x >= kScreenWidth) {
          continue;
        }

        PokePixel(dst_x, dst_y, (bits >> (7 - col)) & 1 ? kFgColor : kBgColor);
      }
    }
    cursor_x += kFontWidth;
  }
#else
  (void)x;
  (void)y;
  (void)text;
#endif
}

void DCVideoDevice::DrawTextCentered(int y, std::string_view text) {
  const int x = std::max(0, kOffsetX + ((kGBAWidth * kScale) / 2) - (static_cast<int>(text.size()) * kFontWidth) / 2);
  DrawText(x, y, text);
}

void DCVideoDevice::DrawFilledRect(int x, int y, int width, int height, u16 color) {
#if NBA_DC_HAS_KOS || NBA_DC_HAS_SDL_MENU
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(kScreenWidth, x + width);
  const int y1 = std::min(kScreenHeight, y + height);
  if(x0 >= x1 || y0 >= y1) {
    return;
  }

  RefreshFramebuffer();

  for(int row = y0; row < y1; row++) {
    for(int col = x0; col < x1; col++) {
      PokePixel(col, row, color);
    }
  }
#else
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  (void)color;
#endif
}

void DCVideoDevice::DrawTextMultiline(int x, int y, std::string_view text) {
  while(!text.empty() && y < kScreenHeight) {
    const auto newline = text.find('\n');
    const auto line = text.substr(0, newline);
    DrawText(x, y, line);
    y += kLineHeight;

    if(newline == std::string_view::npos) {
      break;
    }

    text.remove_prefix(newline + 1);
  }
}

void DCVideoDevice::DrawStatusBar(std::string_view text) {
#if NBA_DC_HAS_KOS
  RefreshFramebuffer();
  if(!vram_base_) return;

  for(int x = 0; x < kScreenWidth; x++) {
    vram_base_[kStatusBarY * kScreenWidth + x] = 0x1084;
  }
#elif NBA_DC_HAS_SDL_MENU
  for(int y = kStatusBarY; y < kStatusBarY + kLineHeight && y < kScreenHeight; y++) {
    for(int x = 0; x < kScreenWidth; x++) {
      PutPixel(x, y, 0x1084);
    }
  }
#endif

  DrawText(16, kStatusBarY + 4, text);
}

void DCVideoDevice::DrawOverlay(std::string_view text) {
  DrawStatusBar(text);
}

void DCVideoDevice::Present() {
#if NBA_DC_HAS_KOS
  if(pvr_ready_ && frame_ready_) {
    RenderScaledFramePvr();
  }
  {
    NBA_DC_FRAME_TIMING_SCOPE(Present);
    vid_waitvbl();
  }
#elif NBA_DC_HAS_SDL_MENU
  if(!sdl_renderer_ || !sdl_texture_) {
    return;
  }
  SDL_UpdateTexture(sdl_texture_, nullptr, sdl_pixels_.data(), kScreenWidth * sizeof(u16));
  SDL_SetRenderDrawColor(sdl_renderer_, 0, 0, 0, 255);
  SDL_RenderClear(sdl_renderer_);
  SDL_RenderTexture(sdl_renderer_, sdl_texture_, nullptr, nullptr);
  SDL_RenderPresent(sdl_renderer_);
#endif
}

void DCVideoDevice::DrawRgb565(u16* buffer) {
#if NBA_DC_HAS_KOS
  if(!buffer) {
    return;
  }

  if(pvr_ready_) {
    UploadRgb565Frame(buffer);
    return;
  }

  DrawSoftwareScaledRgb565(buffer);
#elif NBA_DC_HAS_SDL_MENU
  DrawSoftwareScaledRgb565SDL(buffer);
  Present();
#else
  (void)buffer;
#endif
}

void DCVideoDevice::Draw(u32* buffer) {
#if NBA_DC_HAS_KOS
  if(!buffer) {
    return;
  }

  if(pvr_ready_) {
    ConvertFrameToTexture(buffer);
    return;
  }

  DrawSoftwareScaled(buffer);
#elif NBA_DC_HAS_SDL_MENU
  DrawSoftwareScaledSDL(buffer);
  Present();
#else
  (void)buffer;
#endif
}

void DCVideoDevice::ShowFatalError(const char* message) {
  ClearScreen();
  DrawTextMultiline(kOffsetX, kOffsetY, message ? message : "Unknown error");
  Present();
}

} // namespace nba
