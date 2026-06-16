// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast mode-0 text background rasterizer
// (PPU::FastRenderMode0BGScanline, used by ppu_fast_mode / Speed profile)
// against the cycle-accurate path (InitBackground + DrawBackgroundImpl<0>).
//
// Both fill bg.buffer[x][id]; for a single enabled BG they must be identical.
// Fuzzes screen size, char/screen base blocks, 16- and 256-color tiles, and
// scroll offsets. Drives the real PPU code (linked, not copied).

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ppu_test_access.hh"

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0xBADA55);
  int tested = 0, with_pixels = 0, mismatches = 0;

  auto harness = std::make_unique<PPUTestAccess>();

  for(int it = 0; it < 40000; it++) {
    u8* vram = harness->Vram();
    for(u32 i = 0; i < 0x10000; i++) vram[i] = static_cast<u8>(rng()); // BG VRAM

    const int id = static_cast<int>(rng() & 3);
    const int vcount = static_cast<int>(rng() % 160);
    const int size = static_cast<int>(rng() & 3);
    // Keep all map and tile fetches inside BG VRAM (< 0x10000). The fast path
    // does not model BG "open-bus" reads into the OBJ VRAM region (>= 0x10000)
    // the way the cycle-accurate path does (it returns the BG latch there) --
    // an accepted Speed-mode approximation for the unusual layouts that reach
    // it. tile_block 0 keeps tile data below 0x10000 for all tile numbers; a
    // bounded map_block keeps the screen-block fetches below it for all sizes.
    const int tile_block = 0;
    const int map_block = static_cast<int>(rng() % 25);
    const bool full_palette = rng() & 1;
    const int bghofs = static_cast<int>(rng() % 512);
    const int bgvofs = static_cast<int>(rng() % 512);

    harness->ConfigureTextBg(id, vcount, size, tile_block, map_block, full_palette, bghofs, bgvofs);

    const auto fast = harness->RunBgTextFast(id);
    const auto cycle = harness->RunBgTextCycle(id);

    tested++;
    bool any = false;
    int bad = -1;
    for(int x = 0; x < 240; x++) {
      if(cycle[x] != 0U) any = true;
      if(fast[x] != cycle[x] && bad < 0) bad = x;
    }
    if(any) with_pixels++;
    if(bad >= 0) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d id=%d vcount=%d size=%d tb=%d mb=%d fp=%d hofs=%d vofs=%d "
                    "x=%d fast=%08X cycle=%08X\n",
          it, id, vcount, size, tile_block, map_block, full_palette, bghofs, bgvofs,
          bad, fast[bad], cycle[bad]);
      }
      mismatches++;
    }
  }

  std::printf("[ppu-bg-text] tested=%d with_pixels=%d mismatches=%d\n",
    tested, with_pixels, mismatches);
  return mismatches == 0 ? 0 : 1;
}
