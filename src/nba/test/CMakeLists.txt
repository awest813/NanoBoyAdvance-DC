# SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(nba-sprite-fast-test sprite_fast_test.cc)

# The test drives the PPU's private sprite rasterizers, so it needs the nba
# private source tree (core.hh, hw/ppu/ppu.hh) in addition to the public API
# that linking nba already propagates.
target_include_directories(nba-sprite-fast-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(nba-sprite-fast-test PRIVATE nba)

add_test(NAME ppu-sprite-fast COMMAND nba-sprite-fast-test)
