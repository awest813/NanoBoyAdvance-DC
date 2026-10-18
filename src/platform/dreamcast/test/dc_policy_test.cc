// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host checks for settings dirty-tracking, Speed dynarec linkage, VMU state
// folder rejection, auto frame skip, ROM path matching, and pause-clock resync.

#include "dc_auto_frameskip.hh"
#include "dc_config.hh"
#include "dc_paths.hh"

#include <nba/rom/backup/backup_file.hh>
#include <platform/frame_limiter.hh>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void expect_true(const char* label, bool value) {
  if(!value) {
    std::printf("FAIL %s\n", label);
    failures++;
  }
}

void expect_eq(const char* label, int expected, int actual) {
  if(expected != actual) {
    std::printf("FAIL %s: expected %d, got %d\n", label, expected, actual);
    failures++;
  }
}

void expect_str(const char* label, std::string const& expected, std::string const& actual) {
  if(expected != actual) {
    std::printf("FAIL %s: expected '%s', got '%s'\n", label, expected.c_str(), actual.c_str());
    failures++;
  }
}

auto WriteToml(std::string const& path, std::string const& body) -> bool {
  std::ofstream file{path};
  if(!file) {
    return false;
  }
  file << body;
  return file.good();
}

void test_settings_differ_includes_dynarec() {
  using nba::DreamcastConfig;
  DreamcastConfig saved;
  saved.ApplyDefaults();
  DreamcastConfig draft = saved;
  expect_true("identical settings", !draft.SettingsDiffer(saved));

  draft.cpu_dynarec = !draft.cpu_dynarec;
  expect_true("cpu_dynarec is dirty", draft.SettingsDiffer(saved));

  draft = saved;
  draft.cpu_dynarec_on_speed = !draft.cpu_dynarec_on_speed;
  expect_true("cpu_dynarec_on_speed is dirty", draft.SettingsDiffer(saved));
}

void test_speed_dynarec_toggles_stay_linked() {
  using nba::DreamcastConfig;
  DreamcastConfig config;
  config.ApplyPerformanceProfile(DreamcastConfig::PerformanceProfile::Speed);
  config.cpu_dynarec_on_speed = true;
  config.cpu_dynarec = true;

  config.ToggleCpuDynarec();
  expect_true("toggle off clears live dynarec", !config.cpu_dynarec);
  expect_true("toggle off clears speed opt-in", !config.cpu_dynarec_on_speed);

  config.ApplyPerformanceProfile(DreamcastConfig::PerformanceProfile::Speed);
  expect_true("reapplying Speed does not force dynarec back on", !config.cpu_dynarec);

  config.ToggleCpuDynarecOnSpeed();
  expect_true("opt-in on enables live dynarec", config.cpu_dynarec);
  expect_true("opt-in flag is on", config.cpu_dynarec_on_speed);
}

void test_vmu_state_folder_is_rewritten() {
  using nba::DreamcastConfig;
  const std::string path = "/tmp/nba-dc-policy-test.toml";

  expect_true(
    "write opt-in toml",
    WriteToml(
      path,
      "[dreamcast]\n"
      "performance_profile = \"Speed\"\n"
      "cpu_dynarec = false\n"
      "cpu_dynarec_on_speed = true\n"
      "state_folder = \"/vmu/a1\"\n"
    )
  );

  DreamcastConfig forced;
  expect_eq(
    "load opt-in",
    static_cast<int>(nba::ConfigLoadResult::Loaded),
    static_cast<int>(forced.TryLoadDreamcast(path))
  );
  expect_true("speed opt-in forces dynarec", forced.cpu_dynarec);
  expect_true("speed opt-in flag kept", forced.cpu_dynarec_on_speed);
  expect_str("vmu state folder rewritten", DreamcastConfig::kDefaultStateFolder, forced.state_folder);

  expect_true(
    "write manual-off toml",
    WriteToml(
      path,
      "[dreamcast]\n"
      "performance_profile = \"Speed\"\n"
      "cpu_dynarec = false\n"
      "cpu_dynarec_on_speed = false\n"
      "state_folder = \"/pc\"\n"
    )
  );

  DreamcastConfig manual;
  expect_eq(
    "load manual-off",
    static_cast<int>(nba::ConfigLoadResult::Loaded),
    static_cast<int>(manual.TryLoadDreamcast(path))
  );
  expect_true("manual dynarec off sticks", !manual.cpu_dynarec);
  expect_str("pc state folder kept", "/pc", manual.state_folder);

  std::remove(path.c_str());
}

