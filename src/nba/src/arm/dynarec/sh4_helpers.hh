// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace nba::core::arm::dynarec {

// C linkage helpers invoked from generated SH4 code.
// Return codes for nba_dr_run_one_op:
//   0 = Continue executing the block
//   1 = Block finished successfully
//   2 = IRQ forced early exit (cpu state already updated)
inline constexpr int kDrStepContinue = 0;
inline constexpr int kDrStepDone = 1;
inline constexpr int kDrStepIrqExit = 2;

extern "C" int nba_dr_run_one_op(void* cpu, void const* op);

// Exposed for codegen tests and the linker on all targets.
extern "C" std::size_t nba_dr_ir_op_stride();

} // namespace nba::core::arm::dynarec
