#!/usr/bin/env bash
# Host smoke test for the Dreamcast port: build, launch a ROM, run N frames, exit.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-dc-host}"
BIN="$BUILD_DIR/bin/dreamcast/Release/NanoBoyAdvance"
FIXTURES="$ROOT/test-fixtures"
FRAMES="${NBA_DC_MAX_FRAMES:-120}"

if ! command -v g++-12 >/dev/null 2>&1; then
  echo "g++-12 is required for the host Dreamcast build." >&2
  exit 1
fi

cmake -B "$BUILD_DIR" -G Ninja \
  -DPLATFORM_DREAMCAST=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_C_COMPILER=gcc-12
cmake --build "$BUILD_DIR" -j"$(nproc)"

mkdir -p "$FIXTURES/pc/roms" "$FIXTURES/pc/saves" "$FIXTURES/cd/gbaDC"
python3 "$ROOT/scripts/generate-dc-test-rom.py" \
  --output "$FIXTURES/pc/roms/test.gba" \
  --size-kib 512 \
  --title "TEST ROM" \
  --code TEST
cp "$FIXTURES/pc/roms/test.gba" "$FIXTURES/cd/test.gba"
cp "$FIXTURES/pc/roms/test.gba" "$FIXTURES/cd/TEST.GBA;1"

cat >"$FIXTURES/cd/gbaDC/game_config.txt" <<'EOF'
game_name = Smoke Test
game_code = TEST
vendor_code = 00
idle_loop_eliminate_target = 08000000
EOF

python3 "$ROOT/scripts/generate-dc-test-rom.py" \
  --output "$FIXTURES/cd/kirby.gba" \
  --size-mib 16 \
  --title "KIRBY TEST" \
  --code B8KE \
  --maker 01 \
  --sram-signature

if [[ ! -e /pc || -L /pc ]]; then
  sudo ln -sfn "$FIXTURES/pc" /pc
fi
if [[ ! -e /cd || -L /cd ]]; then
  sudo ln -sfn "$FIXTURES/cd" /cd
fi

run_case() {
  local label="$1"
  local rom="$2"
  local frames="${3:-$FRAMES}"
  echo "=== Smoke: $label ($rom, $frames frames) ==="
  local log
  log="$(mktemp)"
  if ! NBA_DC_AUTOBOOT_ROM="$rom" NBA_DC_MAX_FRAMES="$frames" "$BIN" >"$log" 2>&1; then
    cat "$log"
    rm -f "$log"
    echo "Smoke test failed: $label" >&2
    exit 1
  fi
  grep -q "Phase 9: Enter frame loop" "$log" || {
    cat "$log"
    rm -f "$log"
    echo "Smoke test missing gameplay loop: $label" >&2
    exit 1
  }
  grep -q "Smoke test: ran $frames frame" "$log" || {
    cat "$log"
    rm -f "$log"
    echo "Smoke test did not complete $frames frames: $label" >&2
    exit 1
  }
  rm -f "$log"
  echo "OK: $label"
}

run_case "PC paged ROM" /pc/roms/test.gba
run_case "CD paged ROM" /cd/test.gba 60
run_case "CD ISO9660 suffix" /cd/TEST.GBA 30
run_case "CD large ROM" /cd/kirby.gba

echo "All Dreamcast smoke tests passed."
