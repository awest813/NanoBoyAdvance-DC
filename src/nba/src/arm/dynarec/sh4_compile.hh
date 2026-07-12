// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "arm/dynarec/code_arena.hh"
#include "arm/dynarec/ir.hh"

namespace nba::core::arm::dynarec {

struct Sh4CompileResult {
  bool (*entry)(void* cpu, IrOp const* ops, u16 count) = nullptr;
  u8* code = nullptr;
  u32 size = 0;
};

// Lower a compiled IR block to SH4 machine code in `arena`.
// On success, `entry` is non-null on SH4 targets (after I-cache flush).
// On host builds the machine code is still emitted for verification but
// `entry` remains null because it cannot be executed.
auto TryCompileSh4Block(CompiledBlock const& block, CodeArena& arena) -> Sh4CompileResult;

} // namespace nba::core::arm::dynarec
