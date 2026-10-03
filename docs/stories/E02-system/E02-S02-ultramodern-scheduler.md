# E02-S02 — Resting the `ultramodern` scheduler on the Win95 layer

| | |
|---|---|
| **Epic** | E02 — Windows 95 system substrate |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E02-S01, E01-S02 |
| **Blocks** | E02-S06, E03-S02 |

## Context

`ultramodern` implements the N64's execution model: game threads at strict
priorities, message queues, hardware events, and the synchronisation between the game
thread and the graphics thread. It is code the project has every interest in keeping
— it is battle-tested, and rewriting it would amount to redoing `ultramodern`'s work
without its history of fixes.

The repository already has thirteen `n64-modern-runtime` patches in place, several of
which touch precisely the scheduling and the handing over of graphics tasks
(`0005-stop-scheduler-cascade-during-quit`,
`0010-yield-between-sp-and-dp-completion`,
`0013-use-reliable-external-message-fifo`). The precedent is therefore established:
those components get patched, they do not get rewritten.

This ticket substitutes E02-S01's layer for the standard primitives, without touching
the scheduling logic.

## Objective

To make the `ultramodern` scheduler work on the Win95 layer, with behaviour identical
to the modern host's.

## Scope

**In:** the `ultramodern` patches substituting the primitives, and their validation.

**Out:** any change to the scheduling behaviour. An observed difference is a porting
defect, not an improvement.

## Work

1. Record in `ultramodern` every use of the standard primitives: thread creation,
   lock, conditional wait, sleep, thread-local data.
2. Introduce a conditionally compiled seam: on modern targets, the standard
   primitives; on the Win95 target, E02-S01's layer. That seam must be a clean patch,
   fit to be proposed upstream — an invasive rewrite is unmanageable in the long run.
3. Deal with sleeping and timeouts. `ultramodern` relies on fine-grained timed waits;
   under Windows 95, the scheduler's default granularity is coarse and is set through
   `winmm`'s `timeBeginPeriod`. Measure the granularity actually reached rather than
   assuming it.
4. Deal with shutdown. Several existing patches bear on clean termination
   (`0004-wake-game-thread-on-runtime-quit`,
   `0005-stop-scheduler-cascade-during-quit`): check that they stay correct with the
   new layer, in particular if the conditional wait does not offer the same wake-up
   guarantee.
5. Do the same for `librecomp`, whose I/O and event threads use the same primitives.
6. Validate by execution: run the game with the diagnostic renderer
   (`null_renderer.cpp`) under emulated Windows 95, and check that it reaches the
   same point as on the modern host — the same threads created, the same graphics
   tasks submitted, the same frame rate.
7. Compare a scheduling trace between the two targets: the order in which threads are
   created, the order in which messages are handed over, the order of the switches.
   It is that comparison, and not the absence of a crash, that proves the equivalence.

## Acceptance criteria

- [~] Every use of a standard primitive in `ultramodern` and `librecomp` goes
      through the seam. **`ultramodern`: done** (5 primitives, 7 files).
      `librecomp`: no, it is blocked elsewhere.
- [x] The substitution is a patch under `patches/n64-modern-runtime/`, referenced in
      `patches/manifest.json` — 0015, with its digest.
- [x] The modern targets compile and behave identically with that patch applied — 15
      translation units compiled on the 64-bit Linux host before and after; without
      the two macros, the patch reduces to `using std::...`.
- [x] The time granularity actually reached under Windows 95 is measured -- 3 October
      2026, `SLEEPT.EXE` on the test machine: ultramodern waits with `Sleep`, which
      Windows 95 rounds to a 13.7 ms tick by default and honours to the millisecond
      after `timeBeginPeriod(1)` (`Sleep(1)` 1,001 us mean, `Sleep(5)` 4,994 us), which
      `dkr_clock_init` sets at start. The clock itself steps by under 100 us
      (`ultramodern::precise_now`, logged at every start). `docs/research/win95-clock.md`.
- [~] The game starts under emulated Windows 95 with the diagnostic renderer and
      reaches the same point as on the modern host -- it starts and runs with
      `RENDERER=null` (E02-S06: 9,822 display lists before a pixel was drawn); the
      comparison with the modern host waits for a modern build on this machine.
- [ ] The two targets' scheduling traces are compared and agree.
- [ ] The shutdown paths are rechecked in the light of the new wake-up semantics.
      **Rechecked on 3 October 2026, and they did not always finish**: about
      half the `Alt+F4`s left the process running. Found and fixed in the
      port, not in the wake-up semantics: `DestroyWindow` in
      `dkr_window_close`, racing the guest threads' exit -- 0 clean stops in 3
      with the call, 3 in 3 without, one binary, interleaved; the window is
      now left to the process's exit, and the build with the fix stopped 6
      times in 6. A second hang, inside `recomp::start`, shows in an older
      build and is not located yet. `docs/research/win95-shutdown-hang.md`.

## State as of 2026-08-13 — the substitution is done, execution stays blocked

Patch **0015**, `platform-seam-for-threading-primitives`, routes `ultramodern`'s five
primitives — `thread`, `mutex`, `condition_variable`, `lock_guard`, `unique_lock` —
through a seam that the target fills with E02-S01's layer. No scheduling logic is
touched: the patch does nothing but rename types.

What is gained, and verified:

| | |
|---|---|
| `ultramodern` compiles for Windows 95 | **15 files out of 15** |
| The modern targets compile identically | 15 out of 15, the `std::` branch unchanged |
| Forbidden includes | **9 → 1**, the last being `<filesystem>` (E02-S05) |
| The C++ bridge on the machine | 22 checks, 0 failures |
| `ultramodern` in the target's build | the `win95ultramodern` library |

Two discoveries reduced the announced work:

- **`thread_local` works under Windows 95.** The PE's TLS directory is duly handled
  there, contrary to what is often said. Measured on the machine by
  `tools/win95/witnesses/tls_probe.cpp`: two threads, isolated values. The three
  `thread_local`s in `threads.cpp` therefore asked for nothing.
- **The work's point 3 is moot.** `timer.cpp` already has an `#ifdef _WIN32` branch
  that calls `Sleep` directly; `std::this_thread` is never reached on this target,
  and the `timeBeginPeriod` question belongs to E02-S03, which deals with pacing.

**What stays blocked, and by what.** Points 5 to 7 — `librecomp`, running the game,
comparing the scheduling traces — presuppose that the game links for this target. It
cannot: `librecomp` still carries `<filesystem>` (E02-S05) and six compilation
errors, the recompiled code awaits
[E01-S05](../E01-build/E01-S05-compiling-the-recompiled-code.md), and SDL2 awaits
[E07-S03](../E07-scope/E07-S03-sdl2-decoupling.md). This ticket cannot close before
them.

## Risks

A subtle scheduling divergence — a message handed over in a different order, a switch
that does not happen at the same moment — may break nothing visibly and corrupt the
game's determinism. Step 7's trace comparison is the only way to catch it before it
becomes a gameplay bug that is hard to pin down.

## References

- `patches/n64-modern-runtime/` — thirteen patches, several of them on scheduling
- `runtime-recomp/src/game/null_renderer.cpp`
- E02-S01 — threading and synchronisation layer
