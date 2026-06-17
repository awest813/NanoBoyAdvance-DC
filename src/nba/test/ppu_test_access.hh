// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared test harness that drives the real PPU rasterizers (linked, not copied)
// via the PPUTestAccess friend hook declared in ppu.hh / core.hh.

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <nba/config.hh>

#include "core.hh"
#include "hw/ppu/ppu.hh"

namespace nba::core {

struct PPUTestAccess {
  std::shared_ptr<Config> config = std::make_shared<Config>();
  Core core{config};
  PPU& ppu = core.ppu;

  auto Oam() -> u8* { return ppu.oam; }
  auto Vram() -> u8* { return ppu.vram; }
  auto Pram() -> u8* { return ppu.pram; }

  // ---- Sprite fast-path testing ------------------------------------------

  struct Pix { int x; unsigned c; unsigned prio; unsigned alpha; };

  void ConfigureSprites(bool map1d, bool hblank_oam) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = 0;
    m.dispcnt.oam_mapping_1d = map1d ? 1 : 0;
    m.dispcnt.hblank_oam_access = hblank_oam ? 1 : 0;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0;
    m.dispcnt.enable[PPU::LAYER_OBJ] = 1;
    m.mosaic.obj.size_x = 1; m.mosaic.obj.size_y = 1; m.mosaic.obj._counter_y = 0;
    m.mosaic.bg.size_x = 1;  m.mosaic.bg.size_y = 1;  m.mosaic.bg._counter_y = 0;
  }

  auto CollectSprite() -> std::vector<Pix> {
    std::vector<Pix> out;
    for(int x = 0; x < 240; x++) {
      const auto& p = ppu.sprite.buffer_wr[x];
      if(p.color != 0U) out.push_back({x, p.color, p.priority, p.alpha});
    }
    return out;
  }

  auto RunSpriteFast(int vcount) -> std::vector<Pix> {
    std::memset(ppu.sprite.buffer_wr, 0, sizeof(PPU::Sprite::Pixel) * 240);
    ppu.FastDrawSpriteScanlineImpl(vcount);
    return CollectSprite();
  }

  auto RunSpriteCycle(int vcount) -> std::vector<Pix> {
    ppu.mmio.vcount = static_cast<u8>((vcount + 228 - 1) % 228); // InitSprite does +1
    ppu.InitSprite();
    ppu.DrawSpriteImpl(static_cast<int>(ppu.sprite.latch_cycle_limit));
    return CollectSprite();
  }

  // ---- Merge fast-path testing -------------------------------------------

  void ClearMergeInputs() {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    for(int x = 0; x < 240; x++) ppu.sprite.buffer_rd[x].data = 0;
  }

  void SetBgPixel(int x, int id, u32 value) { ppu.bg.buffer[x][id] = value; }

  void SetSpritePixel(int x, unsigned color, unsigned prio, unsigned alpha) {
    auto& p = ppu.sprite.buffer_rd[x];
    p.data = 0;
    p.color = static_cast<u8>(color);
    p.priority = prio;
    p.alpha = alpha;
  }

  // Configure a text-mode (mode 0) scanline. With no windows/mosaic/greenswap/
  // forced-blank/first-target and sfx=NONE this satisfies CanUseFastTextMerge,
  // so toggling ppu_fast_mode selects the fast vs cycle-accurate path.
  void ConfigureTextMerge(const int priority[4], const bool bg_enable[4],
                          bool obj_enable, const bool second_target[6],
                          int eva, int evb) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = 0;

    u16 enable_bits = 0;
    for(int id = 0; id < 4; id++) {
      m.bgcnt[id].priority = priority[id];
      m.bgcnt[id].mosaic_enable = 0;
      if(bg_enable[id]) enable_bits |= static_cast<u16>(256U << id);
    }
    if(obj_enable) enable_bits |= static_cast<u16>(256U << PPU::LAYER_OBJ);

    m.dispcnt.hword = enable_bits;       // no bit 7 => not forced blank
    m.dispcnt_latch[0] = enable_bits;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0; // no windows

    m.mosaic.bg.size_x = 1; m.mosaic.bg.size_y = 1; m.mosaic.bg._counter_y = 0;
    m.mosaic.obj.size_x = 1; m.mosaic.obj.size_y = 1; m.mosaic.obj._counter_y = 0;

