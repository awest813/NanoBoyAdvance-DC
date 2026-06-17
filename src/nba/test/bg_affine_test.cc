// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast mode-1 affine BG2 rasterizer
// (PPU::FastRenderMode2BGScanline, used by ppu_fast_mode / Speed profile)
// against the cycle-accurate path (DrawBackgroundImpl<1>).
//
// The fast affine path only applies with an identity matrix, so per-pixel it
// reduces to (base + screen_x) with constant y; the cycle path advances the
// internal affine coordinate by PA/PC each pixel. Both must fill bg.buffer[x][2]
// identically. Fuzzes screen size, wraparound, and the starting coordinate
// (covering in-bounds and out-of-bounds). Drives the real PPU (linked).

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ppu_test_access.hh"

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0xAFF12E);
  int tested = 0, with_pixels = 0, mismatches = 0;

  auto harness = std::make_unique<PPUTestAccess>();

  for(int it = 0; it < 40000; it++) {
    u8* vram = harness->Vram();
    for(u32 i = 0; i < 0x10000; i++) vram[i] = static_cast<u8>(rng()); // BG VRAM

    const int size = static_cast<int>(rng() & 3);
    const bool wraparound = rng() & 1;
    const int dim = 128 << size;
    // Starting coordinate in pixels, biased to land inside the rotation BG while
    // still reaching the edges/out-of-bounds region.
    const int base_x = static_cast<int>(rng() % (dim + 64)) - 32;
    const int base_y = static_cast<int>(rng() % (dim + 64)) - 32;
    const s32 ax = static_cast<s32>(base_x) << 8;
    const s32 ay = static_cast<s32>(base_y) << 8;

    harness->ConfigureAffineBg(size, wraparound);

    const auto fast = harness->RunBgAffineFast(ax, ay);
    const auto cycle = harness->RunBgAffineCycle(ax, ay);

    tested++;
    bool any = false; int bad = -1;
    for(int x = 0; x < 240; x++) {
      if(cycle[x] != 0U) any = true;
      if(fast[x] != cycle[x] && bad < 0) bad = x;
    }
    if(any) with_pixels++;
    if(bad >= 0) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d size=%d wrap=%d base=(%d,%d) x=%d fast=%08X cycle=%08X\n",
          it, size, wraparound, base_x, base_y, bad, fast[bad], cycle[bad]);
      }
      mismatches++;
    }
  }

  std::printf("[ppu-bg-affine] tested=%d with_pixels=%d mismatches=%d\n",
    tested, with_pixels, mismatches);
  return mismatches == 0 ? 0 : 1;
}
