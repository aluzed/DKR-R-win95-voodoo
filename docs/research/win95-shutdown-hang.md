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

## The second hang

The build of 13:18 (`53ab788`, patches up to 0063) rebuilt as it was hangs 3
runs in 4 **before** `[boot] runtime finished`: there, `recomp::start` does
not return. Tested so far:

- the synchronous-draw wait bounded (an environment switch in one binary):
  the bounded wait never fired, in the runs that stopped as in the runs that
  did not, so it is not the cause; the two clean runs out of three with the
  switch on came from the switch's own effect on the timing;
- the same build with marks at every step of `recomp::start`: the second hang
  no longer shows, and the first one, in the window, does.

It has not been seen in the build that carries the window fix: 6 stops in 6
and one more checked in the task list. That proves nothing about it, for the
reason above, and it stays open until it is located.
