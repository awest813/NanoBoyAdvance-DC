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

} // namespace nba

#endif
