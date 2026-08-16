# E08-S01 — Instrumentation and per-frame budget

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | TODO |
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
- [ ] The instrumentation's cost is measured and it is disableable.
- [ ] The on-screen display is legible on the target machine.
- [ ] Median and 99th percentile are reported per item.
- [ ] The export to a file allows offline analysis.
- [ ] `docs/research/frame-budget.md` gives allocation, measurement and deviation per
      item.
- [ ] The three most expensive items are identified and directed.

## Risks

Without this ticket, optimisation is done on intuition — and intuition about 1998
hardware, with its tiny caches and a memory hierarchy very different from today's, is
regularly wrong. It must be delivered **before** any optimisation, failing which
E08-S02 and E08-S03 work blind.

## References

- `runtime-recomp/src/game/runtime_telemetry.cpp` — collection kept by E07-S02
- E00-S03 — first estimate of the budget
- E06-S03 — the audio underruns as an overall indicator
