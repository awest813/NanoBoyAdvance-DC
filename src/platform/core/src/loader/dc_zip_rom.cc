// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(PLATFORM_DREAMCAST)

#include <platform/loader/dc_zip_rom.hh>
#include <platform/loader/dc_virtual_fs.hh>

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace nba {

namespace {

constexpr size_t kMaxROMSize = 32 * 1024 * 1024;
constexpr char kZipCacheDir[] = "/pc/roms/.cache";

auto NormalizeExtension(std::string extension) -> std::string {
  for(auto& character : extension) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return extension;
}

auto IsROMEntryName(char const* filename) -> bool {
  if(!filename) {
    return false;
  }

  const auto* dot = std::strrchr(filename, '.');
  if(!dot) {
    return false;
  }

  const auto extension = NormalizeExtension(dot);
  return extension == ".gba" || extension == ".bin";
}

auto OpenZipArchive(fs::path const& path, mz_zip_archive& archive) -> bool {
  for(auto const& candidate : DreamcastVirtualPathCandidates(path)) {
    auto* file = std::fopen(candidate.c_str(), "rb");
    if(!file) {
      continue;
    }

    if(mz_zip_reader_init_cfile(&archive, file, 0, 0)) {
      return true;
    }

    std::fclose(file);
  }
  return false;
}

auto FindROMEntryIndex(mz_zip_archive& archive, mz_zip_archive_file_stat& stat) -> int {
  const auto file_count = static_cast<int>(mz_zip_reader_get_num_files(&archive));
  for(int index = 0; index < file_count; index++) {
    if(!mz_zip_reader_file_stat(&archive, static_cast<mz_uint>(index), &stat)) {
      continue;
    }

    if(IsROMEntryName(stat.m_filename)) {
      return index;
    }
  }

  return -1;
}

auto EnsureZipCacheDir() -> bool {
  return ::mkdir(kZipCacheDir, 0755) == 0 || errno == EEXIST;
}

auto CachePathForZip(fs::path const& zip_path) -> fs::path {
  auto stem = zip_path.stem().string();
  for(auto& character : stem) {
    if(character == '/' || character == '\\' || character == ':') {
      character = '_';
    }
  }

  return fs::path{kZipCacheDir} / (stem + ".gba");
}

auto CacheIsValid(fs::path const& cache_path, size_t expected_size) -> bool {
  size_t cache_size = 0;
  if(!GetDreamcastVirtualFileSize(cache_path, cache_size, kMaxROMSize)) {
    return false;
  }

  return cache_size == expected_size;
}

} // namespace

auto IsDreamcastZipROM(fs::path const& path) -> bool {
  return NormalizeExtension(path.extension().string()) == ".zip";
}

auto GetDreamcastZipROMSize(fs::path const& zip_path, size_t& rom_size) -> bool {
  rom_size = 0;

  mz_zip_archive archive {};
  if(!OpenZipArchive(zip_path, archive)) {
    return false;
  }

  mz_zip_archive_file_stat stat {};
  const int index = FindROMEntryIndex(archive, stat);
  const bool found = index >= 0 && stat.m_uncomp_size > 0 &&
                     stat.m_uncomp_size <= kMaxROMSize;

  if(found) {
    rom_size = static_cast<size_t>(stat.m_uncomp_size);
  }

  mz_zip_reader_end(&archive);
  return found;
}

auto ResolveDreamcastZipROM(fs::path const& zip_path, fs::path& rom_path) -> bool {
  rom_path.clear();

  mz_zip_archive archive {};
  if(!OpenZipArchive(zip_path, archive)) {
    return false;
  }

  mz_zip_archive_file_stat stat {};
  const int index = FindROMEntryIndex(archive, stat);
  if(index < 0 || stat.m_uncomp_size == 0 || stat.m_uncomp_size > kMaxROMSize) {
    mz_zip_reader_end(&archive);
    return false;
  }

  const auto cache_path = CachePathForZip(zip_path);
  const auto expected_size = static_cast<size_t>(stat.m_uncomp_size);

  if(CacheIsValid(cache_path, expected_size)) {
    mz_zip_reader_end(&archive);
    rom_path = cache_path;
    std::printf(
      "[NBA-DC] ZIP cache hit: %s -> %s (%lu bytes)\n",
      zip_path.string().c_str(),
      cache_path.string().c_str(),
      static_cast<unsigned long>(expected_size)
    );
    std::fflush(stdout);
    return true;
  }

  if(!EnsureZipCacheDir()) {
    mz_zip_reader_end(&archive);
    return false;
  }

  const auto cache_string = cache_path.string();
  if(!mz_zip_reader_extract_to_file(
        &archive,
        static_cast<mz_uint>(index),
        cache_string.c_str(),
        0)) {
    mz_zip_reader_end(&archive);
    return false;
  }

  mz_zip_reader_end(&archive);

  if(!CacheIsValid(cache_path, expected_size)) {
    return false;
  }

  rom_path = cache_path;
  std::printf(
    "[NBA-DC] ZIP extracted: %s -> %s (%lu bytes)\n",
    zip_path.string().c_str(),
    cache_path.string().c_str(),
    static_cast<unsigned long>(expected_size)
  );
  std::fflush(stdout);
  return true;
}

} // namespace nba

#endif
