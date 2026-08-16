# What time base Windows 95 really offers

Measurements from
[E02-S03](../stories/E02-system/E02-S03-clock-timers-and-pacing.md), taken on the
test machine — Windows 95 OSR2, emulated Pentium II 400 MHz. Probe:
`tools/win95/witnesses/clock_probe.cpp`.

The ticket drew up a table of candidate sources with, for each, an *assumed*
resolution. Two of those assumptions are false, and they change the design.

## The report

```text
QueryPerformanceFrequency : 1193180 Hz
timeGetDevCaps            : period from 1 to 65535 ms

Observed resolution (smallest non-zero step)
  GetTickCount            : 9 ms
  timeGetTime  (before)   : 1 ms
  timeGetTime  (after timeBeginPeriod(1)) : 1 ms
  QueryPerformanceCounter : 5 ticks = 4.190 us

Monotonicity over 200000 consecutive reads
  QueryPerformanceCounter : 0 step(s) backwards
  timeGetTime             : 0 step(s) backwards

Cost per call - order of magnitude under emulation, NOT transferable
  GetTickCount            : 45 ns
  timeGetTime             : 6085 ns
  QueryPerformanceCounter : 4685 ns
```

The resolution is not asked of the system, it is **observed**: the source is read
in a loop until its value changes, and the observed gap is the real granularity. A
source that announces the microsecond and only advances every 55 ms is precisely
the trap this method avoids.

## What the frequency says about the counter's origin

`QueryPerformanceFrequency` returns **1,193,180 Hz**. That is not just any value:
it is the **8254 PIT**'s frequency, 1.193182 MHz, the 14.31818 MHz oscillator
divided by twelve. Windows 95 therefore does not build
`QueryPerformanceCounter` on the processor's cycle counter but on the programmable
interval timer, read through I/O accesses.

Three consequences, and the third is the most important:

1. **The measured resolution, 4.19 µs, is 5 PIT ticks.** That is not the counter's
   period — one tick is 0.838 µs — but the time a read takes. One cannot timestamp
   more finely than the cost of measuring.

2. **The cost is explained**: reading the PIT goes through ISA I/O accesses, slow
   by nature. `GetTickCount`, for its part, reads a variable in shared memory
   without a context switch, hence the two orders of magnitude between them.

3. **The low 32 bits wrap in exactly 60 minutes.** 2³² ÷ 1,193,180 = 3,600 s. The
   API returns 64 bits and Windows 95 extends the counter, but it is a fact better
   known before than after: a play session commonly exceeds an hour.

## The two assumptions disproved

### `timeBeginPeriod(1)` changes nothing here

The ticket gave it as the means of obtaining the millisecond. `timeGetTime`
already returns the millisecond **before** any setting, and `timeGetDevCaps`
announces a minimum period of 1 ms.

The call is made nonetheless, and above all **released**: nothing guarantees the
same holds on another machine, and under Windows 9x a setting left in place
degrades the whole system until the next reboot — including after the end of the
process that set it.

### `GetTickCount` is far finer than 55 ms — and far cheaper

The ticket announced it at "~55 ms", the DOS tick period at 18.2 Hz. The
measurement gives **9 ms**, and a cost **a hundred times lower** than the two
other sources.

It stays too coarse to pace 30 frames per second — a 9 ms step is more than a
quarter of a frame — but it is the right tool wherever coarse timestamping
suffices, and the cost difference is wide enough for the question to arise at every
call site. The figure goes into
[E08-S01](../stories/E08-perf/E08-S01-frame-budget-instrumentation.md)'s budget.

> **A reservation.** The 9 ms are measured under 86Box. `GetTickCount`'s
> granularity under Windows 9x depends on the system timer, which the emulator
> reproduces functionally and not temporally.
> [E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md) will settle it
> on real hardware. The design choice does not depend on it: `GetTickCount` is
> retained as the main source in no case.

## What was retained

`QueryPerformanceCounter` as the main source — 4.19 µs, monotonic over 200,000
reads — with a fallback to `timeGetTime` if it turns out to be absent or
inconsistent at startup. The validation is not a formality: a source that steps
backwards, even by one tick, is set aside in favour of the fallback rather than
producing a stopwatch that jumps mid-game.

The fallback is a 32-bit counter that wraps after 49.7 days. It is accumulated by
`dkr_tick64_step`, [E01-S03](../WIN95-COMPAT.md)'s pure function — reused and not
copied, because a second copy of the same reasoning always ends up diverging from
the first.

Implementation and contract: `platform/win95/clock.{h,cpp}`.

## The defect this measurement brought to light

`ultramodern` derives `osGetCount` — hence **all** of DKR's timekeeping — from
`std::chrono::high_resolution_clock` (`timer.cpp:65`). What remains is to know
which clock that really is. Measured on the target:

```text
is_steady=false  system_clock=YES  steady_clock=no
```

**It is the wall clock.** On this toolchain, `high_resolution_clock` is an alias of
`system_clock`, and `is_steady` is false: it steps backwards when the user changes
the time, and when Windows applies the switch to winter time. The VR4300's cycle
counter, on which the pacing, the race timings and the audio timing all rest,
inherits those jumps.

The irony is instructive: `timer.cpp` carries, ten lines below, a comment
explaining that the Windows branch avoids `std::chrono::sleep_until` *precisely*
because the implementations "have been affected by the system clock stepping
backwards". The precaution was taken on the wait, not on the counter.

This is not specific to Windows 95 — it is true on all of `ultramodern`'s
platforms — but it is here that it can be corrected without risk, since
`platform/win95/clock.{h,cpp}` offers a monotonic base whose drift is measured.
**Wiring `osGetCount` onto it is therefore justified by a measurement and not by a
concern for tidiness**, and it is E02-S03's point 4.

## Reproducing

```sh
i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 \
  -o CLOCK.EXE tools/win95/witnesses/clock_probe.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive -lwinmm
scripts/Push-To-Win95-VM.sh CLOCK.EXE
# in the guest: d:\clock.exe - the report lands in D:\CLOCK.TXT
```
