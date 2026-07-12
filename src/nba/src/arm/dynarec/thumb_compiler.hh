// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/ir.hh"
#include "bus/bus.hh"

namespace nba::core::arm::dynarec {

auto PeekCodeHalf(Bus& bus, u32 address) -> u16;
auto PeekCodeWord(Bus& bus, u32 address) -> u32;

auto CompileThumbBlock(Bus& bus, u32 pc, CompiledBlock& out) -> bool;
auto CompileArmBlock(Bus& bus, u32 pc, CompiledBlock& out) -> bool;

} // namespace nba::core::arm::dynarec
