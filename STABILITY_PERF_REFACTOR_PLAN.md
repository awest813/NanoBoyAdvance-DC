# Plan: Stability, Performance, and Refactoring Pass

This plan is grounded in `ROADMAP.md` (active milestones 3, 4, 5), the
`PPU_GPU_OVERHAUL.md` phased program, and a survey of the Dreamcast port source
tree (committed via `fd57fea6`). It describes the work the agent can drive
from the host side **without retail hardware** and where a refactor pass is
worth doing before the next major change lands.

---

## 1. What is hardware-blocked (out of scope here)

These items cannot be moved forward without retail SH4 runs:

- M3: per-game retail FPS, recommended profile, known regressions
- M3: re-measure retail benchmarks after each tuning pass
- M5 Phase A: retail GPU segment timers as the source of truth
- M5 Phase C.5: SH4-tuned inner loops (must wait for hot-loop identification)
- M5 Phase E.4: Speed profile default tuning by ear
- M5 Phase F: research items (PVR compositing, TA strips, SH4 DSP)

Anything in the host smoke + CI pipelines remains a valid regression check.

---

## 2. Software-actionable stability work

The codebase already has a layered diagnostics + UI story (frame timing,
breadcrumbs, FPS overlay, save status, fatal error screens, settings/pause
menus). The gaps are concentrated in three areas.

### 2.1 `main.cc` is past the comfort zone for one TU

**Status: Largely resolved.** `main.cc` has been reduced from 934 lines to 188
lines. The following modules have been extracted:

- `dc_autoboot.{hh,cc}` — Tekken autoboot path + env-driven autoboot
- `dc_session.{hh,cc}` — `LoadEmulator` and its phase breadcrumbs
- `dc_frame_timing.{hh,cc}` — frame timing singleton, per-second log
- `dc_auto_frameskip.{hh,cc}` — `UpdateAutoFrameSkip` policy
- `dc_log.hh` — unified `[NBA-DC]` logging helper
- `dc_version.{hh,cc}` — `BuildDreamcastBootInfo`

Remaining items that could be further extracted:
- `dc_frame_loop` — the KOS vs non-KOS frame loop divergence (still
  inline in `main.cc`)
- `dc_save_status` — post-session save-flush UX messages

**Remaining items:**

- `dc_frame_loop` — the KOS vs non-KOS frame loop divergence (still
  inline in `main.cc`)
- `dc_save_status` — post-session save-flush UX messages

### 2.2 `dc_video_device` packs three implementations into one TU

`dc_video_device.cc` (16 KB) interleaves KOS PVR, host SDL menu, and stub
paths under `#if`s. `Draw`, `DrawRgb565`, `Initialize`, and the two scaled
draw routines each carry the KOS/SDL/stub triple. This is the most likely
place for an uninitialized field on a new code path to slip through review.

Recommended shape: a `dc_video::Backend` strategy class with a single
implementation per backend (`dc_video_kos.cc`, `dc_video_sdl.cc`,
`dc_video_stub.cc`) selected in `Initialize`. The interface needs only what
the rest of the port actually calls (`Initialize`, `ClearScreen`, `Draw*`,
`DrawText*`, `Present`, `DrawStatusBar`, `DrawOverlay`, `DrawFilledRect`,
`ShowFatalError`).

### 2.3 `dc_config` TOML I/O and `dc_frame_timing` reset paths

~~`dc_config.cc` retains two legacy entry points (`LoadDreamcast`,
`SaveDreamcast`) that the codebase has moved past.~~ **Done:** Legacy
`LoadDreamcast` / `SaveDreamcast` have been removed; only `TryLoadDreamcast`
and `SaveDreamcastSafe` remain.

The `DCFrameTiming` singleton has an `OnSecondTick` that calls
`ResetInterval` and is called from both the KOS and the host frame loop.
The reset of the host branch is correct but not exercised by any test; if
the `PresentedFrames` counter is wrong, the printed `EMU ms` becomes
meaningless. The fix is small: a unit test that ticks the singleton for
N seconds with controlled `Add*Micros` calls and asserts the
`OnSecondTick` log format and counters reset.

### 2.4 Settings + ROM browser affordances

`dc_frontend.cc` Settings menu is fine but the long label list shows the
deep coupling between config knobs and UI text. The `kSettings[]` array
plus the per-knob `Adjust*` functions form a small DSL. If we add a
fourth profile, an auto-skip settings row, or a PVR DMA toggle, we have
to touch three places. A `struct Setting` with `name`, `value_label`,
`adjust` and a single row-render helper would localize the change.

The ROM browser appends `(last)` to the previously selected ROM and tags
large ROMs `[Needs Large ROMs]`. When the user picks an unlaunchable
entry the main loop falls through to a fatal-error path. Promoting this
into a structured `LaunchDecision { ok, reason }` from the browser would
let the UI show the explanatory message in-app and avoid the
`ShowFatalError` UX for a user-actionable case.

### 2.5 Logging hygiene

