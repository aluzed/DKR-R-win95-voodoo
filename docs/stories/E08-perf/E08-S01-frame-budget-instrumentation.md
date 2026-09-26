# E08-S01 — Instrumentation and per-frame budget

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E02-S03, E05-S01 |
| **Blocks** | E08-S02, E08-S03, E08-S04, E09-S03 |

## Context

This whole project holds in one constraint: 33.3 ms per frame, on a processor that has
no margin. Every preceding ticket asked for a measurement and entered it "in the
budget". This ticket builds the budget itself.

The expected items, each measured by its ticket of origin:

| Item | Ticket of origin |
|---|---|
| Recompiled game's CPU | E00-S03, E02-S06 |

Measured 21 September 2026 by two instruments that do not measure the same thing,
which is worth settling before the budget is filled in.

`DKR_TRACE_CPU` gives the share of wall time on which guest threads are
**scheduled**: 72% at the title screen, 50% in the adventure hub. That is a
scheduling figure. A guest thread blocked on the renderer is wall time it does not
count and the frame still pays for, so it is not the budget's line item.

The renderer's own `[gfx] frame:` line gives the frame, and its answer is steadier
than expected: `elsewhere` — everything outside the renderer — is about **150 ms a
frame in every scene measured**, menus, loading and the richest world alike, while
the renderer's share is what the scene changes. The game's own cost is a floor, and
at roughly five times the 33.3 ms budget it is the reason the frame is 190 ms.

Both figures are cumulative means in the log, so per-scene instantaneous values can
be bounded but not read off. `scripts/Measure-Guest-Time-VM.sh` produces the series;
`docs/research/cpu-budget.md` has the runs.

**A third item belongs in the table above and is not in it.** Measured at the title
screen with patches 0042 and 0043, the wall divides into

    computing                             67.6%
    no guest running, renderer flagged    14.9%
    no guest running, nothing flagged     17.5%

and the third is none of the items this ticket lists.

**The budget closed on 22 September 2026**, and the table it closed to is not the
one below. One run, 400 display lists:

    RDRAM snapshot              94.0 ms a frame    50.2%
    guest, minus the snapshot   50.9 ms            27.2%
    renderer (send_dl)          39.9 ms            21.3%
    unaccounted                  2.5 ms             1.3%

The largest item is not in this ticket's list at all: an eight-megabyte copy of
RDRAM that `ultramodern` takes per graphics task, on the guest thread, which is
why it has been counted as the recompiled game since August. The middle row is by
subtraction and holds the recompiled game, the audio microcode and the scheduler
together. See `docs/research/cpu-budget.md`.

**Named, 21 September 2026: it is the graphics thread's own loop.** Not the
vertical interval, not the scheduler quantum, not the synchronised buffer swap;
all three were tested and refuted. The graphics thread is unblocked 29.2% of the
wall, of which 14.6% is inside `send_dl` and `update_screen` — which the rows
below already cover — and **14.5% is the loop around them**: the queue poll, the
variant dispatch, `sp_complete`, the DP edge. It wakes 120 times a second and
three rounds in five find nothing at all.

That is a row this table does not have. It is the size of the vertex path's whole
allowance, and it belongs to E08-S03 and E05 rather than to the recompiled code it
has been silently charged to. `[trace][gfx]`, patch 0045.

| Audio microcode | E03-S02 |
| Display-list decoding | E04-S02 |
| Vertex transformation | E04-S03 |
| Clipping | E04-S05 |
| Texture decoding | E04-S07 |
| Glide backend, CPU side | E05-S01 to E05-S08 |
| Texture download | E05-S02 |
| Audio output | E06-S03 |
| Message loop | E06-S01 |
| Presentation wait | E06-S04 |

On a machine with no modern profiler, the instrumentation has to be built into the
program. And it has to be cheap: an instrument that costs 5 % of the budget falsifies
what it measures.

## Objective

To deliver permanent instrumentation that breaks the time per frame down between its
items, readable on the target machine.

## Scope

**In:** the instrumentation, the display, the export, and establishing the budget.

**Out:** the optimisations themselves (E08-S02, E08-S03, E08-S04).

## Work

1. Implement per-zone counters, on a time source of measured cost (E02-S03). On a
   Pentium, `RDTSC` is the right instrument: a few cycles per read. Calibrate its
   frequency at startup.
2. Place the zones at the boundaries of the items above, aiming at a dozen zones —
   enough to attribute, few enough not to cost.
3. Measure the cost of the instrumentation itself, and make it disableable through the
   configuration (E06-S05). Record that cost.
4. Display the counters on screen, through E05-S07's 2D rectangles: time per frame,
   breakdown by item, number of triangles, number of state changes, texture memory
   occupancy, audio underruns.
5. Report distributions, not means: median and 99th percentile per item. It is the high
   percentile that produces the stutters and the audio dropouts.
6. Implement the export to a file, in order to analyse offline a session played on the
   target machine. It is the only way to study finely what happens there.
7. Establish the budget in `docs/research/frame-budget.md`: target allocation per item,
   real measurement, deviation. That document is the dashboard for all of E08.
8. Identify the three most expensive items and direct them to E08-S02, E08-S03 or
   E08-S04 according to their nature.

## Acceptance criteria

- [ ] The counters cover every item in the table.
- [x] The instrumentation's cost is measured and it is disableable. The coarse trace
      (`DKR_TRACE_CPU`) costs less than the run-to-run spread. The render zones
      (`DKR_TRACE_RENDER_ZONES`) cost 5.8 µs per clock read, about 8 ms a display
      list. Everything is off unless its variable is set. See `frame-budget.md`.
- [x] The on-screen display is legible on the target machine: `DKR_OSD=1`,
      frame rate, frame, render and audio times, triangles, state changes,
      texture memory and audio underruns, checked on frame dumps from the
      Voodoo 2, 0.5 ms a display list. See
      `frame-budget.md`.
- [x] Median and 99th percentile are reported per item: the frame period, the
      render per display list and the audio per task, over steady windows
      (`[gfx]   frame-percentiles`, `[audio][percentiles]`). The game thread's
      share comes from the trace and has no distribution. See `frame-budget.md`.
- [x] The export to a file allows offline analysis: `DKR_TIMING_EXPORT` writes
      one record per display list and per audio task, and
      `tools/win95/timing_report.py` gives CSV and exact percentiles. See
      `frame-budget.md`.
- [x] `docs/research/frame-budget.md` gives allocation, measurement and deviation per
      item. The allocation is a proposal awaiting a decision; the measurement is in
      processor time (patch 0051).
- [x] The three most expensive items are identified and directed: the audio
      microcode to E03-S03, the RDRAM snapshot to E08-S04, the graphics thread to
      E08-S03. The zones that located them also found 19 ms a display list of
      discarded trace formatting, now removed: 8.27 fps to 10.17.

## Risks

Without this ticket, optimisation is done on intuition — and intuition about 1998
hardware, with its tiny caches and a memory hierarchy very different from today's, is
regularly wrong. It must be delivered **before** any optimisation, failing which
E08-S02 and E08-S03 work blind.

## References

- `runtime-recomp/src/game/runtime_telemetry.cpp` — collection kept by E07-S02
- E00-S03 — first estimate of the budget
- E06-S03 — the audio underruns as an overall indicator
