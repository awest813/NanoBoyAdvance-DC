// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast text-mode merge path (PPU::FastMergeTextScanlineImpl,
// used by ppu_fast_mode / Speed profile) against the cycle-accurate merge in
// PPU::DrawMergeImpl. The same real DrawMergeImpl is run twice with ppu_fast_mode
// toggled; for a configuration the fast path supports (mode 0, no windows /
// mosaic / greenswap / forced-blank / first-target, sfx=NONE) both must produce
// the identical 240-pixel output row. Exercises BG priority layering, BG-vs-OBJ
// priority, OBJ over backdrop, and semi-transparent OBJ alpha blend over a
// second-target BG.

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ppu_test_access.hh"

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0x5EED);
  const int vcount = 80;
  int tested = 0, fast_applies = 0, mismatches = 0;

  auto harness = std::make_unique<PPUTestAccess>();

  for(int it = 0; it < 60000; it++) {
    // Randomize the palette (BG region bytes 0x000-0x1FF, OBJ region 0x200-0x3FF).
    u8* pram = harness->Pram();
    for(int i = 0; i < 0x400; i++) pram[i] = static_cast<u8>(rng());

    int priority[4];
    bool bg_enable[4];
    for(int id = 0; id < 4; id++) {
      priority[id] = static_cast<int>(rng() & 3);
      bg_enable[id] = (rng() & 3) != 0; // ~75% enabled
    }
    const bool obj_enable = (rng() & 3) != 0;
    bool second_target[6];
    for(int l = 0; l < 6; l++) second_target[l] = rng() & 1;
    const int eva = static_cast<int>(rng() % 17);
    const int evb = static_cast<int>(rng() % 17);

    harness->ClearMergeInputs();
    for(int x = 0; x < 240; x++) {
      for(int id = 0; id < 4; id++) {
        // ~30% transparent, else a BG palette index (1-255).
        const u32 v = (rng() % 10 < 3) ? 0U : (1U + (rng() % 255U));
        harness->SetBgPixel(x, id, v);
      }
      if(rng() & 1) {
        const unsigned color = 1U + (rng() % 255U);   // opaque OBJ palette index
        const unsigned prio = rng() & 3;
        const unsigned alpha = rng() & 1;              // semi-transparent flag
        harness->SetSpritePixel(x, color, prio, alpha);
      }
    }

    harness->ConfigureTextMerge(priority, bg_enable, obj_enable, second_target, eva, evb);

    tested++;
    if(harness->FastTextMergeApplies()) fast_applies++;

    const auto fast = harness->RunMerge(true, vcount);
    const auto slow = harness->RunMerge(false, vcount);

    int bad = -1;
    for(int x = 0; x < 240; x++) {
      if(fast[x] != slow[x]) { bad = x; break; }
    }
    if(bad >= 0) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d x=%d fast=%08X slow=%08X obj_en=%d\n",
          it, bad, fast[bad], slow[bad], obj_enable);
      }
      mismatches++;
    }
  }

  std::printf("[ppu-merge-fast] tested=%d fast_applies=%d mismatches=%d\n",
    tested, fast_applies, mismatches);

  // The test is only meaningful if the fast path was actually taken.
  if(fast_applies == 0) {
    std::printf("[ppu-merge-fast] ERROR: fast path never applied\n");
    return 2;
  }
  return mismatches == 0 ? 0 : 1;
}
