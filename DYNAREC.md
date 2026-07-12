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

### Invalidation

- `InvalidateAll()` on reset, ROM attach, cheat ROM patches, and save-state load.
- Fine-grained page invalidation comes later (IWRAM/EWRAM writes that hit code).

## Config

- `Config::cpu_dynarec` (bool, default `false`).
- Dreamcast Speed profile does **not** auto-enable yet; toggle via
  `cpu_dynarec = true` under `[dreamcast]` in `/pc/nba-dc.toml` or the settings
  menu once wired.
- When unset / false, `Core::Run` is unchanged.

## Phased plan

| Phase | Deliverable |
|-------|-------------|
| **1 (this PR)** | IR + Thumb ALU/branch compiler, block cache, IR executor, SH4 encoder stubs, config flag, unit tests, docs |
| **2** | SH4 native emit for Phase-1 IR; icache flush; run native blocks on retail/Flycast |
| **3** | Thumb memory ops (LDR/STR/LDM/STM) via bus helpers; spill/fill |
| **4** | Conditional branches + block linking; ARM mode subset |
| **5** | SMC invalidation; telemetry (`DR` hit rate in FPS overlay); Speed-profile default A/B |

## Testing

- `nba-dynarec-test`: compile Thumb fixtures → IR → execute; compare register /
  CPSR flags against a software reference for the same opcode sequence.
- `Sh4Emitter` encoding self-checks (known instruction words).
- Host Dreamcast smoke stays on the interpreter unless `cpu_dynarec` is set.

## Risk notes

- Dynarec must never be required for correctness; interpreter fallback is
  mandatory.
- GPL-3.0: do not import non-GPL dynarec sources (e.g. some gpSP trees). Prefer
  original SH4 encodings from public architecture manuals.
