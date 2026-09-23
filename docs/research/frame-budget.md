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

## The budget, 23 September 2026, after the trace fix

The test machine is a Pentium II with a Voodoo 2. The run was 100 s of intro and
attract mode in exclusive mode, with a frame of 106.9 ms. The same binary in normal
mode, with the trace off, runs at **98.3 ms, 10.17 fps**. The target is 33.3 ms
(30 fps).

The allocation column is a **proposal**, not a decision anyone has taken. It is
there so that each ticket has a number to be measured against. It adds up to the
frame, and it keeps no margin, because the processor is never idle. Revise it
freely; the measurement column is the part that is established.

| Item | Owner | Measured | Share | Allocation (proposed) | Deviation |
|---|---|---:|---:|---:|---:|
| Audio microcode | E03-S03 | 31.5 ms | 29.5% | 4.0 ms | +27.5 ms |
| RDRAM snapshot | E08-S04 | 29.5 ms | 27.6% | 1.0 ms | +28.5 ms |
| Graphics thread: display lists, present, loop | E08-S03 | 15.9 ms | 14.9% | 12.0 ms | +3.9 ms |
| Recompiled game (thread 3) | E08-S02 | 8.2 ms | 7.7% | 8.0 ms | +0.2 ms |
| Idle thread executing (thread 1) | runtime | 5.9 ms | 5.5% | 2.0 ms | +3.9 ms |
| libultra scheduler (thread 5, without the snapshot) | runtime | 3.0 ms | 2.8% | 1.5 ms | +1.5 ms |
| Audio manager (thread 4) | E03-S03 | 1.8 ms | 1.6% | 0.8 ms | +1.0 ms |
| Not measured | E08-S01 | 11.1 ms | 10.3% | 4.0 ms | +7.1 ms |
| **Frame** | | **106.9 ms** | | **33.3 ms** | **+73.6 ms** |

Before the fix, the same table had the graphics thread first, at 34.2 ms of a
120.1 ms frame. The trace fix took 19 ms off each display list (see below), and
the order of the three large items changed with it.

**The audio row understates the problem, and it has grown.** A faster game
submits more audio tasks: 1,333 in this run, against 1,054 before. On this target
nothing clocks the audio (E06-S03), so the game synthesises only part of the
sound it should. The invariant is 628 ms of processor for each second of sound,
263 to 922 depending on the scene. Real-time sound at 30 fps would cost about
**21 ms of every 33.3 ms frame**, and would need 63% of the processor at any
frame rate. The 4 ms allocation assumes the high-level mixer.

**Not measured** is 11.1 ms. Most of it is the idle thread parked while no timed
section runs. The processor then belongs to host threads that no one times: the VI
thread, the SP task thread outside the microcode, the window's message loop, the
Glide driver. It may also be genuinely idle. The instrument cannot tell which.

### The snapshot row can be zero: `DKR_RDRAM_SNAPSHOT=none`

Patch 0052 adds a mode in which the graphics task is drawn from live RDRAM while
libultra's scheduler waits for it, holding the guest token. Nothing is copied. The
reasoning is in the patch and in `cpu-budget.md`. In short: on one processor, the
overlap the snapshot bought never existed. Normal mode, trace off:

| Mode | Frame | fps | Run |
|---|---:|---:|---|
| default (snapshot) | 98.2 ms | 10.18 | 150 s |
| `none` | 54.2 ms | 18.44 | 150 s |
| `none` | 59.7 ms | 16.75 | 300 s: 1,920 display lists, 0 rejects, clean shutdown |

The attract mode's screenshots match between the two modes. **It is still off by
default.** Two runs of the attract mode are not the menus, a race under the
player's control, or a long session. Turning it on is a decision to take after
those have been played.

## Inside the display list

`DKR_TRACE_RENDER_ZONES` puts a timed proxy in front of every entry of the
`dkr_render_backend` table. It times `dkr_f3d_run`, each opcode, texture
conversion, and twelve phases of `cmd_triangle`, all on the `RDTSC` clock (see
`platform/win95/clock.h`). The zones cost about 0.9 ms a list. One run in
exclusive mode, 780 display lists, per list:

