// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fstream>
#include <platform/writer/save_state.hh>

#if defined(PLATFORM_DREAMCAST)
#include <platform/loader/dc_virtual_fs.hh>
#endif

namespace nba {

auto SaveStateWriter::Write(
  std::unique_ptr<CoreBase>& core,
  fs::path const& path
) -> Result {
  SaveState save_state;
  core->CopyState(save_state);

#if defined(PLATFORM_DREAMCAST)
  if(IsDreamcastVirtualPath(path)) {
    auto* file = OpenDreamcastVirtualFile(path, "wb");
    if(!file) {
      return Result::CannotOpenFile;
    }

    const auto written = std::fwrite(&save_state, 1, sizeof(SaveState), file);
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;

    if(written != sizeof(SaveState) || !flushed || !closed) {
      return Result::CannotWrite;
    }

    return Result::Success;
  }
#endif

  std::ofstream file_stream{path.c_str(), std::ios::binary};

  if(!file_stream.good()) {
    return Result::CannotOpenFile;
  }

  file_stream.write((const char*)&save_state, sizeof(SaveState));

  if(!file_stream.good()) {
    return Result::CannotWrite;
  }

  return Result::Success;
}

} // namespace nba
