// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>

#include "ppu.hh"

#include "ppu_timing.hh"

namespace nba::core {

void PPU::AdvanceSuppressedRasterScanline() {
  const int mode = mmio.dispcnt.mode;

  // Always finish the scanline (including vcount 159) so affine BG reference
  // points advance the same way as the drawn path's FinishBackgroundScanline.
  FinishBackgroundScanline(mode, 1232);

  const u64 timestamp_now = scheduler.GetTimestampNow();
  bg.timestamp_last_sync = timestamp_now;
  bg.cycle = 0U;
  merge.timestamp_last_sync = timestamp_now;
  merge.cycle = 0U;
}

auto PPU::CanUseFastTextBackground(int mode) const -> bool {
  if(!config->ppu_fast_mode || mode > 1) {
    return false;
  }

  const u16 dispcnt = mmio.dispcnt_latch[0] & mmio.dispcnt.hword;
  const int max_id = mode == 0 ? 3 : 1;

  for(int id = 0; id <= max_id; id++) {
    if((dispcnt & (256U << id)) && mmio.bgcnt[id].mosaic_enable) {
      return false;
    }
  }

  if(mode == 1 && (dispcnt & (256U << 2))) {
    if(mmio.bgcnt[2].mosaic_enable) {
      return false;
    }

    if(mmio.bgpa[0] != 0x100 || mmio.bgpb[0] != 0 || mmio.bgpc[0] != 0 || mmio.bgpd[0] != 0x100) {
      return false;
    }
  }

  return true;
}

auto PPU::CanUseFastBitmapBackground(int mode) const -> bool {
  if(!config->ppu_fast_mode || mode < 3 || mode > 5) {
    return false;
  }

  const u16 dispcnt = mmio.dispcnt_latch[0] & mmio.dispcnt.hword;
  if(!(dispcnt & (256U << 2))) {
    return false;
  }

  if(mmio.bgcnt[2].mosaic_enable) {
    return false;
  }

  return mmio.bgpa[0] == 0x100 && mmio.bgpb[0] == 0 && mmio.bgpc[0] == 0 && mmio.bgpd[0] == 0x100;
}

void PPU::FastRenderMode0BGScanline(int id) {
  const auto& bgcnt = mmio.bgcnt[id];
  const u32 tile_base = static_cast<u32>(bgcnt.tile_block) << 14;
  uint map_block = bgcnt.map_block;

  const uint bghofs = mmio.bghofs[id];
  const uint line = mmio.vcount + mmio.bgvofs[id];

  for(int screen_x = 0; screen_x < 240; screen_x++) {
    const uint world_x = static_cast<uint>(screen_x) + bghofs;
    const uint grid_x = world_x >> 3;
    const uint grid_y = line >> 3;
    const uint tile_x = world_x & 7U;
    const uint tile_y = line & 7U;

    uint block = map_block;
    const uint screen_x_map = (grid_x >> 5) & 1U;
    const uint screen_y_map = (grid_y >> 5) & 1U;

    switch(bgcnt.size) {
      case 1: block += screen_x_map; break;
      case 2: block += screen_y_map; break;
      case 3: block += screen_x_map + (screen_y_map << 1); break;
      default: break;
    }

    const u32 map_address = (block << 11) + ((grid_y & 31U) << 6) + ((grid_x & 31U) << 1);
    const u16 tile_entry = read<u16>(vram, map_address);

    const uint number = tile_entry & 0x3FFU;
    const bool flip_x = tile_entry & (1U << 10);
    const bool flip_y = tile_entry & (1U << 11);
    const uint palette = tile_entry >> 12;

    const uint real_tile_x = flip_x ? (7U - tile_x) : tile_x;
    const uint real_tile_y = flip_y ? (7U - tile_y) : tile_y;

    u32 index = 0U;

    if(bgcnt.full_palette) {
      const u32 tile_address = tile_base + (number << 6) + (real_tile_y << 3) + real_tile_x;
      index = read<u8>(vram, tile_address);
    } else {
      // 4bpp: one byte holds two pixels (low nibble = even column). Read the
      // exact byte for this column and pick its nibble; reading a u16 with a
      // masked address mishandles odd byte offsets and the >>4 high nibble.
      const u32 tile_address = tile_base + (number << 5) + (real_tile_y << 2) + (real_tile_x >> 1);
      const u8 tile_data = read<u8>(vram, tile_address);
      const u8 nibble = (real_tile_x & 1U) ? (tile_data >> 4) : (tile_data & 0x0FU);
      index = nibble;
      if(index != 0U) {
        index |= palette << 4;
      }
    }

    bg.buffer[screen_x][id] = index;
  }
}

void PPU::FastRenderMode2BGScanline(int id) {
  const auto& bgcnt = mmio.bgcnt[2 + id];
  const int log_size = bgcnt.size;
  const s32 size = 128 << log_size;
  const s32 mask = size - 1;

  const s32 base_x = bg.affine[id].x >> 8;
  const s32 base_y = bg.affine[id].y >> 8;

  for(int screen_x = 0; screen_x < 240; screen_x++) {
    s32 x = base_x + screen_x;
    s32 y = base_y;

    bool out_of_bounds = false;
    if(bgcnt.wraparound) {
      x &= mask;
      y &= mask;
    } else {
      out_of_bounds = ((x | y) & -size) != 0;
    }

    u32 index = 0U;
    if(!out_of_bounds) {
      const u32 map_address =
        static_cast<u32>(bgcnt.map_block) << 11 |
        static_cast<u32>((y >> 3) << (4 + log_size)) |
        static_cast<u32>(x >> 3);
      const u8 tile = read<u8>(vram, map_address);
      const u32 tile_address =
        static_cast<u32>(bgcnt.tile_block) << 14 |
        static_cast<u32>(tile) << 6 |
        static_cast<u32>((y & 7) << 3) |
        static_cast<u32>(x & 7);
      index = read<u8>(vram, tile_address);
    }

    bg.buffer[screen_x][2 + id] = index;
  }
}

void PPU::FastRenderMode3BGScanline() {
  const int base_x = bg.affine[0].x >> 8;
  const int vy = bg.affine[0].y >> 8;

  for(int screen_x = 0; screen_x < 240; screen_x++) {
    const int vx = base_x + screen_x;
    u32 color = 0U;

    if(vx >= 0 && vx < 240 && vy >= 0 && vy < 160) {
      const u32 address = (static_cast<u32>(vy) * 240U + static_cast<u32>(vx)) * 2U;
      color = read<u16>(vram, address) | 0x8000'0000U;
    }

    bg.buffer[screen_x][2] = color;
  }
}

void PPU::FastRenderMode4BGScanline() {
  const int base_x = bg.affine[0].x >> 8;
  const int vy = bg.affine[0].y >> 8;
  const u32 frame_base = static_cast<u32>(mmio.dispcnt.frame) * 0xA000U;

  for(int screen_x = 0; screen_x < 240; screen_x++) {
    const int vx = base_x + screen_x;
    u32 index = 0U;

    if(vx >= 0 && vx < 240 && vy >= 0 && vy < 160) {
      const u32 address = frame_base + static_cast<u32>(vy) * 240U + static_cast<u32>(vx);
      index = read<u8>(vram, address);
    }

    bg.buffer[screen_x][2] = index;
  }
}

void PPU::FastRenderMode5BGScanline() {
  const int base_x = bg.affine[0].x >> 8;
  const int vy = bg.affine[0].y >> 8;
  const u32 frame_base = static_cast<u32>(mmio.dispcnt.frame) * 0xA000U;

  for(int screen_x = 0; screen_x < 240; screen_x++) {
    const int vx = base_x + screen_x;
    u32 color = 0U;

    if(vx >= 0 && vx < 160 && vy >= 0 && vy < 128) {
      const u32 address = frame_base + (static_cast<u32>(vy) * 160U + static_cast<u32>(vx)) * 2U;
      color = read<u16>(vram, address) | 0x8000'0000U;
    }

    bg.buffer[screen_x][2] = color;
  }
}

void PPU::FinishBackgroundScanline(int mode, int cycles) {
  bg.cycle = std::min(1232U, bg.cycle + static_cast<uint>(cycles));

  auto& mosaic = mmio.mosaic;
  const u16 latched_dispcnt_and_current_dispcnt = mmio.dispcnt_latch[0] & mmio.dispcnt.hword;

  if(mmio.vcount < 159) {
    if(++mosaic.bg._counter_y == mosaic.bg.size_y) {
      mosaic.bg._counter_y = 0;
    } else {
      mosaic.bg._counter_y &= 15;
    }
  } else {
    mosaic.bg._counter_y = 0;
  }

  if(mode >= 1 && mode <= 5) {
    auto& bgx = mmio.bgx;
    auto& bgy = mmio.bgy;
    auto& bgpb = mmio.bgpb;
    auto& bgpd = mmio.bgpd;

    const auto advance = [&](int id) {
      const int bg_id = 2 + id;
      if(latched_dispcnt_and_current_dispcnt & (256U << bg_id)) {
        if(mmio.bgcnt[bg_id].mosaic_enable) {
          if(mosaic.bg._counter_y == 0) {
            bgx[id]._current += mosaic.bg.size_y * bgpb[id];
            bgy[id]._current += mosaic.bg.size_y * bgpd[id];
          }
        } else {
          bgx[id]._current += bgpb[id];
          bgy[id]._current += bgpd[id];
        }
      }
    };

    advance(0);

    if(mode == 2) {
      advance(1);
    }
  }
}

auto PPU::TryFastBackgroundScanline(int mode, int cycles) -> bool {
  if(bg.cycle != 0U || cycles < 1232) {
    return false;
  }

  const u16 dispcnt = mmio.dispcnt_latch[0] & mmio.dispcnt.hword;

  if(mode <= 1 && CanUseFastTextBackground(mode)) {
    const int max_id = mode == 0 ? 3 : 1;

    for(int id = 0; id <= max_id; id++) {
      if(dispcnt & (256U << id)) {
        FastRenderMode0BGScanline(id);
      }
    }

    if(mode == 1 && (dispcnt & (256U << 2))) {
      FastRenderMode2BGScanline(0);
    }

    FinishBackgroundScanline(mode, cycles);
    return true;
  }

  if(CanUseFastBitmapBackground(mode)) {
    switch(mode) {
      case 3: FastRenderMode3BGScanline(); break;
      case 4: FastRenderMode4BGScanline(); break;
      case 5: FastRenderMode5BGScanline(); break;
      default: return false;
    }

    FinishBackgroundScanline(mode, cycles);
    return true;
  }

  return false;
}

void PPU::InitBackground() {
  const u64 timestamp_now = scheduler.GetTimestampNow();

  bg.timestamp_init = timestamp_now;
  bg.timestamp_last_sync = timestamp_now;
  bg.cycle = 0U;

  for(auto& text : bg.text) {
    text.fetches = 0;
  }

  const bool first_scanline = mmio.vcount == 0;

  for(int id = 0; id < 2; id++) {
    auto& bgx = mmio.bgx[id];
    auto& bgy = mmio.bgy[id];

    // @todo: should BGY be latched when BGX was written and vice versa?
    if(bgx.written || first_scanline) {
      bgx._current = bgx.initial;
      bgx.written = false;
    }
    if(bgy.written || first_scanline) {
      bgy._current = bgy.initial;
      bgy.written = false;
    }

    bg.affine[id].x = bgx._current;
    bg.affine[id].y = bgy._current;
  }
}

void PPU::DrawBackground() {
#if defined(PLATFORM_DREAMCAST)
  const auto timing_start = std::chrono::steady_clock::now();
#endif

  const u64 timestamp_now = scheduler.GetTimestampNow();

  const int cycles = (int)(timestamp_now - bg.timestamp_last_sync);

  if(cycles == 0 || bg.cycle >= 1232U) {
    return;
  }

  const int mode = mmio.dispcnt.mode;

  if(TryFastBackgroundScanline(mode, cycles)) {
    bg.timestamp_last_sync = timestamp_now;
#if defined(PLATFORM_DREAMCAST)
    AddPpuTiming(
      config,
      std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - timing_start
      )
    );
#endif
    return;
  }

