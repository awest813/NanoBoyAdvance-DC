// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdarg>
#include <cstdio>

namespace nba {

// Unified Dreamcast logging — wraps printf + fflush(stdout) so every
// [NBA-DC] ... line is flushed immediately and can be gated or redirected
// later without touching 40 call sites.
inline void DCLog(char const* fmt, ...) {
  std::va_list args;
  va_start(args, fmt);
  std::vprintf(fmt, args);
  va_end(args);
  std::fflush(stdout);
}

} // namespace nba
