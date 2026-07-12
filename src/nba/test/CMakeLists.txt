# SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The tests drive the PPU's private rasterizers through the PPUTestAccess friend
# hook, so they need the nba private source tree (core.hh, hw/ppu/ppu.hh) in
# addition to the public API that linking nba already propagates.
function(add_nba_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
  target_link_libraries(${name} PRIVATE nba)
endfunction()

add_nba_test(nba-sprite-fast-test sprite_fast_test.cc)
add_nba_test(nba-merge-fast-test merge_fast_test.cc)
add_nba_test(nba-merge-bitmap-test merge_bitmap_test.cc)
add_nba_test(nba-bg-text-test bg_text_test.cc)
add_nba_test(nba-bg-affine-test bg_affine_test.cc)
add_nba_test(nba-bg-bitmap-test bg_bitmap_test.cc)
add_nba_test(nba-bg-advance-test bg_advance_test.cc)

add_nba_test(nba-dynarec-test dynarec_test.cc)

add_test(NAME ppu-sprite-fast COMMAND nba-sprite-fast-test)
add_test(NAME ppu-merge-fast COMMAND nba-merge-fast-test)
add_test(NAME ppu-merge-bitmap COMMAND nba-merge-bitmap-test)
add_test(NAME ppu-bg-text COMMAND nba-bg-text-test)
add_test(NAME ppu-bg-affine COMMAND nba-bg-affine-test)
add_test(NAME ppu-bg-bitmap COMMAND nba-bg-bitmap-test)
add_test(NAME ppu-bg-advance COMMAND nba-bg-advance-test)
add_test(NAME dynarec-phase1 COMMAND nba-dynarec-test)
