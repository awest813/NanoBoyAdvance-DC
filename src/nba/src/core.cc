// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <nba/common/crc32.hh>
#include <nba/rom/gpio/rtc.hh>
#include <nba/rom/gpio/solar_sensor.hh>
#include <atom/logger/logger.hh>

#if defined(PLATFORM_DREAMCAST)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#endif

#include "core.hh"

namespace nba {

namespace core {

Core::Core(std::shared_ptr<Config> config)
    : config(config)
    , cpu(scheduler, bus)
    , dynarec(cpu)
    , irq(cpu, scheduler)
    , dma(bus, irq, scheduler)
    , apu(scheduler, dma, bus, config)
    , ppu(scheduler, irq, dma, config)
    , timer(scheduler, irq, apu)
    , keypad(scheduler, irq)
    , bus(scheduler, {cpu, irq, dma, apu, ppu, timer, keypad}) {
  bus.SetCodeInvalidateHook(&Core::OnCodeWrite, this);
  Reset();
}

void Core::OnCodeWrite(void* ctx, u32 address, u32 size) {
  static_cast<Core*>(ctx)->dynarec.InvalidateRange(address, size);
}

void Core::Reset() {
  scheduler.Reset();
  cpu.Reset();
  dynarec.Reset();
  irq.Reset();
  dma.Reset();
  timer.Reset();
  apu.Reset();
  ppu.Reset();
  bus.Reset();
  keypad.Reset();

  if(config->skip_bios) {
    SkipBootScreen();
  }

  if(config->audio.mp2k_hle_enable) {
    apu.GetMP2K().UseCubicFilter() = config->audio.mp2k_hle_cubic;
    apu.GetMP2K().ForceReverb() = config->audio.mp2k_hle_force_reverb;
    hle_audio_hook = SearchSoundMainRAM();
    if(hle_audio_hook != 0xFFFFFFFF) {
      ATOM_INFO("Core: detected MP2K audio mixer @ 0x{:08X}", hle_audio_hook);
    }
  } else {
    hle_audio_hook = 0xFFFFFFFF;
  }
}

void Core::Attach(std::vector<u8> const& bios) {
  bus.Attach(bios);
  dynarec.InvalidateAll();
}

void Core::Attach(ROM&& rom) {
  bus.Attach(std::move(rom));
  dynarec.InvalidateAll();
}

auto Core::CreateRTC() -> std::unique_ptr<RTC> {
  return std::make_unique<RTC>(irq);
}

auto Core::CreateSolarSensor() -> std::unique_ptr<SolarSensor> {
  return std::make_unique<SolarSensor>();
}

void Core::SetKeyStatus(Key key, bool pressed) {
  keypad.SetKeyStatus(key, pressed);
}

void Core::Run(int cycles) {
  using HaltControl = Bus::Hardware::HaltControl;

  const auto limit = scheduler.GetTimestampNow() + cycles;
#if defined(PLATFORM_DREAMCAST)
  const u32 idle_loop_target = config->idle_loop_eliminate_target;
#endif

  while(scheduler.GetTimestampNow() < limit) {
#if defined(PLATFORM_DREAMCAST)
    if(idle_loop_target != 0 &&
       bus.hw.haltcnt == HaltControl::Run &&
       cpu.state.r15 == idle_loop_target) {
      bus.hw.haltcnt = HaltControl::Halt;
    }
#endif

    if(bus.hw.haltcnt == HaltControl::Run) {
      if(cpu.state.r15 == hle_audio_hook) {
        if(auto* sound_info_ptr = bus.GetHostAddress<u32>(0x03007FF0)) {
          const u32 sound_info_addr = *sound_info_ptr;
          if(const auto sound_info = bus.GetHostAddress<MP2K::SoundInfo>(sound_info_addr)) {
            apu.GetMP2K().SoundMainRAM(*sound_info);
          }
        }

        // The HLE mixer replaces SoundMainRAM(); skip the ROM routine body.
        cpu.ReturnFromSubroutine();
        continue;
      }

      if(config->cpu_dynarec && dynarec.TryRunBlock()) {
        continue;
      }

      cpu.Run();
    } else {
      while(scheduler.GetTimestampNow() < limit && !irq.ShouldUnhaltCPU()) {
        if(dma.IsRunning()) {
          dma.Run();
          if(irq.ShouldUnhaltCPU()) continue; // can become true during the DMA
        }

        bus.Step(scheduler.GetRemainingCycleCount());
      }

      if(irq.ShouldUnhaltCPU()) {
        bus.Step(1);
        bus.hw.haltcnt = HaltControl::Run;
      }
    }
  }
}

void Core::SkipBootScreen() {
  cpu.SwitchMode(arm::MODE_SYS);
  cpu.state.bank[arm::BANK_SVC][arm::BANK_R13] = 0x03007FE0;
  cpu.state.bank[arm::BANK_IRQ][arm::BANK_R13] = 0x03007FA0;
  cpu.state.r13 = 0x03007F00;
  cpu.state.r15 = 0x08000000;
}

namespace {

constexpr u32 kSoundMainCRC32 = 0x27EA7FCF;
constexpr int kSoundMainLength = 48;
constexpr size_t kSoundMainPointerReadEnd = 0x74 + sizeof(u32);
constexpr size_t kSoundMainScanStep = 4;

auto DecodeSoundMainRAMHook(u8 const* window) -> u32 {
  u32 address;
  std::memcpy(&address, window + 0x74, sizeof(u32));

  if(address & 1) {
    address &= ~1u;
    address += sizeof(u16) * 2;
  } else {
    address &= ~3u;
    address += sizeof(u32) * 2;
  }

  return address;
}

auto ScanBufferForSoundMain(u8 const* data, size_t size) -> u32 {
  if(size < kSoundMainLength) {
    return 0xFFFFFFFF;
  }

  const size_t safe_limit = size >= kSoundMainPointerReadEnd
    ? size - kSoundMainPointerReadEnd
    : 0;

  for(size_t index = 0; index <= safe_limit; index += kSoundMainScanStep) {
    if(crc32(data + index, kSoundMainLength) != kSoundMainCRC32) {
      continue;
    }

    return DecodeSoundMainRAMHook(data + index);
  }

  return 0xFFFFFFFF;
}

} // namespace

auto Core::SearchSoundMainRAM() -> u32 {
  auto& rom = bus.memory.rom;
  auto& rom_vec = rom.GetRawROM();

  if(rom_vec.size() >= static_cast<size_t>(kSoundMainLength)) {
    const u32 hook = ScanBufferForSoundMain(rom_vec.data(), rom_vec.size());
    if(hook != 0xFFFFFFFF) {
      return hook;
    }

    return 0xFFFFFFFF;
  }

#if defined(PLATFORM_DREAMCAST)
  if(rom.IsPagedROM()) {
    const u32 resident_offset = rom.FindSoundMainOffsetInResidentPages();
    if(resident_offset != 0xFFFFFFFF) {
      u8 window[kSoundMainPointerReadEnd];
      if(rom.CopyRange(resident_offset, sizeof(window), window)) {
        return DecodeSoundMainRAMHook(window);
      }
    }

    return SearchSoundMainRAMFromFile();
  }
#endif

  return 0xFFFFFFFF;
}

#if defined(PLATFORM_DREAMCAST)
auto Core::SearchSoundMainRAMFromFile() -> u32 {
  static constexpr size_t kChunkSize   = 64 * 1024;
  static constexpr size_t kOverlapSize = kSoundMainPointerReadEnd;

  // MP2K SoundMain lives in the game-engine portion of ROM.  Capping the
  // search at 8 MiB covers virtually all GBA titles while bounding the CD
  // read time to a few seconds instead of scanning the full 16+ MiB.
  static constexpr size_t kMaxSearchSize = 8 * 1024 * 1024;

  auto& rom = bus.memory.rom;
  if(!rom.IsPagedROM()) {
    return 0xFFFFFFFF;
  }

  const size_t rom_size = std::min(rom.GetPagedROMSize(), kMaxSearchSize);
  const auto& rom_path = rom.GetPagedROMPath();

  auto* file = std::fopen(rom_path.c_str(), "rb");
  if(!file) {
    return 0xFFFFFFFF;
  }

  std::vector<u8> chunk(kChunkSize + kOverlapSize);
  size_t overlap = 0;
  size_t bytes_scanned = 0;

  while(bytes_scanned < rom_size) {
    const size_t to_read = std::min(kChunkSize, rom_size - bytes_scanned);
    const size_t bytes_read = std::fread(chunk.data() + overlap, 1, to_read, file);
    if(bytes_read == 0) {
      break;
    }

    const size_t buffer_size = overlap + bytes_read;
    const u32 hook = ScanBufferForSoundMain(chunk.data(), buffer_size);
    if(hook != 0xFFFFFFFF) {
      std::fclose(file);
      return hook;
    }

    overlap = std::min(kOverlapSize, buffer_size);
    if(overlap > 0) {
      std::memmove(chunk.data(), chunk.data() + buffer_size - overlap, overlap);
    }

    bytes_scanned += bytes_read;
  }

  std::fclose(file);
  return 0xFFFFFFFF;
}
#endif

auto Core::GetROM() -> ROM& {
  return bus.memory.rom;
}

auto Core::GetPRAM() -> u8* {
  return ppu.GetPRAM();
}

auto Core::GetVRAM() -> u8* {
  return ppu.GetVRAM();
}

auto Core::GetOAM() -> u8* {
  return ppu.GetOAM();
}

auto Core::PeekByteIO(u32 address) -> u8  {
  return bus.hw.ReadByte(address);
}

auto Core::PeekHalfIO(u32 address) -> u16 {
  return bus.hw.ReadHalf(address);
}

auto Core::PeekWordIO(u32 address) -> u32 {
  return bus.hw.ReadWord(address);
}

auto Core::PeekByte(u32 address) -> u8 {
  return bus.ReadByte(address, Bus::Nonsequential);
}

auto Core::PeekHalf(u32 address) -> u16 {
  return bus.ReadHalf(address, Bus::Nonsequential);
}

auto Core::PeekWord(u32 address) -> u32 {
  return bus.ReadWord(address, Bus::Nonsequential);
}

void Core::PokeByte(u32 address, u8 value) {
  bus.WriteByte(address, value, Bus::Nonsequential);
}

void Core::PokeHalf(u32 address, u16 value) {
  bus.WriteHalf(address, value, Bus::Nonsequential);
}

void Core::PokeWord(u32 address, u32 value) {
  bus.WriteWord(address, value, Bus::Nonsequential);
}

auto Core::GetBGHOFS(int id) -> u16 {
  return ppu.mmio.bghofs[id];
}

auto Core::GetBGVOFS(int id) -> u16 {
  return ppu.mmio.bgvofs[id];
}

Scheduler& Core::GetScheduler() {
  return scheduler;
}

auto Core::TakeDynarecTelemetry() -> DynarecTelemetry {
  DynarecTelemetry out{};
  dynarec.TakeTelemetry(out.hits, out.misses, out.invalidations);
  out.cache_blocks = dynarec.CacheSize();
  return out;
}

} // namespace nba::core

auto CreateCore(
  std::shared_ptr<Config> config
) -> std::unique_ptr<CoreBase> {
  return std::make_unique<core::Core>(config);
}

} // namespace nba
