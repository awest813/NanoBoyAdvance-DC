#!/usr/bin/env bash
# Host benchmark helper for the Dreamcast port.
# Runs each performance profile against the CI fixture ROMs and prints FPS / page-miss samples.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-dc-host}"
BIN="$BUILD_DIR/bin/dreamcast/Release/NanoBoyAdvance"
FIXTURES="$ROOT/test-fixtures"
FRAMES="${NBA_DC_BENCH_FRAMES:-600}"

if ! command -v g++-12 >/dev/null 2>&1; then
  echo "g++-12 is required for the host Dreamcast build." >&2
  exit 1
fi

if [[ ! -x "$BIN" ]]; then
  cmake -B "$BUILD_DIR" -G Ninja \
    -DPLATFORM_DREAMCAST=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++-12 \
    -DCMAKE_C_COMPILER=gcc-12
  cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

mkdir -p "$FIXTURES/pc/roms" "$FIXTURES/pc/saves" "$FIXTURES/cd/gbaDC"
if [[ ! -f "$FIXTURES/pc/roms/test.gba" ]]; then
  python3 "$ROOT/scripts/generate-dc-test-rom.py" \
    --output "$FIXTURES/pc/roms/test.gba" \
    --size-kib 512 \
    --title "TEST ROM" \
    --code TEST
fi
if [[ ! -f "$FIXTURES/cd/kirby.gba" ]]; then
  python3 "$ROOT/scripts/generate-dc-test-rom.py" \
    --output "$FIXTURES/cd/kirby.gba" \
    --size-mib 16 \
    --title "KIRBY TEST" \
    --code B8KE \
    --maker 01 \
    --sram-signature
fi

if [[ ! -e /pc || -L /pc ]]; then
  sudo ln -sfn "$FIXTURES/pc" /pc
fi
if [[ ! -e /cd || -L /cd ]]; then
  sudo ln -sfn "$FIXTURES/cd" /cd
fi

SUMMARY_LINES=()

run_profile() {
  local profile="$1"
  local rom="$2"
  local label="$3"

  cat >"$FIXTURES/pc/nba-dc.toml" <<EOF
[dreamcast]
performance_profile = "$profile"
show_fps = true
EOF

  echo "=== Benchmark: $label | profile=$profile | rom=$rom | frames=$FRAMES ==="
  local log
  log="$(mktemp)"
  if ! NBA_DC_AUTOBOOT_ROM="$rom" NBA_DC_MAX_FRAMES="$FRAMES" "$BIN" >"$log" 2>&1; then
    cat "$log"
    rm -f "$log"
    echo "Benchmark failed: $label ($profile)" >&2
    exit 1
  fi

  local fps_lines
  fps_lines="$(grep '\[NBA-DC\] Runtime: FPS' "$log" | tail -3 || true)"
  if [[ -z "$fps_lines" ]]; then
    cat "$log"
    rm -f "$log"
    echo "Benchmark missing FPS samples: $label ($profile)" >&2
    exit 1
  fi

  echo "$fps_lines"
  local last_fps
  last_fps="$(echo "$fps_lines" | tail -1)"
  SUMMARY_LINES+=("$label | $profile | $rom | $last_fps")
  rm -f "$log"
  echo
}

echo "NanoBoyAdvance-DC host benchmark (${FRAMES} frames per case)"
echo "Record steady-state FPS from the last Runtime lines below."
echo

run_profile "Accuracy" /pc/roms/test.gba "light idle loop (512 KiB)"
run_profile "Balanced" /pc/roms/test.gba "light idle loop (512 KiB)"
run_profile "Speed"    /pc/roms/test.gba "light idle loop (512 KiB)"
run_profile "Balanced" /cd/kirby.gba     "large paged ROM (16 MiB)"
run_profile "Speed"    /cd/kirby.gba     "large paged ROM (16 MiB)"

echo "Host benchmark complete."
echo
echo "=== Summary (last steady-state sample per case) ==="
for line in "${SUMMARY_LINES[@]}"; do
  echo "$line"
done
