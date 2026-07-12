// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/ir.hh"

namespace nba::core::arm {

struct ARM7TDMI;

namespace dynarec {

// Execute a compiled IR block against the live CPU. Returns false if an IRQ
// forced an early exit (SignalIRQ already applied); true otherwise.
auto ExecuteIrBlock(ARM7TDMI& cpu, CompiledBlock const& block) -> bool;

} // namespace dynarec
} // namespace nba::core::arm
