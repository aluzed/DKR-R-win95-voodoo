# E02-S03 — Clock, timers and time base

| | |
|---|---|
| **Epic** | E02 — Windows 95 system substrate |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E02-S01 |
| **Blocks** | E02-S05, E06-S04, E08-S01 |

## Context

DKR measures time by the VR4300's cycle counter, at 46.875 MHz — half the
processor's frequency. `ultramodern` translates that counter to a host clock, and the
whole simulation depends on it: frame pacing, race timing, audio timing.

Under Windows 95, the time sources available are unequal:

| Source | Resolution | Remark |
|---|---|---|
| `GetTickCount` | ~55 ms by default | too coarse, and overflows after 49.7 days |
| `timeGetTime` (`winmm`) | 1 ms with `timeBeginPeriod` | correct, system cost to be measured |
| `QueryPerformanceCounter` | depends on the hardware | present from Windows 95 onwards, to be validated on the target |
| `RDTSC` | one cycle | Pentium and later; frequency to be calibrated, and sensitive to power-saving modes |

`QueryPerformanceCounter` is the right default candidate, but its exact behaviour on
1998 hardware is checked rather than assumed — the frequency
`QueryPerformanceFrequency` returns varies with the chipset.

## Objective

To deliver a monotonic time base, of sufficient resolution for timing at 30 frames
per second, and to wire it onto `ultramodern`'s emulated cycle counter.

## Scope

**In:** the time source, its calibration, its validation, and the timers.

**Out:** display synchronisation (E06-S04) and audio pacing (E06-S03), which consume
this base without defining it.

## Work

1. Write `platform/win95/clock.{h,cpp}` with the source selected at launch:
   `QueryPerformanceCounter` if it is available and consistent, falling back on
   `timeGetTime` with `timeBeginPeriod(1)`.
2. Validate the retained source at startup: strict monotonicity, effective resolution
   measured, absence of jumps. An inconsistent source must be set aside in favour of
   the fallback rather than causing erratic behaviour in game.
3. Handle overflow for any 32-bit source, by accumulating in 64 bits. The test must
   simulate the return to zero — it is the kind of defect never met in development and
   always at a player's.
4. Wire the VR4300's cycle counter onto this base. Check that the ratio is exact: the
   counter advances at 46.875 MHz, independently of the host's frequency.
5. Implement the timers `ultramodern` needs (the equivalent of `osSetTimer` and the
   timer queue), on E02-S01's waiting layer.
6. Measure the cost of a call to the retained source. It is consulted several times
   per frame; on a Pentium II, an expensive repeated system call becomes a budget item
   in its own right. Record the figure for E08-S01.
7. Check that `timeBeginPeriod(1)`, if it is used, is duly released at shutdown:
   under Windows 9x, a setting left in place degrades the whole system until the next
   reboot.

## Acceptance criteria

- [x] `platform/win95/clock.{h,cpp}` selects and validates its source at launch — a
      source that steps backwards, even by one tick, is set aside in favour of the
      fallback.
- [x] The effective resolution is measured under emulated Windows 95 and recorded.
- [x] The 32-bit overflow is handled and covered by a test that simulates the return
      to zero — through `dkr_tick64_step`, reused from E01-S03 rather than copied.
- [x] The emulated cycle counter advances at 46.875 MHz, verified over a long
      duration rather than at an instant — **300 s on the target, −0.0000 %**.
- [ ] `ultramodern`'s timers work, with their precision measured. Blocked: they are
      only exercised by running the game.
- [x] The cost of a call is measured and recorded, with the reservation that the
      emulation is not temporal and that E09-S04 must confirm it.
- [x] `timeBeginPeriod` is released at shutdown, including on an abnormal stop. A
      registry of cleanups (`dkr_win95_at_abnormal_exit`) is called by E01-S03's
      exception filter **and** by the threads' fatal fault. The registry exists rather
      than a direct call so as not to invert the dependencies: `startup.c` is the
      bottom layer and cannot know about the clock without every witness dragging
      `winmm` along.

## State as of 2026-08-13 — the base is delivered and measured

`platform/win95/clock.{h,cpp}`, with its `test_clock.cpp` suite (host **and**
`CLOCKT.EXE`). Full report:
[`docs/research/win95-clock.md`](../../research/win95-clock.md).

The ticket's table of sources has been replaced by measurements, and **two of its
assumptions were false**:

| Source | Assumed | Measured |
|---|---|---|
| `GetTickCount` | ~55 ms | **9 ms**, and a hundred times cheaper than the others |
| `timeGetTime` | 1 ms *with* `timeBeginPeriod` | 1 ms **without** — the setting changes nothing here |
| `QueryPerformanceCounter` | "depends on the hardware" | 1,193,180 Hz, that is the **8254 PIT**; 4.19 µs |

The frequency says where the counter comes from, and from it follows a fact better
known before than after: **the PIT's low 32 bits wrap in exactly 60 minutes**.

Results on the target:

| | |
|---|---|
| Source retained | `QueryPerformanceCounter`, 1,193,180 Hz |
| Monotonicity | 200,000 reads, **0 steps backwards** |
| VR4300 counter drift over **300 s** | **−0.0000 %** (536 cycles out of 14.06 billion) |
| Suite | 20 checks, 0 failures |

### A defect found along the way, which justifies point 4

`ultramodern` derives `osGetCount` from `std::chrono::high_resolution_clock`.
Measured on the target: `is_steady=false`, and it is an alias of `system_clock`.
**All of DKR's timekeeping therefore rests on the wall clock**, which steps backwards
when the user changes the time or when Windows applies winter time.

Wiring `osGetCount` onto this base is therefore justified by a measurement, and not by
a concern for tidiness. That wiring is **not** done yet: it is a change of scheduler
behaviour which cannot be exercised as long as the game does not link (E01-S05,
E02-S05, E07-S03).

## Risks

A time base that drifts slowly breaks nothing visible and falsifies every race
timing. Step 4's check must therefore bear on a long duration — several minutes — and
not on a snapshot.

## References

- E02-S01 — the waiting layer
- E01-S03 — the `GetTickCount64` workaround
- `runtime-recomp/src/game/vi_presentation_policy.hpp` — the current presentation
  policy, a consumer of this base
