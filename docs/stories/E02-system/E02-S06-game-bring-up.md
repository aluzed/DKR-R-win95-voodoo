# E02-S06 — Bring-up: from the Win32 entry point to the game's first call

| | |
|---|---|
| **Epic** | E02 — Windows 95 system substrate |
| **Status** | TODO |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E01-S05, E02-S02, E02-S04 |
| **Blocks** | E04-S08, E09-S02 |

## Context

This is the milestone that proves E01 and E02 stand up: the game's recompiled code
really runs under Windows 95. Without rendering, without audio, without inputs — but
it runs, and it submits graphics tasks.

`game_main.cpp` (22 KB) and `game_registration.cpp` orchestrate that bring-up today
around SDL2: creating the window, selecting the ROM through the launcher,
initialising the renderer, then handing control over to `librecomp`. The same
sequence is needed, without SDL2 and without RT64.

The diagnostic renderer (`null_renderer.cpp`) is exactly this milestone's tool: it
implements `RendererContext`, counts the display lists and the presentations, and
displays nothing. It allows the whole CPU path to be validated before a single line
of Glide is written.

## Objective

To make the recompiled game start under Windows 95 up to the regular submission of
graphics tasks, with the diagnostic renderer.

## Scope

**In:** the bring-up, the game's registration, the wiring of the diagnostic renderer,
the startup log.

**Out:** window (E06-S01), inputs (E06-S02), audio (E06-S03), rendering (E04, E05).

## Work

1. Write `platform/win95/main.cpp`: entry point, initialisation of the compatibility
   layer (E01-S03), of the clock (E02-S03), of the log.
2. Take from `game_main.cpp` the sequence that registers the game with `librecomp`
   and starts it, removing SDL2. Split rather than duplicate: the registration logic
   must stay shared with the modern target, failing which the two drift.
3. Resolve the ROM without a launcher: a command-line argument, or a path read from
   the configuration, or a file of agreed name in the application's folder. E06-S06
   will deal with the ergonomics; here, the simplest thing suffices.
4. Instantiate `DiagnosticRenderer` as the rendering context. Check that it depends
   neither on SDL2 nor on RT64 — its header includes only `ultramodern`, which bodes
   well, but its source file is to be checked.
5. Run the loop. `DiagnosticRenderer`'s display-list counter must advance steadily:
   that is the sign that the game thread is alive, that the scheduler switches, and
   that the game produces frames.
6. Write a detailed startup log to a file: each step passed, each thread created,
   each task submitted. It is the only diagnostic tool available on the target
   machine.
7. Measure, with `DiagnosticRenderer`'s counters, the rate at which graphics tasks
   are submitted, and compare it against the expected 30 per second. It is the
   project's first real performance measurement on the target, and it confronts
   E00-S03's extrapolation directly with the facts.

## Acceptance criteria

- [ ] The recompiled game starts under emulated Windows 95.
- [ ] `DiagnosticRenderer`'s display-list counter advances steadily.
- [ ] The submission rate is measured and compared against E00-S03's forecast.
- [ ] The startup log traces every step and is readable from the target machine.
- [ ] No dependency on SDL2, ImGui or RT64 in the binary produced — verified by the
      import table (E01-S04).
- [ ] The game's registration logic stays shared with the modern target.
- [ ] The game reaches at least the title screen from the CPU's point of view, that
      is, submits the corresponding graphics tasks.

## Risks

The gap between the rate measured here and E00-S03's forecast is the project's most
important piece of information at this stage. If it is bad, it is better discovered
now, before the two heaviest epics (E04 and E05), and the hardware-floor ADR
reopened.

## References

- `runtime-recomp/src/game/game_main.cpp`, `game_registration.cpp`
- `runtime-recomp/src/game/null_renderer.{hpp,cpp}`
- `docs/ARCHITECTURE.md` — the complete execution path