  switch(mode) {
    case 0: DrawBackgroundImpl<0>(cycles); break;
    case 1: DrawBackgroundImpl<1>(cycles); break;
    case 2: DrawBackgroundImpl<2>(cycles); break;
    case 3: DrawBackgroundImpl<3>(cycles); break;
    case 4: DrawBackgroundImpl<4>(cycles); break;
    case 5: DrawBackgroundImpl<5>(cycles); break;
    case 6:
    case 7: DrawBackgroundImpl<7>(cycles); break;
  }

  bg.timestamp_last_sync = timestamp_now;

#if defined(PLATFORM_DREAMCAST)
  AddPpuTiming(
    config,
    std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - timing_start
    )
  );
#endif
}

template<int mode> void PPU::DrawBackgroundImpl(int cycles) {
  const u16 latched_dispcnt_and_current_dispcnt = mmio.dispcnt_latch[0] & mmio.dispcnt.hword;

  /**
   * @todo: we are losing out on some possible optimizations,
   * by implementing the various BG modes in separate methods,
   * which we have to call on a per-cycle basis.
   */
  for(int i = 0; i < cycles; i++) {
    // We add one to the cycle counter for convenience,
    // because it makes some of the timing math simpler.
    const uint cycle = 1U + bg.cycle;

    // text-mode backgrounds
    if constexpr(mode <= 1) {
      const uint id = cycle & 3U; // BG0 - BG3

      if((id <= 1 || mode == 0) && (latched_dispcnt_and_current_dispcnt & (256U << id))) {
        RenderMode0BG(id, cycle);
      }
    }

    if(cycle < 1007U) {
      // affine backgrounds
      if constexpr(mode == 1 || mode == 2) {
        const int id = ~(cycle >> 1) & 1; // 0: BG2, 1: BG3

        if((id == 0 || mode == 2) && (latched_dispcnt_and_current_dispcnt & (1024U << id))) {
          RenderMode2BG(id, cycle);
        }
      }

      if constexpr(mode == 3) {
        if(latched_dispcnt_and_current_dispcnt & 1024U) {
          RenderMode3BG(cycle);
        }
      }

      if constexpr(mode == 4) {
        if(latched_dispcnt_and_current_dispcnt & 1024U) {
          RenderMode4BG(cycle);
        }
      }

      if constexpr(mode == 5) {
        if(latched_dispcnt_and_current_dispcnt & 1024U) {
          RenderMode5BG(cycle);
        }
      }
    }

    // @todo: research mosaic timing and narrow down the BG X/Y timing more precisely.
    if(cycle == 1232U) {
      auto& mosaic = mmio.mosaic;

      if(mmio.vcount < 159) {
        if(++mosaic.bg._counter_y == mosaic.bg.size_y) {
          mosaic.bg._counter_y = 0;
        } else {
          mosaic.bg._counter_y &= 15;
        }
      } else {
        mosaic.bg._counter_y = 0;
      }

      auto& bgx = mmio.bgx;
      auto& bgy = mmio.bgy;
      auto& bgpb = mmio.bgpb;
      auto& bgpd = mmio.bgpd;

      const auto AdvanceBGXY = [&](int id) {
        auto bg_id = 2 + id;

        /* Do not update internal X/Y unless the latched BG enable bit is set.
         * This behavior was confirmed on real hardware.
         */
        if(latched_dispcnt_and_current_dispcnt & (256U << bg_id)) {
          if(mmio.bgcnt[bg_id].mosaic_enable) {
            if(mosaic.bg._counter_y == 0) {
              bgx[id]._current += mosaic.bg.size_y * bgpb[id];
              bgy[id]._current += mosaic.bg.size_y * bgpd[id];
            }
          } else {
            bgx[id]._current += bgpb[id];
            bgy[id]._current += bgpd[id];
          }
        }
      };

      if constexpr(mode >= 1 && mode <= 5) {
        AdvanceBGXY(0);
      }

      if constexpr(mode == 2) {
        AdvanceBGXY(1);
      }
    }

    if(++bg.cycle == 1232U) {
      break;
    }
  }
}

} // namespace nba::core
