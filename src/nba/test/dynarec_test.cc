// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynarec Phase 1–5 tests: SH4 encodings, block cache, Thumb/ARM→IR, IR
// execution vs interpreter, and IWRAM SMC invalidation.

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <nba/config.hh>
#include <nba/core.hh>
#include <nba/save_state.hh>

#include "arm/dynarec/block_cache.hh"
#include "arm/dynarec/code_arena.hh"
#include "arm/dynarec/ir.hh"
#include "arm/dynarec/sh4_compile.hh"
#include "arm/dynarec/sh4_emitter.hh"
#include "bus/bus.hh"
#include "core.hh"

namespace {

using nba::core::arm::dynarec::BlockCache;
using nba::core::arm::dynarec::BlockKey;
using nba::core::arm::dynarec::CodeArena;
using nba::core::arm::dynarec::CompiledBlock;
using nba::core::arm::dynarec::IrOpKind;
using nba::core::arm::dynarec::Sh4Emitter;
using nba::core::arm::dynarec::TryCompileSh4Block;
namespace sh4_enc = nba::core::arm::dynarec::sh4_enc;

int g_failures = 0;

void Expect(bool cond, char const* msg) {
  if(!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_failures;
  }
}

constexpr u16 ThumbMovImm(int rd, u8 imm) {
  return static_cast<u16>(0x2000 | (rd << 8) | imm);
}
constexpr u16 ThumbAddImm(int rd, u8 imm) {
  return static_cast<u16>(0x3000 | (rd << 8) | imm);
}
constexpr u16 ThumbSubImm(int rd, u8 imm) {
  return static_cast<u16>(0x3800 | (rd << 8) | imm);
}
constexpr u16 ThumbCmpImm(int rd, u8 imm) {
  return static_cast<u16>(0x2800 | (rd << 8) | imm);
}
constexpr u16 ThumbAluAnd(int rd, int rs) {
  return static_cast<u16>(0x4000 | (rs << 3) | rd);
}
constexpr u16 ThumbB(int imm11) {
  return static_cast<u16>(0xE000 | (imm11 & 0x7FF));
}

void WriteThumb(nba::core::Core& core, u32 addr, std::vector<u16> const& code) {
  for(std::size_t i = 0; i < code.size(); ++i) {
    core.PokeHalf(addr + static_cast<u32>(i * 2), code[i]);
  }
}

void TestSh4Encodings() {
  Sh4Emitter e;
  e.Nop();
  e.Rts();
  e.MovReg(4, 5);
  e.AddImm(4, 8);

  Expect(e.Words().size() == 4, "emitter word count");
  Expect(e.Words()[0] == sh4_enc::kNop, "NOP encoding");
  Expect(e.Words()[1] == sh4_enc::kRts, "RTS encoding");
  Expect(e.Words()[2] == sh4_enc::MovReg(4, 5), "MOV Rm,Rn encoding");
  Expect(e.Words()[3] == sh4_enc::AddImm(4, 8), "ADD #imm,Rn encoding");
}

void TestBlockCache() {
  BlockCache cache;
  CompiledBlock block{};
  block.pc = 0x03000100;
  block.guest_insns = 3;
  cache.Insert(BlockKey(block.pc, true), block);

  auto* found = cache.Find(BlockKey(0x03000100, true));
  Expect(found != nullptr, "cache hit");
  Expect(found && found->guest_insns == 3, "cache payload");
  Expect(cache.Find(BlockKey(0x03000200, true)) == nullptr, "cache miss");
}

struct Snapshot {
  u32 r0 = 0;
  u32 r1 = 0;
  u32 r2 = 0;
  u32 cpsr_flags = 0;
};

auto Capture(nba::core::Core& core) -> Snapshot {
  nba::SaveState state{};
  core.CopyState(state);
  Snapshot s;
  s.r0 = state.arm.regs.gpr[0];
  s.r1 = state.arm.regs.gpr[1];
  s.r2 = state.arm.regs.gpr[2];
  s.cpsr_flags = state.arm.regs.cpsr & 0xF0000000u;
  return s;
}

auto MakeCore(bool use_dynarec, u32 entry, std::vector<u16> const& code)
  -> std::unique_ptr<nba::core::Core> {
  auto config = std::make_shared<nba::Config>();
  config->skip_bios = true;
  config->cpu_dynarec = use_dynarec;
  auto core = std::make_unique<nba::core::Core>(config);

  WriteThumb(*core, entry, code);

  nba::SaveState st{};
  core->CopyState(st);
  st.arm.regs.cpsr = 0x1F | (1u << 5); // SYS + Thumb
  for(int i = 0; i < 16; ++i) {
    st.arm.regs.gpr[i] = 0;
  }
  st.arm.regs.gpr[13] = 0x03007F00;
  st.arm.regs.gpr[15] = entry + 4;
  st.arm.pipe.opcode[0] = code[0];
  st.arm.pipe.opcode[1] = code[1];
  st.arm.pipe.access = nba::core::Bus::Access::Code | nba::core::Bus::Access::Sequential;
  st.bus.io.haltcnt = 0; // Run
  core->LoadState(st);
  return core;
}

void TestIrVsInterpreter() {
  const u32 entry = 0x03000100;
  // mov r0,#1; add r0,#2; mov r1,#0x10; and r0,r1; add r0,#7; sub r0,#3; cmp r0,#4; b +0; b self
  std::vector<u16> code = {
    ThumbMovImm(0, 1),
    ThumbAddImm(0, 2),
    ThumbMovImm(1, 0x10),
    ThumbAluAnd(0, 1),
    ThumbAddImm(0, 7),
    ThumbSubImm(0, 3),
    ThumbCmpImm(0, 4),
    ThumbB(0),
    static_cast<u16>(0xE7FE), // b self
  };

  auto interp = MakeCore(false, entry, code);
  auto dyna = MakeCore(true, entry, code);

  interp->Run(4000);
  dyna->Run(4000);

  const auto a = Capture(*interp);
  const auto b = Capture(*dyna);

  Expect(a.r0 == 4, "interpreter r0 == 4");
  Expect(b.r0 == 4, "dynarec r0 == 4");
  Expect(a.r0 == b.r0, "r0 match");
  Expect(a.r1 == b.r1, "r1 match");
  Expect(a.cpsr_flags == b.cpsr_flags, "NZCV match");
}

// Thumb.11 SP-relative LDR/STR, Thumb.14 PUSH/POP (no PC), Thumb.13 ADD/SUB SP
constexpr u16 ThumbPush(u8 list, bool lr) {
  return static_cast<u16>(0xB400 | (lr ? 0x100 : 0) | list);
}
constexpr u16 ThumbPop(u8 list, bool pc) {
  return static_cast<u16>(0xBC00 | (pc ? 0x100 : 0) | list);
}
constexpr u16 ThumbAddSp(u8 imm7) {
  return static_cast<u16>(0xB000 | (imm7 & 0x7F));
}
constexpr u16 ThumbSubSp(u8 imm7) {
  return static_cast<u16>(0xB080 | (imm7 & 0x7F));
}

void TestMemoryOpsVsInterpreter() {
  const u32 entry = 0x03000400;
  // mov r0,#0x42; mov r1,#0; mov r2,#0;
  // sub sp,#16; str r0,[sp,#0]; ldr r1,[sp,#0];
  // push {r1}; pop {r2}; add sp,#16; b self
  std::vector<u16> code = {
    ThumbMovImm(0, 0x42),
    ThumbMovImm(1, 0),
    ThumbMovImm(2, 0),
    ThumbSubSp(4),
    static_cast<u16>(0x9000 | (0 << 8) | 0),
    static_cast<u16>(0x9800 | (1 << 8) | 0),
    ThumbPush(0x02, false),
    ThumbPop(0x04, false),
    ThumbAddSp(4),
    static_cast<u16>(0xE7FE),
  };

  auto interp = MakeCore(false, entry, code);
  auto dyna = MakeCore(true, entry, code);

  interp->Run(8000);
  dyna->Run(8000);

  const auto a = Capture(*interp);
  const auto b = Capture(*dyna);

  Expect(a.r0 == 0x42, "mem interp r0");
  Expect(a.r1 == 0x42, "mem interp r1 from LDR");
  Expect(a.r2 == 0x42, "mem interp r2 from POP");
  Expect(b.r0 == a.r0, "mem r0 match");
  Expect(b.r1 == a.r1, "mem r1 match");
  Expect(b.r2 == a.r2, "mem r2 match");
}

void TestThumbCompiler() {
  auto config = std::make_shared<nba::Config>();
  config->skip_bios = true;
  nba::core::Core core(config);

  const u32 entry = 0x03000200;
  const std::vector<u16> code = {
    ThumbMovImm(0, 5),
    ThumbAddImm(0, 3),
    ThumbB(0),
  };
  WriteThumb(core, entry, code);
  Expect(core.PeekHalf(entry) == ThumbMovImm(0, 5), "iwram poke mov");
  Expect(core.PeekHalf(entry + 2) == ThumbAddImm(0, 3), "iwram poke add");
}

void TestSh4Codegen() {
  CompiledBlock block{};
  block.ir_count = 3;
  block.ir[0] = {.kind = IrOpKind::MovImm, .rd = 0, .imm = 1};
  block.ir[1] = {.kind = IrOpKind::AddImm, .rd = 0, .imm = 2};
  block.ir[2] = {.kind = IrOpKind::Exit};

  CodeArena arena;
  const auto result = TryCompileSh4Block(block, arena);

  Expect(result.code != nullptr, "sh4 code buffer allocated");
  Expect(result.size >= 32, "sh4 code non-trivial size");
  Expect(result.entry == nullptr, "sh4 entry null on host");
  Expect(arena.Used() == result.size, "arena tracks emission");

  const auto* words = reinterpret_cast<const u16*>(result.code);
  const auto word_count = result.size / sizeof(u16);
  bool saw_rts = false;
  for(u32 i = 0; i < word_count; ++i) {
    if(words[i] == sh4_enc::kRts) {
      saw_rts = true;
    }
  }
  Expect(saw_rts, "sh4 block ends with RTS");
}

void TestCondBranchVsInterpreter() {
  const u32 entry = 0x03000600;
  // mov r0, #0; cmp r0, #0; beq +2 (skip mov #1); mov r1, #1; mov r1, #2; b self
  // After beq taken: land on mov r1, #2
  // Layout:
  // 0: mov r0, #0
  // 1: cmp r0, #0
  // 2: beq +1    ; skip next insn if Z (imm=0 means +0 to r15 which is insn+4, so next next?)
  // Thumb_ConditionalBranch: state.r15 += imm * 2 where r15 is insn_pc+4
  // For beq to skip one insn: want target = insn_pc+4 (the insn after the skipped one)? 
  // insn at P+4 is CondBranch. Want target = P+8 (skip one 16-bit insn at P+6).
  // target = (P+4) + 4 + imm*2 = P+8 => imm*2 = 0 => imm=0.
  // With imm=0: target = insn_pc+4 = P+4+4 = P+8. Yes skips one insn.
  std::vector<u16> code = {
    ThumbMovImm(0, 0),
    ThumbCmpImm(0, 0),
    static_cast<u16>(0xD000),       // beq +0 -> skip next
    ThumbMovImm(1, 1),              // skipped
    ThumbMovImm(1, 2),              // taken land
    static_cast<u16>(0xE7FE),
  };

  auto interp = MakeCore(false, entry, code);
  auto dyna = MakeCore(true, entry, code);
  interp->Run(8000);
  dyna->Run(8000);
  const auto a = Capture(*interp);
  const auto b = Capture(*dyna);
  Expect(a.r1 == 2, "cond branch taken r1==2");
  Expect(b.r1 == a.r1, "cond branch r1 match");
}

auto MakeArmCore(bool use_dynarec, u32 entry, std::vector<u32> const& code)
  -> std::unique_ptr<nba::core::Core> {
  auto config = std::make_shared<nba::Config>();
  config->skip_bios = true;
  config->cpu_dynarec = use_dynarec;
  auto core = std::make_unique<nba::core::Core>(config);

  for(std::size_t i = 0; i < code.size(); ++i) {
    core->PokeWord(entry + static_cast<u32>(i * 4), code[i]);
  }

  nba::SaveState st{};
  core->CopyState(st);
  st.arm.regs.cpsr = 0x1F; // SYS, ARM mode (T=0)
  for(int i = 0; i < 16; ++i) {
    st.arm.regs.gpr[i] = 0;
  }
  st.arm.regs.gpr[13] = 0x03007F00;
  st.arm.regs.gpr[15] = entry + 8;
  st.arm.pipe.opcode[0] = code[0];
  st.arm.pipe.opcode[1] = code.size() > 1 ? code[1] : 0;
  st.arm.pipe.access = nba::core::Bus::Access::Code | nba::core::Bus::Access::Sequential;
  st.bus.io.haltcnt = 0;
  core->LoadState(st);
  return core;
}

void TestArmAluVsInterpreter() {
  const u32 entry = 0x03000800;
  // MOV r0, #1 ; ADD r0, r0, #2 ; B self
  // MOV Rd=#1, imm: cond=E, I=1, opcode=MOV(13), S=0, Rn=0, Rd=0, imm=1
  // 0xE3A00001
  // ADD r0,r0,#2: 0xE2800002
  // B -2 (self): offset such that target = entry. insn_pc=entry+8 for B at entry+8?
  // Place B at entry+8: insn_pc = entry+8, target = entry+8+8+imm<<2 = entry => imm<<2 = -16 => imm = -4 = 0xFFFFFC
  const u32 b_self = 0xEAFFFFFE; // common B to self encoding (offset -2 words from pc+8)
  std::vector<u32> code = {
    0xE3A00001, // mov r0, #1
    0xE2800002, // add r0, r0, #2
    b_self,
  };

  auto interp = MakeArmCore(false, entry, code);
  auto dyna = MakeArmCore(true, entry, code);
  interp->Run(8000);
  dyna->Run(8000);
  const auto a = Capture(*interp);
  const auto b = Capture(*dyna);
  Expect(a.r0 == 3, "arm interp r0==3");
  Expect(b.r0 == a.r0, "arm dynarec r0 match");
}

void TestSmcInvalidation() {
  const u32 entry = 0x03000A00;
  // mov r0, #1; add r0, #1; b self
  std::vector<u16> code = {
    ThumbMovImm(0, 1),
    ThumbAddImm(0, 1),
    static_cast<u16>(0xE7FE),
  };

  auto dyna = MakeCore(true, entry, code);
  dyna->Run(4000);

  auto before = dyna->TakeDynarecTelemetry();
  Expect(before.cache_blocks > 0, "smc: cache populated after run");
  Expect(before.hits + before.misses > 0, "smc: saw cache lookups");

  // Overwrite the ADD with mov r0, #7 while the block is cached.
  dyna->PokeHalf(entry + 2, ThumbMovImm(0, 7));

  auto after_poke = dyna->TakeDynarecTelemetry();
  Expect(after_poke.invalidations >= 1, "smc: poke invalidated compiled page");
  Expect(after_poke.cache_blocks == 0, "smc: cache flushed after code write");

  // Restart at the block entry with a pipeline that matches patched memory
  // (PC was sitting on `b self` after the first Run).
  nba::SaveState st{};
  dyna->CopyState(st);
  st.arm.regs.gpr[0] = 0;
  st.arm.regs.gpr[15] = entry + 4;
  st.arm.pipe.opcode[0] = ThumbMovImm(0, 1);
  st.arm.pipe.opcode[1] = ThumbMovImm(0, 7);
  dyna->LoadState(st);

  dyna->Run(4000);
  const auto snap = Capture(*dyna);
  // mov #1; mov #7; b self → r0 == 7
  Expect(snap.r0 == 7, "smc: recompiled block sees patched opcode");

  // Data write on a different IWRAM page must not flush code pages.
  dyna->Run(2000);
  auto mid = dyna->TakeDynarecTelemetry();
  Expect(mid.cache_blocks > 0, "smc: cache warm before data poke");
  dyna->PokeHalf(0x03007000, 0x1234);
  auto after_data = dyna->TakeDynarecTelemetry();
  Expect(after_data.invalidations == 0, "smc: data-only poke skips invalidate");
  Expect(after_data.cache_blocks > 0, "smc: cache survives data-only poke");
}

} // namespace

int main() {
  TestSh4Encodings();
  TestBlockCache();
  TestThumbCompiler();
  TestSh4Codegen();
  TestIrVsInterpreter();
  TestMemoryOpsVsInterpreter();
  TestCondBranchVsInterpreter();
  TestArmAluVsInterpreter();
  TestSmcInvalidation();

  if(g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("dynarec tests ok\n");
  return 0;
}
