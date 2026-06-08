# SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# miniz is public domain / MIT-compatible (see miniz.h unlicense notice).

add_library(miniz STATIC
  miniz.c
  miniz_tdef.c
  miniz_tinfl.c
  miniz_zip.c
)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
