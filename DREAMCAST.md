# NanoBoyAdvance – Dreamcast Port

This document describes how to build and run NanoBoyAdvance on the Sega Dreamcast using [DreamSDK](https://www.dreamsdk.org/) and [KallistiOS](http://gamedev.allusions.net/softprj/kos/).

## Prerequisites

- **DreamSDK** (Windows) or a manual KallistiOS toolchain (Linux/macOS)
- **sh-elf GCC** toolchain with C++20 support (GCC 10+ recommended)
- **CMake** 3.24 or newer
- **Ninja** (recommended generator)

## Setting Up the Environment

### DreamSDK (Windows)

1. Install DreamSDK from <https://www.dreamsdk.org/>
2. Open the DreamSDK Shell
3. The environment variables (`KOS_BASE`, `KOS_CC_BASE`, etc.) are set automatically

### Manual KallistiOS (Linux/macOS)

1. Build and install KallistiOS per its [documentation](http://gamedev.allusions.net/softprj/kos/)
2. Source the environment script:
   ```sh
   source /opt/toolchains/dc/kos/environ.sh
   ```

## Building

From the repository root:

```sh
cmake -B build-dc -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dreamcast.cmake \
  -DPLATFORM_DREAMCAST=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-dc
```

The output binary is `build-dc/bin/dreamcast/Release/NanoBoyAdvance`.

### Creating a Bootable CD Image

Use KallistiOS `makeip` and `mkisofs` tools:

```sh
# Create the IP.BIN bootstrap
makeip ip.txt IP.BIN

# Scramble the Dreamcast executable to 1ST_READ.BIN
sh-elf-objcopy -O binary build-dc/bin/dreamcast/Release/NanoBoyAdvance NanoBoyAdvance.bin
/opt/toolchains/dc/kos/utils/scramble/scramble NanoBoyAdvance.bin 1ST_READ.BIN

# Create disc layout directory
mkdir -p cd_root
cp 1ST_READ.BIN cd_root/
cp your_bios.bin cd_root/bios.bin

# Optional: place ROMs on disc root or use /pc/roms on writable media
mkdir -p pc_root/roms pc_root/saves
cp your_rom.gba pc_root/roms/

# Create the ISO (adjust layout for your loader/ODE workflow)
mkisofs -C 0,11702 -V "NBA_DC" -G IP.BIN -l -o nba-dc.iso cd_root/
```

## File Layout

| File / Folder | Path | Description |
|---------------|------|-------------|
| GBA BIOS | `/cd/bios.bin` or `/pc/bios.bin` | Required. Configurable in settings |
| GBA ROMs | `/pc/roms/*.{gba,bin,zip}` or `/cd/*.{gba,bin,zip}` or `/cd/gbaDC/` | Scanned by the in-app ROM browser (gpSPDC-style `gbaDC` folder included) |
| Save data | `/pc/saves/<rom>.sav` or `/vmu/a1/NBA########` | Per-ROM backup saves (configurable folder) |
| Save states | `/pc/states/<rom>.ss0`–`.ss9` | 10 slots per ROM (configurable folder) |
| Cheat files | `<rom>.cht` beside ROM or `/cd/gbaDC/` | gpSP-compatible GameShark / PAR `.cht` |
| Idle-loop hints | `/cd/gbaDC/game_config.txt`, `/pc/game_config.txt` | gpSP-compatible `idle_loop_eliminate_target` entries |
| ZIP cache | `/pc/roms/.cache/<stem>.gba` | Extracted ROMs from `.zip` archives (created on first load) |
| Settings | `/pc/nba-dc.toml` | Frame skip, audio buffer, path overrides |

Writable `/pc` paths require an SD/IDE adapter or equivalent host filesystem mount.

### Save Policy

- Backup saves are written to the writable filesystem (`/pc/saves` by default).
- Each ROM gets its own `<rom-stem>.sav` file.
- The save directory is created automatically with `mkdir` on first use.
- Selecting **Save folder: `/vmu/a1`** stores saves on the first VMU in the
  first controller. VMU files use compact `NBA########` filenames because the
  VMU filesystem cannot store long ROM names. Small EEPROM/SRAM saves are the
  best fit; 128 KiB FLASH saves may exceed available VMU capacity and fall back
  to in-memory progress if the VMU write fails.
- On real KallistiOS hardware the save file is opened and written through the
  KOS virtual filesystem; on Flycast (where the `/pc/` stream may not support
  writes) saves remain in-memory for the session only.
- Existing save files from previous sessions are loaded on startup even on
  Flycast if the file can be read via `fopen`.
- When streaming writes are unavailable (in-memory-only session), the emulator
  makes one clean full-buffer write to the save file when you exit back to the
  ROM browser.  This single `fopen("wb")` attempt can succeed at exit even when
  the per-write streaming path failed during play.  A message reports whether
  your progress was persisted (`Save Written`) or could not be saved
  (`Save Not Written`); the latter suggests pointing the save folder at a
  writable `/pc` location.

### ROM Loading

- Large ROMs are loaded using a paged file cache rather than allocating the full
  cartridge into main RAM. ROMs up to 8 MiB use two active 1 MiB pages; larger
  ROMs use four active 1 MiB pages with LRU eviction.
- Page 0 is preloaded during ROM attach so the first CPU instruction fetch
  does not block on CD I/O.
- ROM validation reads only the 228-byte GBA header; the full file is never
  read into memory during browser scanning or pre-launch validation.
- Backup type is detected by scanning the ROM file in 64 KiB chunks for save
  signature strings when the game is not in the built-in database.
- ROMs over 8 MiB are blocked on stock 16 MB hardware unless **Large ROMs
  (>8 MiB)** is enabled in Settings.  On a 32 MB RAM mod (or Flycast with
  `RamMod32MB`), KallistiOS reports extended RAM via `DBL_MEM` and large ROMs
  are allowed automatically.
- The ROM browser shows each cartridge size and marks titles that exceed the
  stock 8 MiB limit on the current RAM configuration with `[Needs Large ROMs]`.
  Selecting such a title does not launch it; the browser shows a message
  explaining that the **Large ROMs** setting (or a 32 MB RAM mod) is required.
  Enabling **Large ROMs** in Settings and returning rescans the list, clearing
  the tag for ROMs that now fit.
- `.zip` ROMs are opened with [miniz](https://github.com/richgel999/miniz)
  (public domain) and streamed from disc without buffering the whole archive in
  RAM.  The inner `.gba`/`.bin` is extracted once to `/pc/roms/.cache/` and
  then loaded through the same paged-ROM path as a flat file.
- Optional `game_config.txt` entries (same format as gpSP) can list
  `idle_loop_eliminate_target` addresses per `game_code`.  When a match is
  found, the core halts at that PC until the next IRQ instead of burning cycles
  in the idle loop.
- After each paged-ROM cache miss, the next 1 MiB page is prefetched when
  possible to hide sequential CD read latency.

### RAM Budget (Stock 16 MB Dreamcast)

| Consumer | Size | Notes |
|----------|------|-------|
| GBA memory (WRAM+IRAM+VRAM+PRAM+OAM) | ~386 KiB | Core emulator state |
| PVR texture staging + VRAM | ~164 KiB | 256×160×2 bytes × 2 |
| Paged ROM cache (large ROMs) | 2 MiB | 2 pages × 1 MiB; 4 pages on 32 MB mod |
| Audio ring buffer | 8–16 KiB | snd_stream buffer |
| Code, static data, stacks | ~300 KiB | .text, .bss, heap |
| BIOS, save states, cheats, misc | ~100 KiB | Scratch allocations |
| **Total overhead** | **~3 MiB** | |
| **Available for ROM data** | **~13 MiB** | |

On stock hardware, large ROMs (>8 MiB) use a 2-page LRU cache because
allocating the full ROM into RAM would exhaust available memory.  The
`[Needs Large ROMs]` tag in the browser flags titles that cannot run on the
current configuration unless a 32 MB RAM mod or **Large ROMs** setting is
active.  Even with the setting enabled, heavy page thrashing is expected
for 16 MiB cartridges.  The page-miss counter (`PG` in the FPS overlay)
helps gauge cache pressure: a steadily climbing `PG` value indicates the
cache is undersized for the current game.

### Flycast Testing Notes

- The ROM browser enumerates `/cd` using POSIX `opendir`/`readdir`.  On real
  KallistiOS hardware this works correctly.  On Flycast, `opendir("/cd")` may
  behave differently; if the disc root appears empty, copy your `.gba` files
  to `/pc/roms` and point the ROM folder setting there instead.
- ISO9660 version suffixes (e.g. `GAME.GBA;1`) in filenames are stripped
  automatically from both display labels and file paths.

## Frontend Controls

### ROM Browser

| Button | Action |
|--------|--------|
| D-Pad / Analog | Move selection |
| A | Launch selected ROM |
| B | Return to loader |
| Y | Open settings |
| Start | Return to loader |

### Settings

| Button | Action |
|--------|--------|
| D-Pad Up/Down | Select row |
| D-Pad Left/Right | Adjust value |
| A | Save and return (on last row) |
| B | Cancel without saving |

Configurable options:

- **Performance** (`Accuracy` / `Balanced` / `Speed` — see Performance Profiles below)
- **Show FPS** (`On` / `Off` — overlays the measured frame rate during play)
- **Large ROMs** (`On` / `Off` — allows >8 MiB ROMs for 32 MB mod testing)
- **Frame skip** (`Auto` or 0–3 extra emulated frames per display frame)
- **Audio buffer** (2048 / 4096 / 8192 bytes — lower = less latency, higher = safer)
- **BIOS path** (`/cd/bios.bin` or `/pc/bios.bin`)
- **ROM folder** (`/pc/roms` or `/cd`)
- **Save folder** (`/pc/saves`, `/pc`, or `/vmu/a1`)

Selecting a **Performance** profile rewrites the audio/video/frame-skip knobs,
disables Auto frame skip, and applies that profile's preset; you can then
fine-tune Frame skip and Audio buffer afterward without losing the rest of the
preset.

When **Frame skip** is set to **Auto**, the runtime raises the active skip value
when measured FPS drops below the full-speed target and slowly lowers it after
several stable samples. On the **Speed** profile, auto frame skip reacts one FPS
point sooner and recovers after two stable samples instead of three. The FPS
overlay shows this as `FSA<n>`; manual mode is shown as `FS <n>`.

### Settings Persistence

- Settings are saved to `/pc/nba-dc.toml`:
  - when you choose **Save and return** in the settings menu, and
  - automatically right before a ROM launches, so the **last played ROM**
    (`last_rom`) is remembered and pre-selected in the browser next time.
- Both saves use a guarded one-shot writer: the config is serialized to memory
  and written with a single `fopen("wb")`. It avoids `std::filesystem` and the
  read-modify-write (re-parsing the existing file) that the regular save path
  performs, so it will not hang on Flycast's `/pc/` virtual filesystem.
- At startup the emulator loads that file with a matching guarded reader: it
  probes the file with `fopen` and parses it from memory, so it never calls
  `std::filesystem` and never writes a new file on a miss. This keeps saved
  settings across reboots without risking a Flycast `/pc/` hang.
- If the file is missing or malformed, the emulator silently falls back to
  defaults; the `[NBA-DC] Config:` lines on stdout report which path was taken
  and whether each save succeeded.

## Hardware Mapping (Gameplay)

| Dreamcast Button | GBA Button |
|-----------------|------------|
| A               | A          |
| B               | B          |
| X               | L          |
| Y               | R          |
| Start           | Start      |
| D (unused btn)  | Select     |
| D-Pad           | D-Pad      |
| Analog Stick    | D-Pad (dead zone: 32, ~25% of ±127 range) |

**Exit combo during gameplay**: Hold Start + A + B + X + Y for ~1 second to return to the ROM browser.

**Pause menu**: Hold **Start + B** for ~⅓ second to open the in-game pause menu. From there you can resume, save/load states, pick a slot, toggle cheats, view the **Controls** help screen, **Reset game**, or exit to the ROM browser. Reset and exit both prompt for confirmation (defaulting to **No**) so a stray button press cannot discard unsaved progress.

**Save-state shortcuts** (also available from the pause menu):

| Combo | Action |
|-------|--------|
| L + R + Start | Save to the active slot |
| L + R + Select | Load from the active slot |
| L + R + D-Pad Left/Right | Change the active slot (0–9) |

Dreamcast **X** and **Y** map to GBA **L** and **R** during play, so hold both shoulder buttons together with Start or Select. A short on-screen message confirms save/load results.

### SDL host menu fallback

Non-cross Dreamcast builds can optionally link SDL3 when it is installed on the
host. This is intended for menu smoke testing without KallistiOS hardware.

| Key | Menu action |
|-----|-------------|
| Arrow keys | Move / adjust |
| Return / Space / Z | Confirm |
| Escape / Backspace / X | Cancel |
| Y / S | Settings |
| F1 / Tab | Start |

During host gameplay smoke tests, Escape opens the pause menu, F5/F8 save/load
states, PageUp/PageDown adjust the state slot, and Q exits to the browser.

### Cheats

Place a gpSP-style `.cht` file next to the ROM (same base name) or under `/cd/gbaDC/`. Supported headers include `PAR_v3`, `gameshark_v3`, and the older v1/v2 variants. Open the pause menu, choose **Cheats**, and toggle entries on or off with **A** or **Left/Right**. Enabled cheats are applied once per emulated frame before the core runs.

Errors and loading screens show on-screen text with path details. Press Start to dismiss fatal errors.

## Current Limitations

- **Cheats** – basic GameShark / PAR v1/v3 support; hook and ROM-patch codes are skipped
- **VMU capacity** – large 128 KiB FLASH saves may not fit on a VMU
- **No post-processing** – color correction and xBRZ upscaling are disabled (LCD ghosting is enabled only by the Accuracy profile). Gameplay frames use PVR nearest-neighbor 2× scaling; shader-based filters remain unavailable.
- **Single-threaded** – the emulation loop runs on the main thread

## Architecture

```
src/platform/dreamcast/
├── CMakeLists.txt
└── src/
    ├── main.cc                    # Frontend loop + emulation sessions
    ├── dc_config.hh/cc            # Dreamcast settings (TOML)
    ├── dc_ui.hh/cc                # On-screen menus and overlays
    ├── dc_frontend.hh/cc          # ROM browser + settings menus
    ├── dc_rom_browser.hh/cc       # ROM directory scanning
    ├── dc_paths.hh                # Save path helpers
    └── device/
        ├── dc_video_device.hh/cc  # PVR hardware-scaled gameplay output
        ├── dc_audio_device.hh/cc  # KOS snd_stream audio output
        └── dc_input.hh/cc         # Maple controller input polling
```

The Dreamcast backend reuses the core emulator library (`nba`) and the platform-core library (loaders, config, frame limiter) while providing its own device implementations.

## PPU / GPU Performance Plan

Detailed phased planning for software PPU and PVR presentation work lives in
[`PPU_GPU_OVERHAUL.md`](PPU_GPU_OVERHAUL.md). That document inventories completed
optimizations (PVR scale, frame skip, LUT conversion, etc.) and tracks the next
phases: RGB565 merge output, fast raster paths, and PVR upload tuning.

## Performance Notes

The Dreamcast SH4 at 200 MHz is significantly slower than modern desktop CPUs. Expect performance challenges with cycle-accurate GBA emulation. Built-in tuning options:

- Performance profile (settings menu)
- Frame skip / Auto frame skip (settings menu)
- Audio buffer size (settings menu)
- **CPU dynarec** (experimental; settings menu / `cpu_dynarec` in `nba-dc.toml`) — see [`DYNAREC.md`](DYNAREC.md)
- SH4-specific compiler flags (`-m4-single-only` is already used)

### ARM Dynarec (experimental)

Phase 1 ships an opt-in Thumb IR recompiler (`cpu_dynarec = true`). It compiles
basic ALU / unconditional-branch blocks and executes them through an IR path that
still performs interpreter-equivalent pipeline fetches. Native SH4 emission is
Phase 2. Leave this **Off** unless you are testing dynarec; Accuracy and Balanced
profiles should keep the interpreter.

### Performance Profiles

The **Performance** setting selects a coherent preset of the accuracy/speed
knobs. Pick the highest-fidelity profile a given game can sustain at full speed.

| Profile      | Audio mixing   | Interpolation | Frame skip | Audio buffer | LCD ghosting |
|--------------|----------------|---------------|------------|--------------|--------------|
| **Accuracy** | Native (LLE)   | Sinc-64       | 0          | 8192         | On           |
| **Balanced** | Native (LLE)   | Cosine        | 0          | 4096         | Off          |
| **Speed**    | MP2K HLE       | Cosine        | Auto (0–3) | 8192         | Off          |

Speed-profile MP2K HLE uses linear resampling (no cubic filter), skips the ROM
`SoundMainRAM()` routine body after the host mixer runs, and leaves reverb at
each game's native strength instead of forcing a minimum level.

- **Accuracy** – closest to real GBA behavior; best for light 2D titles that
  already hold full speed and benefit from accurate audio.
- **Balanced** (default) – native audio with cheap interpolation and no frame
  skipping. Good fidelity with CPU headroom on most games.
- **Speed** – HLE audio bypasses the GBA sound CPU (the ROM `SoundMainRAM()`
  body is not executed once the mixer hook is detected), BIOS splash is skipped,
  and **Auto frame skip** scales skipped emulated frames under load (skipped
  frames skip PPU rasterization and PVR texture conversion). Best for the
  heaviest titles (3D/Mode-7-heavy games).

Switching profiles overwrites Frame skip, disables Auto frame skip, and rewrites
Audio buffer; adjust those rows afterward to fine-tune within a profile.

Large ROMs defaults to **Off** for stock Dreamcast memory budgets. Leave it off
for retail hardware; enable it only when testing a 32 MB RAM mod or emulator
configuration known to have the extra memory headroom.

### FPS Overlay / Benchmarking

Enable **Show FPS** in settings to overlay the measured display frame rate in
the top-left corner during play. The reading is averaged once per second by the
frame limiter. The overlay also shows `EF` (estimated emulated frames per second,
display FPS × (frame skip + 1)), `FS`/`FSA` for manual/automatic frame skip, and
`PG`, the number of ROM page-cache misses since the previous FPS sample. A
title running at full speed reports ~59.7 FPS (the GBA's native rate);
sustained readings below that indicate the SH4 cannot keep up at the current
profile.

### Repeatable Benchmark Workflow

To compare profiles or measure a code change consistently:

1. Use the same retail Dreamcast hardware and the same BIOS dump for every run.
2. Pick a fixed in-game scene per ROM (e.g. a specific level intro or a heavy
   battle/effect screen) and reach it the same way each time.
3. Enable **Show FPS** and let the scene run untouched for ~30 seconds, then
   record the steady-state FPS and `PG` readings.
4. Repeat across the Accuracy / Balanced / Speed profiles, changing only the
   profile between runs.
5. Note the lowest profile that holds ~59.7 FPS for that scene; that is the
   game's recommended profile in the compatibility table below.

A suggested benchmark set spans the GBA workload range: a light 2D title, a
sprite-heavy action title, a Mode-7 / pseudo-3D title, and an audio-heavy title.
Record actual ROMs used in `COMPATIBILITY.md` so results stay reproducible.

On the host port, `scripts/dc-host-benchmark.sh` runs the CI fixture ROMs
(`test.gba`, `kirby.gba`) across Accuracy / Balanced / Speed and prints FPS,
`EF` (estimated emulated FPS), frame-skip state, and ROM page-miss samples for
regression tracking before you record retail hardware numbers. Steady-state host
CI results are recorded in `COMPATIBILITY.md`.

### Compatibility Tiers

Per-game results are tracked in [`COMPATIBILITY.md`](COMPATIBILITY.md) using
these tiers:

| Tier | Meaning |
|------|---------|
| **Playable** | Holds ~full speed on at least one profile with no game-breaking issues. |
| **Runs** | Boots and is playable but with noticeable slowdown or audio/video artifacts. |
| **Broken** | Fails to boot, hangs, or has unplayable issues. |

## Release Packaging

### Building a CDI Image

After a successful build (`cmake --build build-dc`), the output binary is
`build-dc/NanoBoyAdvance-DC.elf`.  To create a self-booting CDI:

```sh
# 1. Scramble the ELF into a 1ST_READ.BIN
sh-elf-objcopy -O binary build-dc/NanoBoyAdvance-DC.elf \
  build-dc/NanoBoyAdvance-DC.bin
# (bootdreams/mkdcdisc handles scrambling automatically)

# 2. Create the disc directory
mkdir -p cdi_root
cp build-dc/NanoBoyAdvance-DC.bin cdi_root/1ST_READ.BIN

# 3. Place BIOS and ROMs
cp /path/to/bios.bin cdi_root/
mkdir -p cdi_root/gbaDC
cp /path/to/roms/*.gba cdi_root/gbaDC/

# 4. Build ISO with mkisofs, then CDI with cdi4dc
mkisofs -C 0,11702 -V NBA_DC -l -o nba_dc.iso cdi_root
cdi4dc nba_dc.iso nba_dc.cdi
```

**BootDreams alternative** (Windows GUI): point BootDreams at `cdi_root/` with
`NBA_DC` as the disc label; it handles scrambling and CDI creation.

### Recommended Layout for CDI Distribution

```
cdi_root/
├── 1ST_READ.BIN      # Scrambled emulator binary
├── bios.bin           # (user-supplied, not distributed)
├── game_config.txt    # Optional: idle-loop hints (gpSP compatible format)
├── gbaDC/             # gpSP convention: ROM root on CD
│   ├── ROM1.gba
│   └── ROM2.gba
└── nba-dc.toml        # Optional: default settings (overridden by /pc/nba-dc.toml)
```

The emulator scans `/cd/gbaDC/` (the gpSP convention) so placing ROMs there
ensures compatibility with existing CD builds and multi-boot discs.

### SD Card (Serial Port / GDEMU / MODE) Setup

```
/pc/
├── nba-dc.toml        # Settings (created automatically on first save)
├── bios.bin            # BIOS (copied here or loaded from /cd/)
├── roms/
│   ├── game.gba
│   ├── game.zip        # Extracted to /pc/roms/.cache/ on first load
│   └── .cache/         # Extracted ZIP contents (256 MiB cap, LRU eviction)
├── saves/
│   └── game.sav        # SRAM/FLASH/EEPROM saves
├── states/
│   └── game_0.nbss     # Save states (10 slots per ROM)
├── cheats/
│   └── game.cht        # gpSP-format cheat files
└── game_config.txt     # Idle-loop hints
| **Broken** | Fails to boot, hangs, or has issues that prevent normal play. |
