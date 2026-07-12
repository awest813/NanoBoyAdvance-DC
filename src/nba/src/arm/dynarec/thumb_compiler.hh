// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/ir.hh"
#include "bus/bus.hh"

namespace nba::core::arm::dynarec {

// Side-effect-light code fetch for the compiler (no waitstates / scheduler).
auto PeekCodeHalf(Bus& bus, u32 address) -> u16;

// Decode a Thumb basic block starting at `pc` (address of the first opcode,
// i.e. r15-4 in the interpreter’s pipeline convention at block entry).
// Returns false if the first opcode is unsupported (caller should interpret).
auto CompileThumbBlock(Bus& bus, u32 pc, CompiledBlock& out) -> bool;

} // namespace nba::core::arm::dynarec
