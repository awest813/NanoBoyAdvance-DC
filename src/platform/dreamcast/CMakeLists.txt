# SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
# SPDX-License-Identifier: GPL-3.0-or-later

include(${CMAKE_CURRENT_SOURCE_DIR}/../qt/version.cmake)

set(SOURCES
  src/device/dc_video_device.cc
  src/device/dc_audio_device.cc
  src/device/dc_input.cc
  src/dc_config.cc
  src/dc_ui.cc
  src/dc_rom_browser.cc
  src/dc_frontend.cc
  src/dc_cheats.cc
  src/dc_gameplay_menu.cc
  src/main.cc
  src/open_bios.cc
  src/romdisk_stub.c
  src/posix_stubs.c
)

set(HEADERS
  src/device/dc_video_device.hh
  src/device/dc_audio_device.hh
  src/device/dc_input.hh
  src/dc_config.hh
  src/dc_paths.hh
  src/dc_ui.hh
  src/dc_rom_browser.hh
  src/dc_frontend.hh
  src/dc_cheats.hh
  src/dc_gameplay_menu.hh
)

add_executable(NanoBoyAdvance-DC)

target_sources(NanoBoyAdvance-DC PRIVATE ${SOURCES} ${HEADERS})
target_include_directories(NanoBoyAdvance-DC PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(NanoBoyAdvance-DC PRIVATE platform-core)

if(NOT CMAKE_CROSSCOMPILING)
  find_package(SDL3 QUIET COMPONENTS SDL3)
  if(SDL3_FOUND)
    target_compile_definitions(NanoBoyAdvance-DC PRIVATE NBA_DC_ENABLE_SDL_MENU=1)
    target_link_libraries(NanoBoyAdvance-DC PRIVATE SDL3::SDL3)
  endif()
endif()

set_target_properties(NanoBoyAdvance-DC PROPERTIES OUTPUT_NAME "NanoBoyAdvance")

install(TARGETS NanoBoyAdvance-DC DESTINATION bin)
