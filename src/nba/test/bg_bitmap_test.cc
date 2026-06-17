// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast bitmap BG rasterizers (modes 3/4/5:
// PPU::FastRenderMode3/4/5BGScanline, used by ppu_fast_mode / Speed profile)
// against the cycle-accurate path (DrawBackgroundImpl<3/4/5>).
//
// Identity matrix => per-pixel (base + screen_x), constant y; out-of-range
// pixels are transparent in both. Both fill bg.buffer[x][2] identically.
// Fuzzes mode, frame (modes 4/5), and the starting coordinate.

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ppu_test_access.hh"

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0xB17409);
  int tested = 0, with_pixels = 0, mismatches = 0;

  auto harness = std::make_unique<PPUTestAccess>();

  for(int it = 0; it < 40000; it++) {
    u8* vram = harness->Vram();
    for(u32 i = 0; i < 0x18000; i++) vram[i] = static_cast<u8>(rng()); // full VRAM

    const int mode = 3 + static_cast<int>(rng() % 3);
    const int frame = static_cast<int>(rng() & 1);
    const int max_w = (mode == 5) ? 160 : 240;
    const int max_h = (mode == 5) ? 128 : 160;
    const int base_x = static_cast<int>(rng() % (max_w + 64)) - 32;
    const int base_y = static_cast<int>(rng() % (max_h + 64)) - 32;
    const s32 ax = static_cast<s32>(base_x) << 8;
    const s32 ay = static_cast<s32>(base_y) << 8;

    harness->ConfigureBitmapBg(mode, frame);

    const auto fast = harness->RunBgBitmapFast(mode, ax, ay);
    const auto cycle = harness->RunBgBitmapCycle(mode, ax, ay);

    tested++;
    bool any = false; int bad = -1;
    for(int x = 0; x < 240; x++) {
      if(cycle[x] != 0U) any = true;
      if(fast[x] != cycle[x] && bad < 0) bad = x;
    }
    if(any) with_pixels++;
    if(bad >= 0) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d mode=%d frame=%d base=(%d,%d) x=%d fast=%08X cycle=%08X\n",
          it, mode, frame, base_x, base_y, bad, fast[bad], cycle[bad]);
      }
      mismatches++;
    }
  }

  std::printf("[ppu-bg-bitmap] tested=%d with_pixels=%d mismatches=%d\n",
    tested, with_pixels, mismatches);
  return mismatches == 0 ? 0 : 1;
}
