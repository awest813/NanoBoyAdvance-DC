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

add_test(NAME ppu-sprite-fast COMMAND nba-sprite-fast-test)
add_test(NAME ppu-merge-fast COMMAND nba-merge-fast-test)
