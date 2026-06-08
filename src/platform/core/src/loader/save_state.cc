// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/loader/save_state.hh>
#include <filesystem>
#include <fstream>

#if defined(PLATFORM_DREAMCAST)
#include <platform/loader/dc_virtual_fs.hh>
#endif

namespace nba {

auto SaveStateLoader::Load(
  std::unique_ptr<CoreBase>& core,
  fs::path const& path
) -> Result {
  SaveState save_state;

#if defined(PLATFORM_DREAMCAST)
  if(IsDreamcastVirtualPath(path)) {
    size_t file_size = 0;
    if(!GetDreamcastVirtualFileSize(path, file_size) ||
        file_size != sizeof(SaveState)) {
      return file_size == 0 ? Result::CannotFindFile : Result::BadImage;
    }

    auto* file = OpenDreamcastVirtualFile(path);
    if(!file) {
      return Result::CannotOpenFile;
    }

    const auto read = std::fread(&save_state, 1, sizeof(SaveState), file);
    std::fclose(file);

    if(read != sizeof(SaveState)) {
      return Result::CannotOpenFile;
    }
  } else
#endif
  {
    if(!fs::exists(path)) {
      return Result::CannotFindFile;
    }

    if(!fs::is_regular_file(path)) {
      return Result::CannotOpenFile;
    }

    auto file_size = fs::file_size(path);

    if(file_size != sizeof(SaveState)) {
      return Result::BadImage;
    }

    std::ifstream file_stream{path.c_str(), std::ios::binary};

    if(!file_stream.good()) {
      return Result::CannotOpenFile;
    }

    file_stream.read((char*)&save_state, sizeof(SaveState));
  }

  auto validate_result = ValidateImage(save_state);

  if(validate_result != Result::Success) {
    return validate_result;
  }

  core->LoadState(save_state);
  return Result::Success;
}

auto SaveStateLoader::ValidateImage(SaveState const& save_state) -> Result {
  if(save_state.magic != SaveState::kMagicNumber) {
    return Result::BadImage;
  }

  if(save_state.version != SaveState::kCurrentVersion) {
    return Result::UnsupportedVersion;
  }

  bool bad_image = false;

  {
    auto& waitcnt = save_state.bus.io.waitcnt;
    bad_image |= waitcnt.sram > 3;
    bad_image |= waitcnt.ws0[0] > 3;
    bad_image |= waitcnt.ws0[1] > 1;
    bad_image |= waitcnt.ws1[0] > 3;
    bad_image |= waitcnt.ws1[1] > 1;
    bad_image |= waitcnt.ws2[0] > 3;
    bad_image |= waitcnt.ws2[1] > 1;
    bad_image |= waitcnt.phi > 3;
  }

  bad_image |= save_state.ppu.io.vcount > 227;

  {
    auto& apu = save_state.apu;

    for(int i = 0; i < 2; i++) {
      bad_image |= apu.io.quad[i].phase > 7;
      bad_image |= apu.io.quad[i].wave_duty > 3;
      bad_image |= apu.fifo[i].count > 7;
    }

    bad_image |= apu.io.wave.phase > 31;
    bad_image |= apu.io.wave.wave_bank > 1;
    bad_image |= apu.io.noise.width > 1;
  }

  {
    auto& dma = save_state.dma;
    bad_image |= dma.hblank_set > 0b1111;
    bad_image |= dma.vblank_set > 0b1111;
    bad_image |= dma.video_set  > 0b1111;
    bad_image |= dma.runnable_set > 0b1111;
  }

  bad_image |= save_state.gpio.rtc.current_byte > 7;

  if(bad_image) {
    return Result::BadImage;
  }

  return Result::Success;
}

} // namespace nba
