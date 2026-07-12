# ARM7TDMI Dynarec (Dreamcast / SH4)

Experimental dynamic recompiler for the Dreamcast port. The cycle-accurate
interpreter in `arm7tdmi.hh` remains the source of truth for Accuracy and
Balanced profiles. Dynarec is an opt-in Speed-path accelerator.

## Goals

1. Cut ARM decode / dispatch overhead on the 200 MHz SH4 when the interpreter
   dominates frame time (`EMU_ms ≫ PPU_ms` in segment timers).
2. Preserve a correct fallback: any unsupported opcode, self-modifying code hit,
   or mid-block IRQ exits to the existing interpreter.
3. Keep host CI green: IR semantics are tested on x86_64; native SH4 emission is
   compiled for KallistiOS / `__SH4__` builds only.

## Non-goals (near term)

- Bit-identical cycle timing vs the interpreter on every edge case.
- ARM (32-bit) mode recompilation (Thumb-first; most GBA user code is Thumb).
- Host x86_64/aarch64 native backends.

## Architecture

```
 guest Thumb stream
        │
        ▼
 ┌──────────────┐     ┌─────────────┐
 │ ThumbCompiler│────▶│  IR block   │
 └──────────────┘     └──────┬──────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        BlockCache     IrExecutor      Sh4Emitter
        (PC → block)   (host + DC)   (Dreamcast SH4)
```

| Piece | Role |
|-------|------|
| `ir.hh` | Compact micro-ops for ALU, flags, simple branches, block exit |
| `thumb_compiler` | Decodes a basic block into IR; aborts on unsupported ops |
| `block_cache` | Fixed-capacity PC hash map + code/IR storage |
| `ir_exec` | Interprets IR against `ARM7TDMI` state + `Bus` (correctness path) |
| `sh4_emitter` | Lowers IR to SH4 machine code (enabled under `__SH4__`) |
| `dynarec` | Lookup → compile → execute; returns false to fall back |

### Block rules (Phase 1)

- Thumb mode only; block key is `(pc & ~1, thumb=1)`.
- End at: conditional branch, BX/BL/BLX, SWI, memory op, MSR/MRS, CPSR mode
  switch, or `kMaxBlockInsns` (32).
- Unconditional `B` is compiled as the final IR op (updates PC + pipeline reload).
- Mid-block IRQ check mirrors `ARM7TDMI::Run()` (bail before the next guest insn).

### Timing model (Phase 1)

Each IR guest instruction still performs the interpreter’s pipeline code-fetch
(`ReadHalf` / `ReadWord`) so scheduler timestamps advance. ALU bodies match the
interpreter helpers. Approximate bulk cycle skipping is deferred until segment
timers show fetch overhead dominating.

### Phase 2 (SH4 native path)

On `__SH4__` targets the dynarec lowers each cached block to a fixed SH4 loop in
a 512 KiB bump-allocated code arena (`CodeArena`). The loop calls
`nba_dr_run_one_op()` per IR micro-op (same semantics as the IR executor) and
returns a `bool` to the dispatcher. `CodeArena::FlushExecutable()` issues
`icache_flush_range()` when KOS cache headers are available.

Host builds still emit and size-check the SH4 machine code for CI, but
`CompiledBlock::native` stays null because the buffer cannot be executed.

### Phase 3 (Thumb memory ops)

The Thumb frontend compiles LDR/STR (imm/reg/PC/SP), signed byte/half loads,
LDRH/STRH, ADR/ADD SP, PUSH/POP, and LDMIA/STMIA into IR. Handlers mirror
`handler16.inl` bus helpers (`ReadWordRotate`, Idle after loads, PUSH/POP
pipeline reload on `POP {PC}`). Hi-register ops and BX still fall back to the
interpreter.

### Phase 4 (conditional branches, linking, ARM subset)

- Thumb.16 `B<cond>` compiles to `CondBranch` (block ends on both taken and
  not-taken paths).
- Soft block linking: after a block finishes, `TryRunBlock` chains up to
  `kMaxBlockChain` cached successors when PC matches `exit_taken` /
  `exit_fallthrough`.
- ARM-mode subset: data-processing (imm / reg+imm-shift) and B/B<cond>; no PC
  operands, no BL, no memory. Pipeline fetch uses `ReadWord` in ARM mode.

### Phase 5 (SMC, telemetry, Speed A/B)

- IWRAM/EWRAM stores notify a Bus hook; when the write hits a 256-byte page that
  holds compiled code, the dynarec schedules a cache + arena flush.
- Flushes are **deferred to the block boundary** while `TryRunBlock` is active
  so a live IR/native block is never wiped mid-op; cheats/`Poke*` flush eagerly.
- Page maps use reference counts so cache eviction unmarks pages (data-only
  writes stay a no-op).
- FPS overlay / runtime log show `DR N%` and `IV` when dynarec had lookups that
  second.
- Speed profile still defaults dynarec **Off**. Opt in with
  `cpu_dynarec_on_speed = true` (settings: **DR on Speed**), or force via
  `NBA_DC_DYNAREC=1` / `=0` for host A/B. Leaving Speed clears dynarec when it
  was armed only by that opt-in.

### Invalidation

- `InvalidateAll()` on reset, ROM attach, and save-state load.
- Page-tracked `InvalidateRange()` on IWRAM/EWRAM writes (including cheats /
  `Poke*`) that overlap compiled pages.

## Config

- `Config::cpu_dynarec` (bool, default `false`) — master toggle.
- `Config::cpu_dynarec_on_speed` (bool, default `false`) — when true, selecting
  the Speed profile also sets `cpu_dynarec = true`.
- Dreamcast: `[dreamcast] cpu_dynarec` / `cpu_dynarec_on_speed` in
  `/pc/nba-dc.toml`, settings menu, or `NBA_DC_DYNAREC`.
- When unset / false, `Core::Run` is unchanged.

## Phased plan

| Phase | Deliverable |
|-------|-------------|
| **1** | IR + Thumb ALU/branch compiler, block cache, IR executor, SH4 encoder stubs, config flag, unit tests |
| **2** | SH4 native emit for Phase-1 IR; icache flush; run native blocks on retail/Flycast |
| **3** | Thumb memory ops (LDR/STR/LDM/STM) via bus helpers; spill/fill |
| **4** | Conditional branches + block linking; ARM mode subset |
| **5** | SMC invalidation; telemetry (`DR` hit rate in FPS overlay); Speed-profile default A/B |

## Testing

- `nba-dynarec-test`: compile Thumb fixtures → IR → execute; compare register /
  CPSR flags against a software reference for the same opcode sequence.
- SMC: compile IWRAM block → poke overlapping code → expect flush + recompile.
- `Sh4Emitter` encoding self-checks (known instruction words).
- Host Dreamcast smoke stays on the interpreter unless `cpu_dynarec` is set.

## Risk notes

- Dynarec must never be required for correctness; interpreter fallback is
  mandatory.
- GPL-3.0: do not import non-GPL dynarec sources (e.g. some gpSP trees). Prefer
  original SH4 encodings from public architecture manuals.
