// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/integer.hh>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace nba {

struct BackupFile {
  static auto OpenOrCreate(fs::path const& save_path,
                           std::vector<size_t> const& valid_sizes,
                           int& default_size) -> std::unique_ptr<BackupFile> {
    bool create = true;
    auto flags = std::ios::binary | std::ios::in | std::ios::out;
    std::unique_ptr<BackupFile> file { new BackupFile() };
    file->path = save_path;

#if defined(PLATFORM_DREAMCAST)
    const auto save_path_string = save_path.string();
    if(save_path_string.rfind("/pc/", 0) == 0 || save_path_string.rfind("/vmu/", 0) == 0) {
      // On Dreamcast virtual paths std::filesystem and std::fstream are
      // unreliable through KOS; use POSIX fopen/fwrite instead.
      file->save_size = static_cast<size_t>(default_size);
      file->memory.reset(new u8[default_size]);
      std::memset(file->memory.get(), 0xFF, default_size);
      file->auto_update = false;

      bool loaded_existing = false;
      if(auto* f = std::fopen(save_path_string.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        const long fsz = std::ftell(f);
        if(fsz > 0) {
          const size_t save_size = static_cast<size_t>(fsz) & ~size_t(63);
          const auto begin = valid_sizes.begin();
          const auto end   = valid_sizes.end();
          if(std::find(begin, end, save_size) != end) {
            std::fseek(f, 0, SEEK_SET);
            std::fread(file->memory.get(), 1, save_size, f);
            file->save_size = save_size;
            default_size = static_cast<int>(save_size);
            loaded_existing = true;
          }
        }
        std::fclose(f);
      }

      auto open_for_write_back = [&]() -> bool {
        if(loaded_existing) {
          file->posix_file = std::fopen(save_path_string.c_str(), "rb+");
        } else {
          if(auto* create_file = std::fopen(save_path_string.c_str(), "wb")) {
            const size_t written = std::fwrite(
              file->memory.get(),
              1,
              file->save_size,
              create_file
            );
            const bool flushed = std::fflush(create_file) == 0;
            const bool closed = std::fclose(create_file) == 0;
            if(written != file->save_size || !flushed || !closed) {
              return false;
            }
          } else {
            return false;
          }

          file->posix_file = std::fopen(save_path_string.c_str(), "rb+");
        }

        return file->posix_file != nullptr;
      };

      if(open_for_write_back()) {
        file->auto_update = true;
      }

      return file;
    }
#endif

    // @todo: check file type and permissions?
    if(fs::is_regular_file(save_path)) {
      auto file_size = fs::file_size(save_path);

      // allow for some extra/unused data; required for mGBA save compatibility
      auto save_size = file_size & ~63u;

      auto begin = valid_sizes.begin();
      auto end = valid_sizes.end();

      if(std::find(begin, end, save_size) != end) {
        file->stream.open(save_path.c_str(), flags);
        if(file->stream.fail()) {
          throw std::runtime_error("BackupFile: unable to open file: " + save_path.string());
        }
        default_size = save_size;
        file->save_size = save_size;
        file->memory.reset(new u8[file_size]);
        file->stream.read((char*)file->memory.get(), file_size);
        create = false;
      }
    }

    /* A new save file is created either when no file exists yet,
     * or when the existing file has an invalid size.
     */
    if(create) {
      file->save_size = default_size;
      file->stream.open(save_path, flags | std::ios::trunc);
      if(file->stream.fail()) {
        throw std::runtime_error("BackupFile: unable to create file: " + save_path.string());
      }
      file->memory.reset(new u8[default_size]);
      file->MemorySet(0, default_size, 0xFF);
    }

    return file;
  }

  ~BackupFile() {
    if(posix_file) {
      std::fclose(posix_file);
      posix_file = nullptr;
    }
  }

  BackupFile(BackupFile const&) = delete;
  auto operator=(BackupFile const&) -> BackupFile& = delete;

  auto Read(unsigned index) -> u8 {
    if(index >= save_size) {
      throw std::runtime_error("BackupFile: out-of-bounds index while reading.");
    }
    return memory[index];
  }

  void Write(unsigned index, u8 value) {
    if(index >= save_size) {
      throw std::runtime_error("BackupFile: out-of-bounds index while writing.");
    }
    memory[index] = value;
    if(auto_update) {
      Update(index, 1);
    }
  }

  void MemorySet(unsigned index, size_t length, u8 value) {
    if((index + length) > save_size) {
      throw std::runtime_error("BackupFile: out-of-bounds index while setting memory.");
    }
    std::memset(&memory[index], value, length);
    if(auto_update) {
      Update(index, length);
    }
  }

  void Update(unsigned index, size_t length) {
    if((index + length) > save_size) {
      throw std::runtime_error("BackupFile: out-of-bounds index while updating file.");
    }

    if(posix_file) {
      if(std::fseek(posix_file, static_cast<long>(index), SEEK_SET) != 0) {
        return;
      }
      std::fwrite(&memory[index], 1, length, posix_file);
      return;
    }

    stream.seekp(static_cast<std::streamoff>(index));
    stream.write(reinterpret_cast<const char*>(&memory[index]), static_cast<std::streamsize>(length));
  }

  auto Buffer() -> u8* {
    return memory.get();
  }

  auto Size() -> size_t {
    return save_size;
  }

  // Whether save writes are streamed straight to disk.  This is false on
  // platforms/media where the backing stream could not be opened for writing
  // (e.g. Flycast's read-only /pc/ stream), in which case the save lives only
  // in memory until Flush() is called at a quiet point such as session exit.
  auto IsPersistent() const -> bool {
    return auto_update;
  }

  // Best-effort write of the whole save buffer to disk.  For persistent
  // sessions this just flushes the open stream; for in-memory-only sessions it
  // makes a single clean fopen("wb") attempt, which can succeed at exit even
  // when the per-write streaming path failed earlier.  Returns true if the
  // save is now safely on disk.
  auto Flush() -> bool {
    if(auto_update) {
      if(posix_file) {
        return std::fflush(posix_file) == 0;
      }

      stream.flush();
      return stream.good();
    }

    if(path.empty() || !memory) {
      return false;
    }

    auto* f = std::fopen(path.string().c_str(), "wb");
    if(!f) {
      return false;
    }

    const size_t written = std::fwrite(memory.get(), 1, save_size, f);
    const bool flushed = std::fflush(f) == 0;
    const bool closed = std::fclose(f) == 0;
    return written == save_size && flushed && closed;
  }

  bool auto_update = true;

private:
  BackupFile() = default;

  fs::path path;
  size_t save_size = 0;
  std::fstream stream;
  FILE* posix_file = nullptr;
  std::unique_ptr<u8[]> memory;
};

} // namespace nba