    m.greenswap = 0;
    m.bldcnt.sfx = BlendControl::SFX_NONE;
    for(int l = 0; l < 6; l++) {
      m.bldcnt.targets[0][l] = 0;                 // no first target (fast req.)
      m.bldcnt.targets[1][l] = second_target[l];  // second target for OBJ alpha
    }
    m.eva = eva; m.evb = evb; m.evy = 0;
  }

  // Does the fast text-merge path apply for the current MMIO state?
  auto FastTextMergeApplies() -> bool {
    config->ppu_fast_mode = true;
    bool enable_obj = false;
    return ppu.CanUseFastTextMerge(enable_obj);
  }

  // Configure a bitmap-mode (3/4/5) scanline: BG2 only, OBJ disabled, satisfying
  // CanUseFastBitmapMerge so toggling ppu_fast_mode selects fast vs cycle path.
  void ConfigureBitmapMerge(int mode) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = mode;

    const u16 enable_bits = static_cast<u16>(256U << 2); // BG2 enabled, OBJ off
    m.dispcnt.hword = enable_bits;       // no bit 7 => not forced blank
    m.dispcnt_latch[0] = enable_bits;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0; // no windows

    for(int id = 0; id < 4; id++) { m.bgcnt[id].priority = 0; m.bgcnt[id].mosaic_enable = 0; }
    m.mosaic.bg.size_x = 1; m.mosaic.bg.size_y = 1; m.mosaic.bg._counter_y = 0;
    m.mosaic.obj.size_x = 1; m.mosaic.obj.size_y = 1; m.mosaic.obj._counter_y = 0;

    m.greenswap = 0;
    m.bldcnt.sfx = BlendControl::SFX_NONE;
    for(int l = 0; l < 6; l++) { m.bldcnt.targets[0][l] = 0; m.bldcnt.targets[1][l] = 0; }
    m.eva = 0; m.evb = 0; m.evy = 0;
  }

  auto FastBitmapMergeApplies() -> bool {
    config->ppu_fast_mode = true;
    return ppu.CanUseFastBitmapMerge();
  }

  // ---- Background fast-path testing --------------------------------------

  auto BgColumn(int id) -> std::vector<u32> {
    std::vector<u32> col(240);
    for(int x = 0; x < 240; x++) col[x] = ppu.bg.buffer[x][id];
    return col;
  }

  // Mode 0 text background: enable a single BG `id` with the given control.
  void ConfigureTextBg(int id, int vcount, int size, int tile_block, int map_block,
                      bool full_palette, int bghofs, int bgvofs) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = 0;
    const u16 enable_bits = static_cast<u16>(256U << id);
    m.dispcnt.hword = enable_bits;
    m.dispcnt_latch[0] = enable_bits;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0;
    for(int b = 0; b < 4; b++) m.bgcnt[b].mosaic_enable = 0;

    auto& c = m.bgcnt[id];
    c.priority = 0; c.tile_block = tile_block; c.map_block = map_block; c.size = size;
    c.full_palette = full_palette ? 1 : 0; c.mosaic_enable = 0;
    m.bghofs[id] = bghofs; m.bgvofs[id] = bgvofs;

    m.mosaic.bg.size_x = 1; m.mosaic.bg.size_y = 1; m.mosaic.bg._counter_y = 0;
    m.vcount = static_cast<u8>(vcount);
  }

  auto RunBgTextFast(int id) -> std::vector<u32> {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    config->ppu_fast_mode = true;
    ppu.FastRenderMode0BGScanline(id);
    return BgColumn(id);
  }

  auto RunBgTextCycle(int id) -> std::vector<u32> {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    config->ppu_fast_mode = false;
    ppu.InitBackground();
    ppu.DrawBackgroundImpl<0>(1232);
    return BgColumn(id);
  }

  // Mode-1 affine BG2. Identity matrix (required by the fast path); map_block /
  // tile_block 0 keep all in-bounds fetches below 0x10000. bg.affine[0] is set
  // directly so the fast and cycle paths start from the same coordinate.
  void ConfigureAffineBg(int size, bool wraparound) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = 1;
    const u16 enable_bits = static_cast<u16>(256U << 2); // BG2 only
    m.dispcnt.hword = enable_bits;
    m.dispcnt_latch[0] = enable_bits;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0;
    for(int b = 0; b < 4; b++) m.bgcnt[b].mosaic_enable = 0;

    auto& c = m.bgcnt[2];
    c.priority = 0; c.tile_block = 0; c.map_block = 0; c.size = size;
    c.mosaic_enable = 0; c.wraparound = wraparound ? 1 : 0;

    m.bgpa[0] = 0x100; m.bgpb[0] = 0; m.bgpc[0] = 0; m.bgpd[0] = 0x100;
    m.mosaic.bg.size_x = 1; m.mosaic.bg.size_y = 1; m.mosaic.bg._counter_y = 0;
    m.vcount = 0;
  }

  auto RunBgAffineFast(s32 ax, s32 ay) -> std::vector<u32> {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    ppu.bg.affine[0].x = ax; ppu.bg.affine[0].y = ay;
    config->ppu_fast_mode = true;
    ppu.FastRenderMode2BGScanline(0);
    return BgColumn(2);
  }

  auto RunBgAffineCycle(s32 ax, s32 ay) -> std::vector<u32> {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    ppu.bg.cycle = 0;
    ppu.bg.affine[0].x = ax; ppu.bg.affine[0].y = ay;
    config->ppu_fast_mode = false;
    ppu.DrawBackgroundImpl<1>(1232);
    return BgColumn(2);
  }

  // Bitmap BG (modes 3/4/5). Identity matrix; bg.affine[0] set directly.
  void ConfigureBitmapBg(int mode, int frame) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = mode;
    m.dispcnt.frame = frame;
    const u16 enable_bits = static_cast<u16>(256U << 2); // BG2
    m.dispcnt.hword = enable_bits;
    m.dispcnt_latch[0] = enable_bits;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0;
    for(int b = 0; b < 4; b++) m.bgcnt[b].mosaic_enable = 0;
    m.bgpa[0] = 0x100; m.bgpb[0] = 0; m.bgpc[0] = 0; m.bgpd[0] = 0x100;
    m.mosaic.bg.size_x = 1; m.mosaic.bg.size_y = 1; m.mosaic.bg._counter_y = 0;
    m.vcount = 0;
  }

  auto RunBgBitmapFast(int mode, s32 ax, s32 ay) -> std::vector<u32> {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    ppu.bg.affine[0].x = ax; ppu.bg.affine[0].y = ay;
    config->ppu_fast_mode = true;
    if(mode == 3) ppu.FastRenderMode3BGScanline();
    else if(mode == 4) ppu.FastRenderMode4BGScanline();
    else ppu.FastRenderMode5BGScanline();
    return BgColumn(2);
  }

  auto RunBgBitmapCycle(int mode, s32 ax, s32 ay) -> std::vector<u32> {
    std::memset(ppu.bg.buffer, 0, sizeof(ppu.bg.buffer));
    ppu.bg.cycle = 0;
    ppu.bg.affine[0].x = ax; ppu.bg.affine[0].y = ay;
    config->ppu_fast_mode = false;
    if(mode == 3) ppu.DrawBackgroundImpl<3>(1232);
    else if(mode == 4) ppu.DrawBackgroundImpl<4>(1232);
    else ppu.DrawBackgroundImpl<5>(1232);
    return BgColumn(2);
  }

  // ---- Cross-scanline affine reference-point advance --------------------

  struct AffineState { s32 bx0, by0, bx1, by1; int counter_y; };

  void SetupAffineAdvance(int mode, bool mosaic2, bool mosaic3, int size_y,
                          s16 pb0, s16 pd0, s16 pb1, s16 pd1,
                          s32 bx0, s32 by0, s32 bx1, s32 by1,
                          int counter_y, int vcount) {
    auto& m = ppu.mmio;
    m.dispcnt.mode = mode;
    u16 en = static_cast<u16>(256U << 2);
    if(mode == 2) en |= static_cast<u16>(256U << 3);
    m.dispcnt.hword = en;
    m.dispcnt_latch[0] = en;
    for(int i = 0; i < 8; i++) m.dispcnt.enable[i] = 0;
    for(int b = 0; b < 4; b++) m.bgcnt[b].mosaic_enable = 0;
    m.bgcnt[2].mosaic_enable = mosaic2 ? 1 : 0;
    m.bgcnt[3].mosaic_enable = mosaic3 ? 1 : 0;

    m.mosaic.bg.size_x = 1; m.mosaic.bg.size_y = size_y; m.mosaic.bg._counter_y = counter_y;
    m.bgpa[0] = 0x100; m.bgpc[0] = 0; m.bgpb[0] = pb0; m.bgpd[0] = pd0;
    m.bgpa[1] = 0x100; m.bgpc[1] = 0; m.bgpb[1] = pb1; m.bgpd[1] = pd1;
    m.bgx[0]._current = bx0; m.bgy[0]._current = by0;
    m.bgx[1]._current = bx1; m.bgy[1]._current = by1;
    m.vcount = static_cast<u8>(vcount);

    ppu.bg.affine[0].x = bx0; ppu.bg.affine[0].y = by0;
    ppu.bg.affine[1].x = bx1; ppu.bg.affine[1].y = by1;
  }

  auto CaptureAffine() -> AffineState {
    auto& m = ppu.mmio;
    return {m.bgx[0]._current, m.bgy[0]._current, m.bgx[1]._current, m.bgy[1]._current,
            m.mosaic.bg._counter_y};
  }

  // The slow renderer advances the affine reference points inline at cycle 1232.
  void RunSlowAdvance(int mode) {
    ppu.bg.cycle = 0;
    config->ppu_fast_mode = false;
    switch(mode) {
      case 1: ppu.DrawBackgroundImpl<1>(1232); break;
      case 2: ppu.DrawBackgroundImpl<2>(1232); break;
      case 3: ppu.DrawBackgroundImpl<3>(1232); break;
      case 4: ppu.DrawBackgroundImpl<4>(1232); break;
      default: ppu.DrawBackgroundImpl<5>(1232); break;
    }
  }

  // The fast path and the frame-skip-suppressed path both advance via this.
  void RunFinishAdvance(int mode) {
    ppu.FinishBackgroundScanline(mode, 1232);
  }

  auto RunMerge(bool fast, int vcount) -> std::vector<u32> {
    config->ppu_fast_mode = fast;
    ppu.mmio.vcount = static_cast<u8>(vcount);
    ppu.InitMerge();
    ppu.DrawMergeImpl(1006);
    std::vector<u32> row(240);
    for(int x = 0; x < 240; x++) row[x] = ppu.output[ppu.frame][vcount * 240 + x];
    return row;
  }
};

} // namespace nba::core
