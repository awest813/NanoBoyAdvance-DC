// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arm/dynarec/sh4_compile.hh"
#include "arm/dynarec/sh4_emitter.hh"
#include "arm/dynarec/sh4_helpers.hh"

#include <cstring>
#include <cstdint>

namespace nba::core::arm::dynarec {

namespace {

static_assert(sizeof(IrOp) == 12, "SH4 stride emission assumes sizeof(IrOp) == 12");

using NativeEntryFn = bool (*)(void* cpu, IrOp const* ops, u16 count);

auto EmitNativeLoop(Sh4Emitter& emitter, void* helper_addr) -> bool {
  // SH4 GCC ABI: R4=cpu, R5=ops, R6=count. Returns bool in R0.
  emitter.StsLPrPreDec(8);
  emitter.MovImm(7, 0);

  const int loop_head = static_cast<int>(emitter.Words().size());

  emitter.CmpGe(6, 7);
  const int bf_done = static_cast<int>(emitter.Words().size()) - 1;
  emitter.Bf(0);

  emitter.MovReg(9, 7);
  emitter.Shll3(9);
  emitter.MovReg(0, 7);
  emitter.Shll2(0);
  emitter.AddReg(9, 0);
  emitter.AddReg(9, 5);

  const int helper_load = static_cast<int>(emitter.Words().size());
  emitter.MovLPcDisp(0, 0);
  emitter.Nop();
  emitter.Jsr(0);

  emitter.MovImm(1, 0);
  emitter.CmpEq(0, 1);
  const int bt_inc = static_cast<int>(emitter.Words().size()) - 1;
  emitter.Bt(0);

  emitter.MovImm(1, 1);
  emitter.CmpEq(0, 1);
  const int bt_success = static_cast<int>(emitter.Words().size()) - 1;
  emitter.Bt(0);

  const int exit_fail = static_cast<int>(emitter.Words().size());
  emitter.MovImm(0, 0);
  const int bra_epilogue_fail = static_cast<int>(emitter.Words().size()) - 1;
  emitter.Bra(0);

  const int inc_index = static_cast<int>(emitter.Words().size());
  emitter.AddImm(7, 1);
  const int bra_loop = static_cast<int>(emitter.Words().size()) - 1;
  emitter.Bra(static_cast<s16>(loop_head - (bra_loop + 1)));

  const int exit_success = static_cast<int>(emitter.Words().size());
  emitter.MovImm(0, 1);
  const int bra_epilogue_success = static_cast<int>(emitter.Words().size()) - 1;
  emitter.Bra(0);

  const int exit_done = static_cast<int>(emitter.Words().size());
  emitter.MovImm(0, 1);

  const int epilogue = static_cast<int>(emitter.Words().size());
  emitter.LdsLPostIncPr(8);
  emitter.Rts();

  const int pool_pc = static_cast<int>(emitter.Words().size());
  emitter.EmitPoolU32(static_cast<u32>(reinterpret_cast<uintptr_t>(helper_addr)));

  emitter.PatchBf(bf_done, exit_done);
  emitter.PatchBt(bt_inc, inc_index);
  emitter.PatchBt(bt_success, exit_success);
  emitter.PatchBra(bra_epilogue_fail, epilogue);
  emitter.PatchBra(bra_epilogue_success, epilogue);
  emitter.PatchMovLPcDisp(helper_load, pool_pc, 0);

  return true;
}

} // namespace

auto TryCompileSh4Block(CompiledBlock const& block, CodeArena& arena) -> Sh4CompileResult {
  Sh4CompileResult result{};

  if(block.ir_count == 0) {
    return result;
  }

  Sh4Emitter emitter;
  if(!EmitNativeLoop(emitter, reinterpret_cast<void*>(&nba_dr_run_one_op))) {
    return result;
  }

  const auto size = emitter.Size();
  u8* const dest = arena.Allocate(size, 4);
  if(dest == nullptr) {
    return result;
  }

  std::memcpy(dest, emitter.Data(), size);
  CodeArena::FlushExecutable(dest, size);

  result.code = dest;
  result.size = static_cast<u32>(size);

#if defined(__SH4__)
  result.entry = reinterpret_cast<NativeEntryFn>(dest);
#else
  result.entry = nullptr;
#endif

  return result;
}

} // namespace nba::core::arm::dynarec
