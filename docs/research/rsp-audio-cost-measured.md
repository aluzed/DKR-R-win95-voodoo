# What the recompiled RSP audio microcode actually costs

E03-S03 — the high-level audio mixer — is an XL ticket that trades away bit-exact
fidelity. Its own risk section is explicit about when it may be started:

> It must not be undertaken out of convenience or anticipation — only on the
> strength of a measurement proving the faithful path does not hold.

Until now the trigger rested on E00-S04, which measured the target at 3.9 % of
the RSP's vector throughput and **extrapolated** 43 ms per frame against a
33.3 ms budget. An extrapolation is not the thing itself, and the game now runs
on the machine with its audio, so the cost can be measured directly.

## Method

`task_thread_func` in `ultramodern/src/events.cpp` is where the recompiled
microcode runs, one call per RSP task. It is timed around
`ultramodern::rsp::run_task`, and the probe reports **total, worst case and the
count of tasks over budget** rather than a mean.

The worst case is the number that decides. An audio task that fits on average
and overruns on a busy frame is an audio task that stutters, and a mean would
average exactly that away.

## The probe changed what it measured, first time round

The first version called `dkr_diag_commit()` from the reporting path — which
forces `FlushFileBuffers`. On the SP task thread, the one the game's audio
depends on, writing through to an emulated disk stalls for tens of milliseconds.

The game crashed within 137 task submissions, in the scheduler, on a task
pointer that had walked out of RDRAM:

    code    : 0xC0000005 (invalid memory access)
    touching: 0x82380010 on read
    esi=80121260            <- gMainSched

That crash was manufactured by the measurement. It is recorded here because it
looked exactly like a defect of the port, and because it is the general hazard
of instrumenting a timing-sensitive path: a probe that perturbs what it measures
is worse than no probe at all. The message-queue trace already flushes the log
often enough; the RSP path must not.

## Result, on the machine

    [rsp] tasks=840 type=2 total=17570 ms worst=110000 us over-33ms=304

Type 2 is `M_AUDTASK`.

| | |
|---|---|
| tasks measured | 840 |
| mean per task | **20.9 ms** |
| worst case | **110 ms** |
| tasks over the 33.3 ms frame budget | **304 of 840 — 36 %** |

DKR runs its logic at 30 Hz — `sched.c` increments `frameCount` by one per
retrace, with Rare's own comment noting that 60 fps would require two — so the
frame budget is 33.3 ms.

## What this establishes

**The faithful path does not hold, and the trigger is now founded on a
measurement rather than an extrapolation.**

The audio microcode alone consumes 63 % of the frame budget on average, and
overruns it outright on more than a third of tasks — before a single triangle is
drawn. The rendering measured elsewhere in this session already peaks at 270 ms
per display list, so the two together are not close to fitting.

Two details worth keeping, because they sharpen the case rather than merely
confirming it:

- **The mean is not the problem; the distribution is.** 20.9 ms average would
  almost fit. 36 % of tasks over budget cannot, and no amount of average-case
  optimisation addresses that shape.
- **The measured number differs from the extrapolated one** — 20.9 ms average
  against 43 ms predicted — and the conclusion holds anyway. Had it come out
  under budget, the extrapolation would have been wrong in the direction that
  costs an XL ticket for nothing.

## Consequence for E03

- **E03-S01** (vector unit without SSE) is already self-declared largely
  obsolete: the scalar fallback exists in `librecomp` and selects itself on
  32-bit targets, and MMX's four lanes against SSE2's eight would not close a
  gap of this size.
- **E03-S03** is confirmed triggered, by this measurement.
- The recompiled path stays available and selectable, as the ticket requires:
  it is the bit-exact oracle the mixer will be compared against, and it works
  today — just not in real time.
