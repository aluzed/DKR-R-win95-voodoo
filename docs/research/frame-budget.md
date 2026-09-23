# The frame budget

The dashboard for E08, as E08-S01 asks for it: each item's allocation, its
measurement, and the deviation between them. The measurements, and how each
figure was arrived at, are in `cpu-budget.md`. This page keeps only the current
answer.

## How the figures are taken

In **processor time**, not wall time. That distinction decided most of what
`cpu-budget.md` found. Windows 95 does not implement `GetThreadTimes`, and on one
processor a wall interval also counts the time of every thread that preempted the
thread being timed. With `DKR_TRACE_EXCLUSIVE` set, every host section that has a
timer runs at time-critical priority, so its wall interval is its processor time:

- the audio microcode;
- each round of the graphics thread, with `send_dl` and `update_screen` inside it;
- the RDRAM snapshot.

A guest thread counted as running during another thread's exclusive section is
being preempted. Patch 0051 charges that time to the thread, so the guest rows are
processor time too.

    DKR_MEASURE_SET="DKR_TRACE_EXCLUSIVE=1" \
        scripts/Measure-Guest-Time-VM.sh build/win95/bin/DKRR.EXE glide 180

The lines to read are `[trace][ran]`, `[trace][preempted]`, `[trace][parked]`,
`[trace][snap]`, `[trace][gfx]` and `[audio][rate]`.

## The budget, 23 September 2026

The test machine is a Pentium II with a Voodoo 2. The run was 100 s of intro and
attract mode, and the frame was 120.1 ms (8.3 fps). The target is 33.3 ms (30 fps).

The allocation column is a **proposal**, not a decision anyone has taken. It is
there so that each ticket has a number to be measured against. It adds up to the
frame, and it keeps no margin, because the processor is never idle. Revise it
freely; the measurement column is the part that is established.

| Item | Owner | Measured | Share | Allocation (proposed) | Deviation |
|---|---|---:|---:|---:|---:|
| Graphics thread: display lists, present, loop | E05, E08-S03 | 34.2 ms | 28.5% | 12.0 ms | +22.2 ms |
| RDRAM snapshot | E08-S04 | 30.5 ms | 25.4% | 1.0 ms | +29.5 ms |
| Audio microcode | E03-S03 | 25.3 ms | 21.0% | 4.0 ms | +21.3 ms |
| Recompiled game (thread 3) | E08-S02 | 8.3 ms | 6.9% | 8.0 ms | +0.3 ms |
| Idle thread executing (thread 1) | runtime | 6.3 ms | 5.3% | 2.0 ms | +4.3 ms |
| libultra scheduler (thread 5, without the snapshot) | runtime | 3.0 ms | 2.5% | 1.5 ms | +1.5 ms |
| Audio manager (thread 4) | E03-S03 | 0.9 ms | 0.8% | 0.8 ms | +0.1 ms |
| Not measured | E08-S01 | 11.6 ms | 9.6% | 4.0 ms | +7.6 ms |
| **Frame** | | **120.1 ms** | | **33.3 ms** | **+86.8 ms** |

The graphics thread breaks down as 30.5 ms of rounds that carry a display list
(`send_dl` alone is 30.3 ms of processor per list), 3.3 ms of screen updates, and
0.4 ms of empty rounds.

**The audio row understates the problem.** On this target nothing clocks the
audio (E06-S03), so the game synthesises only 42.7% of the sound it should. The
invariant is 628 ms of processor for each second of sound, 263 to 922 depending on
the scene. Real-time sound at 30 fps would cost about **21 ms of every 33.3 ms
frame**, and would need 63% of the processor at any frame rate. The 4 ms
allocation assumes the high-level mixer.

**Not measured** is 11.6 ms. 8.65 s of it is the idle thread parked while no timed
section runs. The processor then belongs to host threads that no one times: the VI
thread, the SP task thread outside the microcode, the window's message loop, the
Glide driver. It may also be genuinely idle. The instrument cannot tell which.

## The three most expensive items, and where they go

1. **The graphics thread, 34.2 ms.** It goes to E08-S03 (the vertex path) and E05
   (the Glide backend). The display list alone is 30.3 ms of processor. This is the
   renderer's real cost, and not the 39.7 ms its own log line reports in a normal
   run.
2. **The RDRAM snapshot, 30.5 ms.** It goes to E08-S04, whose first work item is
   this snapshot: one task in flight and only the range DKR uses. It is a copy of
   four megabytes and nothing more. The fastest version of it is the one that does
   not happen.
3. **The audio microcode, 25.3 ms, and about 21 ms at the target even with
   everything else at zero.** It goes to E03-S03, the high-level mixer. That ticket
   is no longer optional. Without it this machine cannot have sound in real time.

The recompiled game, which E00-S03 put at 125 ms of a 170 ms frame and which the
whole of E08-S02 was sized against, measures **8.3 ms**. It is already inside its
proposed allocation. E08-S02 should wait for the three items above.

## What is still missing from E08-S01

- The instrumentation's own cost has not been measured. Every line above costs two
  clock reads per switch and per section, and exclusive mode changes the schedule by
  design.
- Medians and 99th percentiles per item: the figures above are means.
- The on-screen display, and the export for offline analysis.
- Within the graphics thread, the rows E08-S01 lists separately: display-list
  decoding, vertex transformation, clipping, texture decoding and download, and
  the Glide calls.
