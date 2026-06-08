// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if defined(PLATFORM_DREAMCAST)

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/stat.h>

#if __has_include(<arch/arch.h>)
#define NBA_DC_VFS_HAS_ARCH 1
#include <arch/arch.h>
#else
#define NBA_DC_VFS_HAS_ARCH 0
#endif

namespace fs = std::filesystem;

namespace nba {

inline auto IsDreamcastVirtualPath(fs::path const& path) -> bool {
  const auto path_string = path.string();
  return path_string.rfind("/cd/", 0) == 0 || path_string.rfind("/pc/", 0) == 0;
}

inline auto DreamcastVirtualPathCandidates(fs::path const& path) -> std::vector<std::string> {
  std::vector<std::string> candidates;
  const auto path_string = path.string();
  candidates.push_back(path_string);

  // ISO9660 discs may expose versioned names (e.g. "GAME.GBA;1").  Try the
  // suffix when the stripped path from the ROM browser does not open.
  if(path_string.rfind("/cd/", 0) == 0) {
    const auto filename = path.filename().string();
    if(filename.find(';') == std::string::npos) {
      candidates.emplace_back((path.parent_path() / (filename + ";1")).string());
    }
  }

  return candidates;
}

inline auto OpenDreamcastVirtualFile(fs::path const& path, const char* mode = "rb") -> std::FILE* {
  for(auto const& candidate : DreamcastVirtualPathCandidates(path)) {
    if(auto* file = std::fopen(candidate.c_str(), mode)) {
      return file;
    }
  }
  return nullptr;
}

inline auto ResolveDreamcastVirtualPath(fs::path const& path) -> std::string {
  for(auto const& candidate : DreamcastVirtualPathCandidates(path)) {
    if(auto* file = std::fopen(candidate.c_str(), "rb")) {
      std::fclose(file);
      return candidate;
    }
  }
  return path.string();
}

// Returns the byte length of a Dreamcast virtual file.  Uses stat() first
// because ISO9660 streams often reject fseek(SEEK_END).
inline auto GetDreamcastVirtualFileSize(
  fs::path const& path,
  size_t& file_size,
  size_t max_size = SIZE_MAX
) -> bool {
  file_size = 0;

  for(auto const& candidate : DreamcastVirtualPathCandidates(path)) {
    struct stat st {};
    if(::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0) {
      file_size = static_cast<size_t>(st.st_size);
      return file_size <= max_size;
    }
  }

  auto* file = OpenDreamcastVirtualFile(path);
  if(!file) {
    return false;
  }

  if(std::fseek(file, 0, SEEK_END) == 0) {
    const auto end = std::ftell(file);
    if(end >= 0) {
      file_size = static_cast<size_t>(end);
      std::fclose(file);
      return file_size > 0 && file_size <= max_size;
    }
  }

  if(std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    file = OpenDreamcastVirtualFile(path);
    if(!file) {
      return false;
    }
  }

  constexpr size_t kBufferSize = 64 * 1024;
  unsigned char buffer[kBufferSize];
  file_size = 0;

  while(true) {
    const auto read = std::fread(buffer, 1, kBufferSize, file);
    if(read == 0) {
      break;
    }

    file_size += read;
    if(file_size > max_size) {
      std::fclose(file);
      file_size = 0;
      return false;
    }
  }

  std::fclose(file);
  return file_size > 0;
}

inline auto HasExtendedRAM() -> bool {
#if NBA_DC_VFS_HAS_ARCH
  return DBL_MEM != 0;
#else
  return false;
#endif
}

inline auto DreamcastPagedROMPageCount(size_t rom_size) -> size_t {
  static constexpr size_t kLargeROMThreshold = 8 * 1024 * 1024;
  static constexpr size_t kSmallROMPageCount = 2;
  static constexpr size_t kLargeROMPageCountExtended = 4;
  static constexpr size_t kLargeROMPageCountStock = 2;

  if(rom_size <= kLargeROMThreshold) {
    return kSmallROMPageCount;
  }

  return HasExtendedRAM() ? kLargeROMPageCountExtended : kLargeROMPageCountStock;
}

inline auto ShouldUseFlatDreamcastROM(size_t rom_size) -> bool {
  static constexpr size_t kFlatROMLimit = 8 * 1024 * 1024;
  return HasExtendedRAM() && rom_size <= kFlatROMLimit;
}

inline auto ReadDreamcastTextFile(std::string const& path, std::string& content) -> bool {
  content.clear();

  auto* file = OpenDreamcastVirtualFile(fs::path{path});
  if(!file) {
    file = std::fopen(path.c_str(), "rb");
  }
  if(!file) {
    return false;
  }

  if(std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }

  const auto size = std::ftell(file);
  if(size <= 0) {
    std::fclose(file);
    return false;
  }

  content.resize(static_cast<size_t>(size));
  if(std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    content.clear();
    return false;
  }

  const auto read = std::fread(content.data(), 1, content.size(), file);
  std::fclose(file);
  if(read != content.size()) {
    content.clear();
    return false;
  }

  return true;
}

inline auto WriteDreamcastTextFile(std::string const& path, std::string const& content) -> bool {
  auto* file = std::fopen(path.c_str(), "wb");
  if(!file) {
    return false;
  }

  const auto written = content.empty()
    ? 0
    : std::fwrite(content.data(), 1, content.size(), file);
  const bool flushed = std::fflush(file) == 0;
  const bool closed = std::fclose(file) == 0;
  return written == content.size() && flushed && closed;
}

} // namespace nba

#endif
