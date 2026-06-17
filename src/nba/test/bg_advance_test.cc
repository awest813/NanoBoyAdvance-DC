// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Consistency test for the cross-scanline affine reference-point advance.
//
// The affine BGX/BGY internal registers (and the BG mosaic row counter) are
// advanced between scanlines by two duplicated implementations: the slow
// renderer does it inline at cycle 1232 (DrawBackgroundImpl), while the fast
// path AND the frame-skip-suppressed path both use FinishBackgroundScanline.
// If these drift, affine/Mode-7 backgrounds desync under the Speed profile or
// frame skip. This test asserts they advance the state identically across
// modes, mosaic settings, and the mosaic counter wrap boundary.

#include <cstdio>
#include <cstring>
#include <random>

#include "ppu_test_access.hh"

int main() {
  using nba::core::PPUTestAccess;
  using State = PPUTestAccess::AffineState;

  std::mt19937 rng(0x5CA17E);
  int tested = 0, mismatches = 0;

  auto h = std::make_unique<PPUTestAccess>();
  // Affine reference-point reads can touch VRAM during the slow render; keep it
  // defined (content is irrelevant to the reference-point advance).
  std::memset(h->Vram(), 0, 0x18000);

  for(int it = 0; it < 20000; it++) {
    const int mode = 1 + static_cast<int>(rng() % 5); // 1..5
    const bool mosaic2 = rng() & 1;
    const bool mosaic3 = rng() & 1;
    const int size_y = 1 + static_cast<int>(rng() % 16); // mosaic BG height 1..16
    const auto pb0 = static_cast<s16>(rng());
    const auto pd0 = static_cast<s16>(rng());
    const auto pb1 = static_cast<s16>(rng());
    const auto pd1 = static_cast<s16>(rng());
    const auto bx0 = static_cast<s32>(rng());
    const auto by0 = static_cast<s32>(rng());
    const auto bx1 = static_cast<s32>(rng());
    const auto by1 = static_cast<s32>(rng());
    const int counter_y = static_cast<int>(rng() % 16); // exercise wrap at size_y
    const int vcount = static_cast<int>(rng() % 160);

    auto setup = [&]() {
      h->SetupAffineAdvance(mode, mosaic2, mosaic3, size_y, pb0, pd0, pb1, pd1,
                            bx0, by0, bx1, by1, counter_y, vcount);
    };

    setup();
    h->RunSlowAdvance(mode);
    const State slow = h->CaptureAffine();

    setup();
    h->RunFinishAdvance(mode);
    const State fin = h->CaptureAffine();

    tested++;
    if(slow.bx0 != fin.bx0 || slow.by0 != fin.by0 || slow.bx1 != fin.bx1 ||
       slow.by1 != fin.by1 || slow.counter_y != fin.counter_y) {
      if(mismatches < 8) {
        std::printf("MISMATCH it=%d mode=%d m2=%d m3=%d sy=%d cy=%d vc=%d\n"
                    "  slow: bx0=%d by0=%d bx1=%d by1=%d cy=%d\n"
                    "  fin : bx0=%d by0=%d bx1=%d by1=%d cy=%d\n",
          it, mode, mosaic2, mosaic3, size_y, counter_y, vcount,
          slow.bx0, slow.by0, slow.bx1, slow.by1, slow.counter_y,
          fin.bx0, fin.by0, fin.bx1, fin.by1, fin.counter_y);
      }
      mismatches++;
    }
  }

  std::printf("[ppu-bg-advance] tested=%d mismatches=%d\n", tested, mismatches);
  return mismatches == 0 ? 0 : 1;
}
