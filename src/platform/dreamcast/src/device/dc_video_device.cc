// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/dc_video_device.hh"

#include <algorithm>
#include <array>
#include <cstring>

#include "../font_8x16.hh"

#if NBA_DC_HAS_KOS
#include <dc/pvr.h>
#include <dc/video.h>
#endif

namespace nba {

DCVideoDevice::DCVideoDevice() = default;

DCVideoDevice::~DCVideoDevice() {
#if NBA_DC_HAS_KOS
  ShutdownPvr();
#endif
}

bool DCVideoDevice::Initialize() {
#if NBA_DC_HAS_KOS
  vid_set_mode(DM_640x480, PM_RGB565);
  vram_base_ = (uint16*)vram_s;
  ClearScreen();
  pvr_ready_ = InitializePvr();
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

  pvr_poly_cxt_t context{};
  const int texture_format =
    PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE;
  pvr_poly_cxt_txr(
    &context,
    PVR_LIST_OP_POLY,
    texture_format,
    kTextureStride,
    kGBAHeight,
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
  if(texture_vram_) {
    pvr_mem_free(texture_vram_);
    texture_vram_ = nullptr;
  }

  if(pvr_ready_) {
    pvr_shutdown();
    pvr_ready_ = false;
  }
}

void DCVideoDevice::ConvertFrameToTexture(u32* buffer) {
  for(int y = 0; y < kGBAHeight; y++) {
    uint16* row = texture_staging_ + y * kTextureStride;
    const u32* src = buffer + y * kGBAWidth;

    for(int x = 0; x < kGBAWidth; x++) {
      const u32 pixel = src[x];
      const u8 b = (pixel >>  0) & 0xFF;
      const u8 g = (pixel >>  8) & 0xFF;
      const u8 r = (pixel >> 16) & 0xFF;
      row[x] = static_cast<uint16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    std::memset(row + kGBAWidth, 0, (kTextureStride - kGBAWidth) * sizeof(uint16));
  }

  pvr_txr_load(texture_staging_, texture_vram_, kTextureBytes);
  frame_ready_ = true;
}

void DCVideoDevice::RenderScaledFramePvr() {
  pvr_wait_ready();
  pvr_scene_begin();
  pvr_list_begin(PVR_LIST_OP_POLY);
  pvr_txr_set_stride(kTextureStride);
  pvr_prim(&poly_hdr_, sizeof(poly_hdr_));

  const float left = static_cast<float>(kOffsetX);
  const float top = static_cast<float>(kOffsetY);
  const float right = static_cast<float>(kOffsetX + kGBAWidth * kScale);
  const float bottom = static_cast<float>(kOffsetY + kGBAHeight * kScale);
  const float u_max = static_cast<float>(kGBAWidth) / static_cast<float>(kTextureStride);
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
  vert.v = 1.0f;
  pvr_prim(&vert, sizeof(vert));

  vert.flags = PVR_CMD_VERTEX_EOL;
  vert.x = right;
  vert.y = bottom;
  vert.u = u_max;
  vert.v = 1.0f;
  pvr_prim(&vert, sizeof(vert));

  pvr_list_finish();
  pvr_scene_finish();
  frame_ready_ = false;
}

void DCVideoDevice::DrawSoftwareScaled(u32* buffer) {
  vram_base_ = (uint16*)vram_s;
  if(!vram_base_ || !buffer) {
    return;
  }

  for(int y = 0; y < kGBAHeight; y++) {
    for(int sy = 0; sy < kScale; sy++) {
      const int screen_y = kOffsetY + y * kScale + sy;
      uint16* dst = vram_base_ + screen_y * kScreenWidth + kOffsetX;

      for(int x = 0; x < kGBAWidth; x++) {
        const u32 pixel = buffer[y * kGBAWidth + x];
        const u8 b = (pixel >>  0) & 0xFF;
        const u8 g = (pixel >>  8) & 0xFF;
        const u8 r = (pixel >> 16) & 0xFF;

        const uint16 rgb565 = static_cast<uint16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));

        for(int sx = 0; sx < kScale; sx++) {
          dst[x * kScale + sx] = rgb565;
        }
      }
    }
  }
}
#endif

void DCVideoDevice::ClearScreen() {
#if NBA_DC_HAS_KOS
  vram_base_ = (uint16*)vram_s;
  if(!vram_base_) return;
  std::memset(vram_base_, 0, kScreenWidth * kScreenHeight * sizeof(uint16));
#endif
}

void DCVideoDevice::DrawText(int x, int y, std::string_view text) {
#if NBA_DC_HAS_KOS
  vram_base_ = (uint16*)vram_s;
  if(!vram_base_) return;

  const uint16 fg = 0xFFFF;
  const uint16 bg = 0x0000;

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

        const bool on = (bits >> (7 - col)) & 1;
        vram_base_[dst_y * kScreenWidth + dst_x] = on ? fg : bg;
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
  vram_base_ = (uint16*)vram_s;
  if(!vram_base_) return;

  for(int x = 0; x < kScreenWidth; x++) {
    vram_base_[kStatusBarY * kScreenWidth + x] = 0x1084;
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
  vid_waitvbl();
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