The `[NBA-DC] ...` log lines are emitted in many places with bespoke
`snprintf` calls. The `[NBA-DC] Phase N: ...` lines do not follow a
strict format (some include size, some don't, some add `[dir ok]`, etc.).
A small `nba_log_info(tag, fmt, ...)` helper in a shared header would
make it possible to gate logs to a single source-of-truth file or level
later, and would remove the `std::fflush(stdout)` calls scattered around.

---

## 3. Software-actionable performance work

### 3.1 Frame-skip policy (M5 Phase E.1 / E.2)

The current `UpdateAutoFrameSkip` uses *display* FPS and `EMU ms` hints.
Two things are open and host-testable:

- **Decoupling catch-up from skip-draw.** Today, raising the frame skip
  means running N+1 emulated frames per display frame, which re-pays the
  PPU cost on each suppressed frame. A "skip-1 + 1" model that runs
  exactly one emulated frame per display frame and uses the *fast* raster
  path internally has been on the PPU plan since Phase E.1 and is host
  measurable: the PPU unit tests can verify that the fast path is
  bit-identical to the cycle path on the existing test suite, and a host
  benchmark with the new policy can be compared against the current one
  for the same fixture ROMs.
- **EF cap.** The auto-skip already raises `frame_skip` down to 0 when
  `EF > 62` to avoid runaway catch-up. This is documented in `PPU_GPU_OVERHAUL.md`
  but the threshold is hard-coded. Promoting it to a named constant in
  `dc_frame_timing` and reading it from config is a small move that makes
  M5 Phase E.2 ("cap N when EF > 62") a one-line audit instead of a
  search.

### 3.2 Phase E.3 — present only when `frame_ready_`

`DCVideoDevice::Present()` already guards on `frame_ready_`, so this
behaviour is in. The host test should assert that two consecutive
`Present` calls without an intervening `Draw*` do not double-submit
the PVR scene. This is one more `dc_video_device` test that fits into
the host smoke.

### 3.3 M5 Phase A.3 (host-side only) — instrumentation consistency

`DCFrameTiming` is the right shape already. The only thing the agent
can land here without hardware is:

- Make `DCFrameTiming::OnSecondTick` respect `presented_frames_ == 0` for
  the per-emulated-frame rate print (it already does for the early-exit
  case; the unit test should pin it down).
- Add a `Pretty()` formatter that all `[NBA-DC] Frame timing: ...` lines
  share, so format changes don't drift between the KOS and the host
  path.

### 3.4 Hot paths we **should not** touch without retail data

`PPU` rasterization, `DrawMerge`, `DrawBackground` fast paths, the
sprite fast path, the DMA upload, the PVR scene submit, and the
`paged-ROM` cache are all on the optimization list. None of them should
be edited without either (a) a failing host pixel-diff or (b) a
retail segment timer that points to a specific function. `PPU_GPU_OVERHAUL.md`
makes this constraint explicit and the PPU unit tests give us the
(a) lever.

---

## 4. Suggested execution order

| Step | Risk | Payoff | Owner-actionable? |
|------|------|--------|-------------------|
| ~~Split `main.cc` into `dc_autoboot` + `dc_session` + `dc_frame_loop` + `dc_save_status`~~ | low | high (stability) | **done** (main.cc now 188 lines; modules: dc_autoboot, dc_session, dc_frame_timing, dc_auto_frameskip, dc_log, dc_version) |
| Move `dc_video_device` per-backend implementations into separate TUs behind a `Backend` | low | medium | yes |
| ~~Delete legacy `LoadDreamcast` / `SaveDreamcast`; rename callers to the safe pair~~ | trivial | low | **done** |
| Introduce a `dc_log` helper; replace `[NBA-DC] ...` `snprintf` + `fflush(stdout)` pairs | low | low | yes |
| Promote `kEFUpperCap` to a named constant in `dc_frame_timing`; thread to config | low | low | yes |
| Add a `DCFrameTiming` host unit test (tick + reset) | low | medium (perf confidence) | yes |
| Add a `dc_video_device` test for `frame_ready_` double-`Present` guard | low | low | yes |
| `Setting` struct + small DSL for `kSettings[]` | medium | medium (frontend change cost) | yes |
| `LaunchDecision` from `ROMBrowser::Scan` for unlaunchable entries | medium | medium | yes |
| Decouple catch-up from skip-draw (M5 Phase E.1) | medium-high | high (perf) | needs PPU test + host bench |
| Tune SH4 inner loops (M5 Phase C.5) | high | high | **retail-blocked** |
| Phase D.2 (twiddle) / D.5 (double-buffer) revisit | low | low (already evaluated) | not worth |

---

## 5. What I would **not** do in this pass

- Change PPU raster math, sprite fetch, or the merge path. The PPU
  tests are the only signal we have on host and they already pass.
- Edit `dc_pvr` upload heuristics without a profiling source. The
  current async TA-DMA + blocking fallback is documented as settled.
- Rewrite the ROM browser scan logic. The `;1` strip, KOS vs SDL paths,
  and the `(last)`-entry injection all work and have explicit tests.
- Touch `dc_cheats` or the `.cht` parser. They are not on any open
  milestone.
- Restructure the CMake setup beyond what the build directory already
  requires.

---

## 6. Open questions for the user

1. Is the `dc_video_device` refactor (Section 2.2) in scope, or is the
   single-TU shape preferred for easier bisect?
2. Should the `Setting` DSL (Section 2.4) land before or after M5 Phase E.1?
3. For the frame-skip policy (Section 3.1), do you want the decoupling
   attempt to be host-bench-only first, or behind a new config flag from
   day one?
