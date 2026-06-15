// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Equality test for the fast-path OBJ scanline rasterizer
// (PPU::FastDrawSpriteScanlineImpl, used by ppu_fast_mode / Speed profile)
// against the cycle-accurate path (InitSprite + DrawSpriteImpl).
//
// Both paths write PPU::sprite.buffer_wr. For a single sprite per scanline the
// two must agree exactly on every opaque pixel (color, priority, alpha). We use
// a single sprite so the test is unaffected by the fast path's deliberate
// simplification of priority updates on *transparent* overlapping pixels.
//
// The harness drives the real PPU code (linked, not copied) via a friend hook.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include <nba/config.hh>

#include "core.hh"
#include "hw/ppu/ppu.hh"

namespace nba::core {

struct PPUTestAccess {
  std::shared_ptr<Config> config = std::make_shared<Config>();
  Core core{config};
  PPU& ppu = core.ppu;

  struct Pix { int x; unsigned c; unsigned prio; unsigned alpha; };

  auto oam() -> u8* { return ppu.oam; }
  auto vram() -> u8* { return ppu.vram; }

  void Configure(int mode, bool map1d, bool hblank_oam) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = mode;
    m.dispcnt.oam_mapping_1d = map1d ? 1 : 0;
    m.dispcnt.hblank_oam_access = hblank_oam ? 1 : 0;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0;
    m.dispcnt.enable[PPU::LAYER_OBJ] = 1;
    m.mosaic.obj.size_x = 1; m.mosaic.obj.size_y = 1; m.mosaic.obj._counter_y = 0;
    m.mosaic.bg.size_x = 1;  m.mosaic.bg.size_y = 1;  m.mosaic.bg._counter_y = 0;
  }

  auto Collect() -> std::vector<Pix> {
    std::vector<Pix> out;
    for(int x = 0; x < 240; x++) {
      const auto& p = ppu.sprite.buffer_wr[x];
      if(p.color != 0U) out.push_back({x, p.color, p.priority, p.alpha});
    }
    return out;
  }

  auto RunFast(int vcount) -> std::vector<Pix> {
    std::memset(ppu.sprite.buffer_wr, 0, sizeof(PPU::Sprite::Pixel) * 240);
    ppu.FastDrawSpriteScanlineImpl(vcount);
    return Collect();
  }

  auto RunCycle(int vcount) -> std::vector<Pix> {
    // InitSprite sets sprite.vcount = (mmio.vcount + 1) % 228.
    ppu.mmio.vcount = static_cast<u8>((vcount + 228 - 1) % 228);
    ppu.InitSprite();
    ppu.DrawSpriteImpl(static_cast<int>(ppu.sprite.latch_cycle_limit));
    return Collect();
  }
};

} // namespace nba::core

namespace {

void Wr16(u8* p, u32 off, u16 v) { p[off] = v & 0xFF; p[off + 1] = (v >> 8) & 0xFF; }
void Wr32(u8* p, u32 off, u32 v) { for(int i = 0; i < 4; i++) p[off + i] = (v >> (8 * i)) & 0xFF; }

} // namespace

int main() {
  using nba::core::PPUTestAccess;

  std::mt19937 rng(0xC0FFEE);
  int tested = 0, with_pixels = 0, mismatches = 0;
  int affine_pix = 0, nonaffine_pix = 0;

  // One harness reused across iterations (memory re-randomized each time).
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

    u8* oam = harness->oam();
    u8* vram = harness->vram();
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
      // Matrix group 0 lives at OAM offsets 6/14/22/30. Bias toward near-identity
      // so a good fraction of pixels sample in-bounds (real rotation/scale).
      Wr16(oam, 6,  static_cast<u16>(static_cast<s16>(128 + static_cast<int>(rng() % 384))));
      Wr16(oam, 14, static_cast<u16>(static_cast<s16>(static_cast<int>(rng() % 256) - 128)));
      Wr16(oam, 22, static_cast<u16>(static_cast<s16>(static_cast<int>(rng() % 256) - 128)));
      Wr16(oam, 30, static_cast<u16>(static_cast<s16>(128 + static_cast<int>(rng() % 384))));
    }

    harness->Configure(/*mode=*/0, map1d, /*hblank_oam=*/rng() & 1);
    const int vcount = (static_cast<int>(rng() % 80) + static_cast<int>(ypos)) % 160;

    const auto fast = harness->RunFast(vcount);
    const auto cycle = harness->RunCycle(vcount);

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