| Zone | Per list |
|---|---:|
| `dkr_f3d_run`, all of it | 11.7 ms |
| `cmd_triangle` (opcode 0x05), backend calls included | 8.9 ms |
| - `draw_triangles` | 1.9 ms |
| - the corners: vertex fetch, `trace` arguments, (s,t) and NDC statistics | 3.4 ms |
| - near-plane clipping | 1.1 ms |
| - projection | 0.9 ms |
| - per-triangle diagnostics between state and draw | 0.8 ms |
| - `apply_state` | 0.6 ms |
| - batch checks, cull and tail | 0.3 ms |
| Texture lookups, state pushes | 1.2 ms |
| Texture conversion and upload | 0.1 ms |

**Before the fix, the corners were 18.1 ms of a 31.2 ms list.** 16.7 ms of that
was a single `trace()` line per corner, formatted with `vsprintf` and then thrown
away, because the renderer installed its trace callback unconditionally. The
renderer now installs it only while its 24 context lines are unspent. The log's
output is unchanged, and the list went from 31.2 ms to 12.1 ms of processor.

What remains is spread thin. About 2.8 ms a list is statistics kept for the
diagnosis of August and September: (s,t) and NDC extremes, and the per-triangle
diagnostics block. That work produces counters and draws nothing. The vertex
path proper, clipping plus projection, is 2 ms. The texture cache hits: 0.6
conversions a list.

### The statistics, now optional: `DKR_GFX_NO_STATS=1`

About a fifth of what was left of `dkr_f3d_run` was statistics: (s,t) and NDC
extremes, triangle areas, the screen-centre probe, per-combiner and per-state
counts. They feed the diagnostic counter lines and draw nothing, and until now no
switch skipped them. `no_statistics` in the decoder's context now skips all of
them behind one test, and `DKR_GFX_NO_STATS=1` sets it. The per-corner `trace`
call is also guarded at the call site now, so that its arguments are not
evaluated when there is no trace.

    dkr_f3d_run, per list, exclusive     11.7 ms -> 9.5 ms
    frame, normal mode, trace off        98.8 ms -> 95.9 ms   (about -3%)

The counter lines read zero with the switch on, which is why it is off by default.

### The semaphore spin, removed on this target

`LightweightSemaphore` spun up to 10,000 times before each kernel wait, on every
guest handoff. On one processor that spin cannot succeed. Patch 0053 and
`cmake/win95-target.cmake` set it to zero: 97.4–98.8 ms to 96.3–96.4 ms in normal
mode, trace off. This one is on by default: it changes timing only, not results.

## What the instrumentation costs

`DKR_TRACE_CPU` and everything built on it (patches 0041 to 0051), measured by the
renderer's `[gfx] frame:` line, which is printed with or without the trace. Four
150 s runs on the same binary, alternating:

    trace off   120.1 ms   121.7 ms   mean 120.9 ms
    trace on    118.4 ms   122.3 ms   mean 120.4 ms

The difference is below the run-to-run spread of about ±1.6 ms. The coarse trace
costs nothing measurable. The fine one did while it read the 8254, at 5.8 µs a
read and 8 ms a display list. On `RDTSC` it costs about 0.9 ms a list.

## The three most expensive items, and where they go

1. **The audio microcode, 31.5 ms, and about 21 ms at the target even with
   everything else at zero.** It goes to E03-S03, the high-level mixer. That ticket
   is no longer optional. Without it this machine cannot have sound in real time.
2. **The RDRAM snapshot, 29.5 ms.** It goes to E08-S04, whose first work item is
   this snapshot: one task in flight and only the range DKR uses. It is a copy of
   four megabytes and nothing more. The fastest version of it is the one that does
   not happen.
3. **The graphics thread, 15.9 ms.** It goes to E08-S03. The display list is 12.1
   ms of processor, and about 2.8 ms of that is diagnostic statistics that can be
   compiled out. The Glide backend, E05, is 2 to 3 ms of it.

The recompiled game, which E00-S03 put at 125 ms of a 170 ms frame and which the
whole of E08-S02 was sized against, measures **8.2 ms**. It is already inside its
proposed allocation. E08-S02 should wait for the three items above.

## What is still missing from E08-S01

- The instrumentation's cost is measured (above), but only for the coarse trace
  and the render zones. Exclusive mode changes the schedule by design, and it is
  meant for reading costs, not frames.
- Medians and 99th percentiles per item: the figures above are means.
- The on-screen display, and the export for offline analysis.
- The zones stop at `cmd_triangle`'s phases. `dkr_clip_near` and
  `dkr_clip_project` are timed whole, not inside.
