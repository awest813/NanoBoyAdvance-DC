// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <nba/core.hh>
#include <nba/scheduler.hh>

#include "arm/arm7tdmi.hh"
#include "arm/dynarec/dynarec.hh"
#include "bus/bus.hh"
#include "hw/apu/apu.hh"
#include "hw/ppu/ppu.hh"
#include "hw/dma/dma.hh"
#include "hw/irq/irq.hh"
#include "hw/keypad/keypad.hh"
#include "hw/timer/timer.hh"

namespace nba::core {

struct Core final : CoreBase {
  // Lets the PPU unit-test harness reach the fully-wired PPU instance.
  friend struct PPUTestAccess;

  Core(std::shared_ptr<Config> config);

  void Reset() override;

  void Attach(std::vector<u8> const& bios) override;
  void Attach(ROM&& rom) override;
  auto CreateRTC() -> std::unique_ptr<RTC> override;
  auto CreateSolarSensor() -> std::unique_ptr<SolarSensor> override;
  void LoadState(SaveState const& state) override;
  void CopyState(SaveState& state) override;
  void SetKeyStatus(Key key, bool pressed) override;
  void Run(int cycles) override;

  auto GetROM() -> ROM& override;
  auto GetPRAM() -> u8* override;
  auto GetVRAM() -> u8* override;
  auto GetOAM() -> u8* override;
  auto PeekByteIO(u32 address) -> u8  override;
  auto PeekHalfIO(u32 address) -> u16 override;
  auto PeekWordIO(u32 address) -> u32 override;
  auto PeekByte(u32 address) -> u8 override;
  auto PeekHalf(u32 address) -> u16 override;
  auto PeekWord(u32 address) -> u32 override;
  void PokeByte(u32 address, u8 value) override;
  void PokeHalf(u32 address, u16 value) override;
  void PokeWord(u32 address, u32 value) override;
  auto GetBGHOFS(int id) -> u16 override;
  auto GetBGVOFS(int id) -> u16 override;

  Scheduler& GetScheduler() override;
  auto TakeDynarecTelemetry() -> DynarecTelemetry override;

private:
  static void OnCodeWrite(void* ctx, u32 address, u32 size);
  void SkipBootScreen();
  auto SearchSoundMainRAM() -> u32;
#if defined(PLATFORM_DREAMCAST)
  auto SearchSoundMainRAMFromFile() -> u32;
#endif

  u32 hle_audio_hook;
  std::shared_ptr<Config> config;

  Scheduler scheduler;

  arm::ARM7TDMI cpu;
  arm::dynarec::Dynarec dynarec;
  IRQ irq;
  DMA dma;
  APU apu;
  PPU ppu;
  Timer timer;
  KeyPad keypad;
  Bus bus;
};

} // namespace nba::core
