// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast-path OBJ scanline rasterizer
// (PPU::FastDrawSpriteScanlineImpl, used by ppu_fast_mode / Speed profile)
// against the cycle-accurate path (InitSprite + DrawSpriteImpl).
//
// For a single sprite per scanline the two must agree exactly on every opaque
// pixel (color, priority, alpha). A single sprite avoids the fast path's
// deliberate simplification of priority updates on transparent overlapping
// pixels. The harness drives the real PPU code (linked, not copied).

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ppu_test_access.hh"

namespace {

void Wr16(u8* p, u32 off, u16 v) { p[off] = v & 0xFF; p[off + 1] = (v >> 8) & 0xFF; }
void Wr32(u8* p, u32 off, u32 v) { for(int i = 0; i < 4; i++) p[off + i] = (v >> (8 * i)) & 0xFF; }

} // namespace

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0xC0FFEE);
  int tested = 0, with_pixels = 0, mismatches = 0;
  int affine_pix = 0, nonaffine_pix = 0;

  auto harness = std::make_unique<PPUTestAccess>();

  for(int it = 0; it < 200000; it++) {
    const bool affine = rng() & 1;
    const bool is_256 = rng() & 1;
    const bool map1d = rng() & 1;
    const uint shape = rng() % 3;
    const uint size = rng() % 4;
    const int xpos = static_cast<int>(rng() % 300) - 30;
    const uint ypos = rng() % 160;
    const uint mode = (rng() & 7) == 0 ? 1U : 0U; // OBJ_SEMI(=1) occasionally, else Normal

    u8* oam = harness->Oam();
    u8* vram = harness->Vram();
    // Disable every OAM entry (rotate/scale off + bit9 set => hidden); then set
    // up only sprite 0. Otherwise zeroed entries are enabled 8x8 sprites at (0,0)
    // and would render on low scanlines. attr3 (matrix) slots stay untouched.
    std::memset(oam, 0, 0x400);
    for(int i = 0; i < 128; i++) Wr16(oam, i * 8, 0x0200);
    for(u32 i = 0x10000; i < 0x18000; i++) vram[i] = static_cast<u8>(rng());

    u32 attr0 = (ypos & 0xFF);
    attr0 |= (shape & 3) << 14;
    if(is_256) attr0 |= 1U << 13;
    attr0 |= mode << 10;
    if(affine) {
      attr0 |= 0x100U;
      if(rng() & 1) attr0 |= 0x200U; // double-size
    } else {
      if(rng() & 1) attr0 |= 1U << 28; // flip H
      if(rng() & 1) attr0 |= 1U << 29; // flip V
    }
    const u32 attr1 = (static_cast<u32>(xpos & 0x1FF) << 16) | (size << 30);
    Wr32(oam, 0, attr0 | attr1);
    Wr16(oam, 4, static_cast<u16>(rng() & 0xFFFF)); // attr2 (tile/priority/palette)

    if(affine) {
      // Matrix group 0 at OAM offsets 6/14/22/30, biased near identity.
      Wr16(oam, 6,  static_cast<u16>(static_cast<s16>(128 + static_cast<int>(rng() % 384))));
      Wr16(oam, 14, static_cast<u16>(static_cast<s16>(static_cast<int>(rng() % 256) - 128)));
      Wr16(oam, 22, static_cast<u16>(static_cast<s16>(static_cast<int>(rng() % 256) - 128)));
      Wr16(oam, 30, static_cast<u16>(static_cast<s16>(128 + static_cast<int>(rng() % 384))));
    }

    harness->ConfigureSprites(map1d, /*hblank_oam=*/rng() & 1);
    const int vcount = (static_cast<int>(rng() % 80) + static_cast<int>(ypos)) % 160;

    const auto fast = harness->RunSpriteFast(vcount);
    const auto cycle = harness->RunSpriteCycle(vcount);

    tested++;
    if(!cycle.empty()) {
      with_pixels++;
      (affine ? affine_pix : nonaffine_pix)++;
    }

    bool ok = fast.size() == cycle.size();
    for(size_t k = 0; ok && k < cycle.size(); k++) {
      ok = fast[k].x == cycle[k].x && fast[k].c == cycle[k].c &&
           fast[k].prio == cycle[k].prio && fast[k].alpha == cycle[k].alpha;
    }
    if(!ok) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d affine=%d 256=%d map1d=%d vcount=%d fast=%zu cycle=%zu\n",
          it, affine, is_256, map1d, vcount, fast.size(), cycle.size());
      }
      mismatches++;
    }
  }

  std::printf("[ppu-sprite-fast] tested=%d with_pixels=%d (affine=%d nonaffine=%d) mismatches=%d\n",
    tested, with_pixels, affine_pix, nonaffine_pix, mismatches);
  return mismatches == 0 ? 0 : 1;
}
