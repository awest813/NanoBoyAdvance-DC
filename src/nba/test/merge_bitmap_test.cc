// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast bitmap-mode merge path
// (PPU::FastMergeBitmapScanlineImpl, modes 3/4/5, used by ppu_fast_mode /
// Speed profile) against the cycle-accurate merge in PPU::DrawMergeImpl.
//
// The fast bitmap path renders BG2 only with OBJ disabled and handles three
// bg.buffer[x][2] cases: 0 => backdrop, 0x80000000 flag => direct 15-bit color,
// else => 256-color palette index. The same real DrawMergeImpl is run twice
// with ppu_fast_mode toggled; both must produce the identical 240-pixel row.

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ppu_test_access.hh"

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0xB17A);
  const int vcount = 80;
  int tested = 0, fast_applies = 0, mismatches = 0;

  auto harness = std::make_unique<PPUTestAccess>();

  for(int it = 0; it < 60000; it++) {
    u8* pram = harness->Pram();
    for(int i = 0; i < 0x400; i++) pram[i] = static_cast<u8>(rng());

    const int mode = 3 + static_cast<int>(rng() % 3); // 3, 4 or 5

    harness->ClearMergeInputs();
    for(int x = 0; x < 240; x++) {
      u32 v;
      switch(rng() % 4) {
        case 0:  v = 0U; break;                                   // transparent => backdrop
        case 1:  v = 0x80000000U | (rng() & 0x7FFFU); break;      // direct 15-bit color
        default: v = 1U + (rng() % 255U); break;                  // 256-color palette index
      }
      harness->SetBgPixel(x, 2, v);
    }

    harness->ConfigureBitmapMerge(mode);

    tested++;
    if(harness->FastBitmapMergeApplies()) fast_applies++;

    const auto fast = harness->RunMerge(true, vcount);
    const auto slow = harness->RunMerge(false, vcount);

    int bad = -1;
    for(int x = 0; x < 240; x++) {
      if(fast[x] != slow[x]) { bad = x; break; }
    }
    if(bad >= 0) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d mode=%d x=%d fast=%08X slow=%08X\n",
          it, mode, bad, fast[bad], slow[bad]);
      }
      mismatches++;
    }
  }

  std::printf("[ppu-merge-bitmap] tested=%d fast_applies=%d mismatches=%d\n",
    tested, fast_applies, mismatches);

  if(fast_applies == 0) {
    std::printf("[ppu-merge-bitmap] ERROR: fast path never applied\n");
    return 2;
  }
  return mismatches == 0 ? 0 : 1;
}
