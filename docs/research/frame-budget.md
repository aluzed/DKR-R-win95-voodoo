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

## Correction, 24 September 2026: steady state, and what the 17% is

**The budgets below were taken from cumulative counters, and those include the
loading phase.** An idle meter now shows how much that matters.
`DKR_TRACE_IDLE_METER=1` runs a thread at THREAD_PRIORITY_IDLE, which only runs
when nothing else of any process is ready. It measures spare processor
directly, and it costs nothing: 44.3 ms a frame with it against 44.1 without.
Per five-second interval:

    10 s  86%   15 s 27%   20 s 60%   25 s 10%     loading
    30 s   3%   35 s  5%   40 s  4%   ...   90 s 2.4%   attract mode

In steady state the processor is **97% busy**. The same budget taken over a
steady window (40 to 90 s) rather than from boot:

| Item | Share | Per 44.9 ms frame |
|---|---:|---:|
| Graphics thread | 29.0% | 13.0 ms |
| Audio mixer | 24.2% | 10.8 ms |
| Recompiled game (thread 3) | 14.2% | 6.4 ms |
| Idle thread executing: delivering interrupts | 8.7% | 3.9 ms |
| Audio manager (thread 4) | 3.6% | 1.6 ms |
| libultra scheduler (thread 5) | 3.0% | 1.4 ms |
| Not measured | 17.3% | 7.8 ms |

Of the 17.3% not measured, about 3 points are spare. The rest, **about 14% of
the processor, is work no timer sees.** The VI thread is not it: it waits in
`Sleep`, not in a spin. What remains is kernel-side: thread switches and the
semaphore calls behind every guest handoff, the 1 ms timer interrupt, the
Glide driver's own work. A user-mode stopwatch cannot see any of these, so the
next instrument would have to be a sampler.

**The frame is not paced by the retrace.** A histogram of frame periods in 2 ms
bins (`[gfx] frame-bins-2ms`, beside `[gfx] frame-retraces`) shows one broad hump
from 20 to 60 ms, peaking at 38 to 44 ms, with no clustering on 33.3 or 50. The
frame is as long as its work.

## The budget, 24 September 2026, with the audio mixer

After the audio mixer (E03-S03), the semaphore-spin fix and
`-fno-strict-aliasing` on all recompiled code. This is measured with both
opt-in options on, `DKR_RDRAM_SNAPSHOT=none` and `DKR_GFX_NO_STATS=1`, since
they are what a player would run once they are validated. Exclusive mode,
90.1 s, a frame of 44.9 ms. The same binary in normal mode runs at 43.2 ms,
23.1 fps.

| Item | Owner | Measured | Share |
|---|---|---:|---:|
| Graphics thread: display lists, present, loop | E08-S03 | 12.7 ms | 28.3% |
| Audio: the high-level mixer | E03-S03 | 9.6 ms | 21.3% |
| Recompiled game (thread 3) | E08-S02 | 7.4 ms | 16.5% |
| Idle thread executing: delivering interrupts | runtime | 4.6 ms | 10.2% |
| Audio manager (thread 4) | E03-S03 | 1.6 ms | 3.6% |
| libultra scheduler (thread 5) | runtime | 1.5 ms | 3.4% |
| RDRAM snapshot | E08-S04 | 0 | - |
| Not measured: idle thread parked, nothing timed running | E08-S01 | 7.5 ms | 16.6% |
| **Frame** | | **44.9 ms** | |

Three things have changed since the table below:

- **Nothing dominates any more.** The largest item is 28% of the frame. The
  three that were 90 ms of a 120 ms frame are now 22 ms together: the renderer
  fell 19 ms through the trace fix, the snapshot is gone, and the audio is a
  quarter of what it was.
- **The recompiled game is now the third item**, at 7.4 ms. That is E08-S02's
  turn. It was deliberately left for last.
- **16.6% is not measured, and is probably idle.** At 22 fps the frame no longer
  divides evenly into retraces. The game waits for the next vertical interval,
  and nothing runs while it does. If so, that time comes back only when the frame
  crosses the next retrace boundary, not by making any single item cheaper.

The audio row covers 62% of real-time sound. At full rate the mixer would be
about 15 ms here, and about 5.5 ms of a 33.3 ms frame at the target.

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
- The on-screen display.
- The zones stop at `cmd_triangle`'s phases. `dkr_clip_near` and
  `dkr_clip_project` are timed whole, not inside.

### Medians and 99th percentiles

The figures above are means. Stutters and audio dropouts come from the high
percentile, so the log now also reports distributions, from fixed histograms
(`runtime-recomp/src/game/percentile_histogram.hpp`, no allocation, no floating
point):

- `[gfx]   frame-percentiles`: the frame period (1 ms bins) and one display
  list's render (250 µs bins), over windows of 600 display lists, about 20 s.
