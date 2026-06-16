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