void test_auto_frame_skip_does_not_write_baseline() {
  using nba::DreamcastConfig;
  using nba::UpdateAutoFrameSkip;

  DreamcastConfig config;
  config.auto_frame_skip = true;
  config.frame_skip = 1;
  config.performance_profile = DreamcastConfig::PerformanceProfile::Balanced;
  int recovery = 0;

  const int lowered = UpdateAutoFrameSkip(config, 30.0f, 2, recovery, 0.0);
  expect_eq("ef cap lowers active skip", 1, lowered);
  expect_eq("baseline frame_skip unchanged", 1, config.frame_skip);

  recovery = 0;
  const int raised = UpdateAutoFrameSkip(config, 40.0f, 0, recovery, 0.0);
  expect_eq("low fps raises active skip", 1, raised);
  expect_eq("baseline still unchanged", 1, config.frame_skip);

  config.performance_profile = DreamcastConfig::PerformanceProfile::Speed;
  recovery = 0;
  const int headroom = UpdateAutoFrameSkip(config, 30.0f, 1, recovery, 10.0);
  expect_eq("speed headroom lowers active skip", 0, headroom);
  expect_eq("baseline unchanged after headroom", 1, config.frame_skip);
}

void test_rom_path_ignores_iso_version() {
  using nba::SameDreamcastROMPath;
  expect_true("version suffix matches", SameDreamcastROMPath("/cd/GAME.GBA", "/cd/GAME.GBA;1"));
  expect_true("identical paths match", SameDreamcastROMPath("/pc/roms/a.gba", "/pc/roms/a.gba"));
  expect_true("different roms do not match", !SameDreamcastROMPath("/cd/GAME.GBA", "/cd/OTHER.GBA"));
}

void test_dreamcast_save_adopts_larger_valid_size() {
  const std::string path = "/pc/nba-dc-policy-flash.sav";
  if(::mkdir("/pc", 0755) != 0 && errno != EEXIST) {
    std::printf("SKIP larger-save adopt (/pc not writable)\n");
    return;
  }

  constexpr size_t kLarge = 131072;
  {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if(!file) {
      std::printf("SKIP larger-save adopt (cannot create %s)\n", path.c_str());
      return;
    }
    std::vector<char> data(kLarge, static_cast<char>(0xA5));
    data.back() = static_cast<char>(0x5A);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    if(!file.good()) {
      std::printf("SKIP larger-save adopt (short write)\n");
      std::remove(path.c_str());
      return;
    }
  }

  int bytes = 65536;
  auto save = nba::BackupFile::OpenOrCreate(path, {65536, 131072}, bytes);
  expect_eq("adopted byte count", static_cast<int>(kLarge), bytes);
  expect_eq("adopted buffer size", static_cast<int>(kLarge), static_cast<int>(save->Size()));
  expect_eq("first adopted byte", 0xA5, save->Read(0));
  expect_eq("last adopted byte", 0x5A, save->Read(static_cast<unsigned>(kLarge - 1)));
  save.reset();
  std::remove(path.c_str());
}

void test_frame_limiter_resync_drops_pause() {
  using namespace std::chrono_literals;
  nba::FrameLimiter skewed(60.0f);
  float skewed_fps = -1.0f;
  skewed.Run(
    []() { std::this_thread::sleep_for(1200ms); },
    [&](float fps) { skewed_fps = fps; }
  );
  expect_true("unpaused long frame reports fps", skewed_fps >= 0.0f);
  expect_true("unpaused long frame collapses fps", skewed_fps < 5.0f);

  nba::FrameLimiter synced(60.0f);
  bool called = false;
  synced.Run(
    [&]() {
      std::this_thread::sleep_for(1200ms);
      synced.Resync();
    },
    [&](float) { called = true; }
  );
  expect_true("resync drops the pause sample", !called);
}

} // namespace

int main() {
  test_settings_differ_includes_dynarec();
  test_speed_dynarec_toggles_stay_linked();
  test_vmu_state_folder_is_rewritten();
  test_auto_frame_skip_does_not_write_baseline();
  test_rom_path_ignores_iso_version();
  test_dreamcast_save_adopts_larger_valid_size();
  test_frame_limiter_resync_drops_pause();

  if(failures != 0) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }

  std::printf("dc-policy-test: ok\n");
  return 0;
}