- `[audio][percentiles]`: one audio task (250 µs bins), over windows of 300
  tasks, about 10 s of sound.

A percentile is the upper edge of its bin. It is reported in wall time, which
includes preemption, and that is the time that matters for a missed retrace or
an underrun. The first window of a run includes loading and is not read. The
first audio histogram, at 100 µs a bin, capped its p99 at exactly 25,600 µs, the
last bin; the bins are now 250 µs wide, with a 64 ms ceiling.

Normal mode, 200 s runs, steady windows:

| | Default options | Both opt-in options |
|---|---:|---:|
| Frame period, p50 | 68 ms | **34 ms** |
| Frame period, p99 | 135 ms | 67 to 81 ms |
| Render per list, p50 | 15.0 ms | 7.8 to 17.3 ms |
| Render per list, p99 | 42.5 ms | 19.8 to 25.8 ms |
| Audio per task, p50 | 8.8 to 14.3 ms | 7.8 to 10.0 ms |
| Audio per task, p99 | 22 to 41 ms | 11 to 20 ms |

Ranges are across windows; the render follows the scene. What they show:

- **The frame's p99 is twice its median**, in both modes. With the options on,
  one frame in a hundred takes four or five retraces instead of two. That is a
  visible stutter, and the mean of 37 ms hides it.
- **The default options' tail is the RDRAM snapshot and the statistics.** With
  them on, the audio's p99 reaches 41 ms, longer than one audio DMA (33 ms). With a
  sound driver, that risks an underrun. With both options on, the p99 stays at
  20 ms or below.
- The audio task's median, 8 to 10 ms of wall time, is above its 6.4 ms of
  processor time (`AUDIO-HLE.md`). The difference is presumably preemption by
  the graphics thread; this has not been measured separately.

### The export for offline analysis

`DKR_TIMING_EXPORT=<prefix>` writes one record per event
(`runtime-recomp/src/game/timing_export.hpp`). On the target the prefix is `D:\`,
the transfer disk:

- `FRAMES.BIN`, one record per display list: time, period, render, triangles.
- `AUDIO.BIN`, one record per audio task: time, the task's wall time, and the
  samples queued since boot.

Each stream has its own file because each has a single writer, and the target
has no `<mutex>`. Records are written and flushed 128 at a time. A run ends
with the machine being killed; only the last partial chunk is lost. The file is
capped at 131,072 records, 2 MB.

```
DKR_MEASURE_SET='DKR_TIMING_EXPORT=D:\' scripts/Measure-Guest-Time-VM.sh ...
mcopy -i transfer.img@@32256 ::/FRAMES.BIN ::/AUDIO.BIN .
tools/win95/timing_report.py FRAMES.BIN --skip-s 60 --csv frames.csv
tools/win95/timing_report.py AUDIO.BIN --skip-s 60
```

The report gives exact percentiles, not binned ones, and `--csv` gives every
record. Checked on the target: a 300 s run, 143 s of which is the game, with
both opt-in options. The file held 3,200 of 3,212 display lists and 3,584 of
3,597 audio tasks. Over the last 65 s:

| | p50 | p99 | worst |
|---|---:|---:|---:|
| Frame period | 33.9 ms | 76.6 ms | 983.5 ms |
| Render per list | 13.1 ms | 25.5 ms | 72.8 ms |
| Audio task | 7.3 ms | 18.8 ms | 21.3 ms |

The log's windowed percentiles, run for run, match those of a run without
the export (period p99 76 to 77 ms, against 67 to 81 ms), so its cost is
within the spread. The worst period, nearly a second, is a single hitch that
the windowed percentiles cannot show; the export finds it at its time.

The audio produces 49,078 samples a second, against 44,100 for 22,050 Hz
stereo. Without a sound driver the feedback reads idle and the audio manager
takes its largest quantum; this is the ratio to check again once the driver is
in (E06-S03).

### E08-S02, first measurement: the recompiled code's optimisation level

`DKR_WIN95_RECOMP_OPT` (CMake) adds a flag to the recompiled code. The game
thread's processor time, in exclusive mode over a steady window, with both
opt-in options on:

| Level | Game thread | Share |
|---|---:|---:|
| `-O3` (the default) | 4.49 ms a frame | 11.9% |
| `-O2` | 4.24 ms | 11.5% |
| `-Os` | 4.71 ms | 12.8% |

The spread is about 5%, which is the run-to-run spread. The windows were not the
same length either. **The recompiled game is 4.5 ms of a 37 ms frame**, since
the clock fix and at a correct game speed. E08-S02 has less to win than any other
item in this table. `-O3` stays.

One reservation. The ticket expects `-Os` to win on a real Pentium II, because
its instruction cache is 16 KB. Whether 86Box models instruction-cache misses is
not established. If it does not, that effect cannot show here in either
direction.
