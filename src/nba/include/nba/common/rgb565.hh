// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nba/integer.hh>

namespace nba {

// Converts GBA ARGB1555 (bit layout: R[0:4] G[5:9] B[10:14] A[15])
// to RGB565 (B[0:4] G[5:10] R[11:15]).  Green is expanded from 5-bit to
// 6-bit by inserting a zero LSB -- the standard GBA colour conversion.
//
// Uses arithmetic instead of a 64 KiB lookup table so the conversion fits
// entirely in the SH4 ALU without touching dcache.  On Dreamcast this saves
// ~3 cache-missing main-memory loads per pixel.
inline auto RGB555ToRGB565(u16 rgb555) -> u16 {
  return static_cast<u16>(
    ((rgb555 & 0x001F) << 11) |   // red:   bits 0..4  → 11..15
    ((rgb555 & 0x03E0) <<  1) |   // green: bits 5..9  →  6..10
    ((rgb555 & 0x7C00) >> 10)     // blue:  bits 10..14 →  0..4
  );
}

} // namespace nba
