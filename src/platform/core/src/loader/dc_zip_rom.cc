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
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace nba {

namespace {

constexpr size_t kMaxROMSize = 32 * 1024 * 1024;
constexpr char kZipCacheDir[] = "/pc/roms/.cache";
constexpr size_t kZipCacheMaxBytes = 256 * 1024 * 1024;

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
  ::mkdir("/pc/roms", 0755);
  return ::mkdir(kZipCacheDir, 0755) == 0 || errno == EEXIST;
}

struct CacheFileInfo {
  std::string name;
  size_t size = 0;
  time_t mtime = 0;
};

auto ScanZipCache(std::vector<CacheFileInfo>& files) -> void {
  if(auto* dir = ::opendir(kZipCacheDir)) {
    while(auto* entry = ::readdir(dir)) {
      if(std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      const auto filename = std::string{kZipCacheDir} + "/" + entry->d_name;
      struct stat st {};
      if(::stat(filename.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        files.push_back(CacheFileInfo{filename, static_cast<size_t>(st.st_size), st.st_mtime});
      }
    }
    ::closedir(dir);
  }
}

auto EvictOldestZipCacheEntries(size_t bytes_to_keep) -> void {
  std::vector<CacheFileInfo> files;
  ScanZipCache(files);

  size_t total_bytes = 0;
  for(auto const& f : files) {
    total_bytes += f.size;
  }

  if(total_bytes + bytes_to_keep <= kZipCacheMaxBytes) {
    return;
  }

  std::sort(files.begin(), files.end(), [](CacheFileInfo const& a, CacheFileInfo const& b) {
    return a.mtime < b.mtime;
  });

  const size_t target = total_bytes - std::min(total_bytes, kZipCacheMaxBytes - bytes_to_keep);
  size_t evicted = 0;
  for(auto const& f : files) {
    if(evicted >= target) break;
    if(::unlink(f.name.c_str()) == 0) {
      std::printf("[NBA-DC] ZIP cache evict: %s (%lu bytes)\n",
                  f.name.c_str(), static_cast<unsigned long>(f.size));
      std::fflush(stdout);
      evicted += f.size;
    }
  }
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

  EvictOldestZipCacheEntries(expected_size);

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
