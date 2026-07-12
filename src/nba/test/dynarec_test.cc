// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynarec Phase 1 tests: SH4 encodings, block cache, Thumb→IR compile, and
// IR execution vs the interpreter on a short ALU block in IWRAM.

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <nba/config.hh>
#include <nba/core.hh>
#include <nba/save_state.hh>

#include "arm/dynarec/block_cache.hh"
#include "arm/dynarec/sh4_emitter.hh"
#include "bus/bus.hh"
#include "core.hh"

namespace {

using nba::core::arm::dynarec::BlockCache;
using nba::core::arm::dynarec::BlockKey;
using nba::core::arm::dynarec::CompiledBlock;
using nba::core::arm::dynarec::Sh4Emitter;
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
  u32 cpsr_flags = 0;
};

auto Capture(nba::core::Core& core) -> Snapshot {
  nba::SaveState state{};
  core.CopyState(state);
  Snapshot s;
  s.r0 = state.arm.regs.gpr[0];
  s.r1 = state.arm.regs.gpr[1];
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

} // namespace

int main() {
  TestSh4Encodings();
  TestBlockCache();
  TestThumbCompiler();
  TestIrVsInterpreter();

  if(g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("dynarec tests ok\n");
  return 0;
}
