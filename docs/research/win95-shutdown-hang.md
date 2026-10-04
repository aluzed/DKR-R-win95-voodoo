# The stop that did not finish

3 October 2026. E02-S02, E06-S01.

## The symptom

On the test machine, about half the stops asked with `Alt+F4` never finished.
The display came back -- Glide was closed -- and the saves were written, but the
process stayed, and only the task list could end it. Of 173 saved logs of runs
that asked to stop, 32 ended on `[gfx] Glide closed`; run again with 45 to 60 s
left before the machine was stopped instead of 10, 4 of 8 still did.

## Where it was

**In `dkr_window_close`, on `DestroyWindow`.** Proved with one binary and an
environment switch that skips the call, so that the two conditions share the
same code to the byte, interleaved on the test machine:

| | with `DestroyWindow` | without |
|---|---|---|
| runs | 3 | 3 |
| stopped cleanly | 0 | 3 |

Each hung run's log reaches `[boot] runtime thread joined` -- the runtime has
stopped, every thread it owns has been joined -- and nothing after. Between that
line and `[boot] runtime stopped cleanly` the port does three things: restore
the cursor, destroy the window, and reset the Controller Pak session, which is a
no-op on this target (the session directory is only ever set by the mod system,
which it does not carry).

With the call removed, the task list 25 s after `Alt+F4` holds only Explorer,
Systray and the finished batch window: the process is gone.

**Why `DestroyWindow` waits, and on what, is not established.** It runs while
the game's guest threads -- never joined, woken by the quit -- are unwinding and
exiting, and USER on Windows 95 serialises on the Win16 mutex; a thread exit
that needs it while the destroy holds it, or the reverse, would fit. The fix
does not depend on knowing: `ExitProcess` ends every other thread before
Windows tears the window down, so the race is gone rather than made rarer.

## The fix

`dkr_window_close` restores the cursor and lets go of the window; the process's
exit destroys it, a moment later. Glide is closed before this point, so the
desktop is already back.

## What made it slow to find

**The instrument hid it.** Logging and committing the log at each step of the
shutdown (patch 0064's first version) shifted the timing enough that 16 runs
in 16 stopped cleanly, and so did a watchdog thread started at the quit (4 in
4) and one started at boot (2 in 2). Under 86Box the timing of one binary is close to
deterministic: the build that hung did so six times in six, and a build that
differed by six atomic stores in `recomp::start` never did. A race whose
outcome follows the code's layout reads as fixed by any change near it.

**The A/B that settled it kept the layout fixed** -- one binary, the call
switched by an environment variable -- and that is the method to reach for the
next time a hang comes and goes with unrelated changes.

**Two hypotheses tested and dropped.** A guest thread waiting for a
synchronous draw (`DKR_RDRAM_SNAPSHOT=none`) that the graphics thread would
never make, holding the guest token: plausible from the code, and a bounded
wait was written for it, but the logs placed the hang after the runtime had
returned, so it was not committed. A `join` in `recomp::start` never returning:
the runs that hung in the window all logged `runtime thread joined`, which
`recomp::start` returning is the condition for.

**But an earlier build hung before that.** The logs of the build of 13:18,
which wrote and committed `[boot] runtime finished` as soon as the runtime
returned, end on `Glide closed` without it in 3 runs of 5: in that layout
`recomp::start` itself did not return. That is a second place, and the window
fix does not touch it; see "The second hang" below.

## What stays

Patch 0064 marks each step of the runtime's shutdown and the exits of the
graphics, VI and RSP task threads in a bit field, without I/O; the main thread
prints it with `[boot] runtime finished` and, if a stop is still running ten
seconds after the quit, reports it. `DKR_MEASURE_QUIT_WAIT` sets how long
`Measure-Guest-Time-VM.sh` leaves a stop before it stops the machine.

## The second hang -- found on 4 October 2026

**A guest thread read freed RDRAM.** The E06-S01 changes moved the timing,
and the second hang became frequent: 5 stops in 6 left the process
behind, the log ending on `runtime finished`, the watchdog silent. The
crash filter's log (`DKR-BOOT.LOG`, which nothing had been reading) ended
on `*** unhandled exception ***` and no more: the filter itself stalled on
the C runtime right after its heading. Given a first record built without
the C runtime, it named the fault, the same in every run:

    *** fault code=C0000005 at=0083933A touching=0213F92C esp=0515FE88

`0x0083933A` is `_thread_func`, the body of every guest thread, right after
`run_thread_function` returns: `cmp %ebx,0x1c(%edi)`, the test
`self->context == thread_context`. `self` is the guest's `OSThread`, which
lives in RDRAM. At a quit the guest threads are never joined; woken, they
unwind on `thread_terminated` and read their `OSThread` on the way out --
and `recomp::start`, once its own threads were joined, freed RDRAM. A guest
thread still unwinding at that moment faulted.

**Fixed by patch 0066**: the guest threads alive are counted from their
creation to the last line of `_thread_func`, and RDRAM is freed only when
none is left. Five of DKR's never are -- blocked in waits the quit does not
reach -- so the wait ends when the count has held still for 250 ms, and
RDRAM is kept for the process's exit to return (`[boot][stop] 5 guest
thread(s) still running: RDRAM kept`). Before: 5 stops in 6 hung. After: 15
in 15 clean, launched from Explorer and from a batch file, no fault record.

This is also the likeliest identity of the hang seen in the build of 13:18
on 3 October, inside `recomp::start`: the same race, met earlier. Not
proven for that build; it has not been seen since the fix.

**And perhaps of the first one too.** The hang in `DestroyWindow` (above)
came after RDRAM was freed, with the guest threads still unwinding; a guest
thread faulting then would run the crash filter, which calls USER
(`MessageBoxA`) and the display cleanup, while the main thread was in
`DestroyWindow`, also USER. The A/B proved that the call decided whether the
process stopped, not that it was the cause; which it was is not established.
Leaving the window to the process's exit stays right either way.

**The crash filter writes its essentials first.** `on_unhandled` in
`platform/win95/startup.c` now begins with one line formatted by hand and
written with `WriteFile` -- code, address, the address touched, the stack
pointer, the thread -- before any call into the C runtime, which a fault
during a thread's end may find locked.

## The safety net

So that no stop, this one or the next, can leave a process behind, the main
thread starts a watchdog thread at the quit (`StopWatchdog`, `game_main.cpp`).
A process that ends takes it along; one still alive fifteen seconds after the
quit gets a log line with the runtime's marks and `TerminateProcess`. It runs
above every thread of the game, and after its sleep it calls the kernel only
-- the line built by hand, written with `WriteFile` to a handle on the log it
opened at the quit -- because the C runtime may be the thing that is stuck:
tried with a build that holds `stderr`'s lock for ever after the quit, the
line is written and the process ended. The saves are safe at any instant -- every write is a
durable replacement that survives a power cut (E02-S05) -- and Glide closes
before any of the hangs seen.

Tried with a deliberate hang (a build that sleeps for ever after
`recomp::start` when `DKR_TEST_STOP_HANG` is set, not kept in the tree): the
log reads `not finished 15 s after the quit (marks=0xF3F); ending the
process`, and the game launched again in the same Windows session opened the
sound card and the Voodoo as usual -- `TerminateProcess` leaves no device
held. That relaunch, from the Run box, hit the watchdog as well; the same
binary launched from the Run box in fresh sessions stopped cleanly twice in
two, so the test variable most likely reached the relaunch, but that is not
established.
