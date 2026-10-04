# E06-S01 — Win32 window and message loop

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | DONE |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E01-S03, E02-S06 |
| **Blocks** | E06-S02, E05-S01, E07-S03 |

## Context

SDL2 supplies the window, the event loop, the inputs and the audio today. It
disappears, and what it brought has to be rewritten — in raw Win32, as it was done in
1996.

The use case is happily simple, because the display mode is. On Voodoo 1 and 2, the 3D
card is a *passthrough* accelerator: it takes control of the screen full screen, and
the Win32 window serves only to receive the system's messages — keyboard, focus,
shutdown. There is no rendering to compose with the desktop. On Banshee and Voodoo 3,
complete 2D/3D cards, a windowed mode becomes possible, but it is not indispensable to
the project.

That simplifies things a great deal: the window is a message receiver, not a rendering
surface.

## Objective

To deliver `platform/win95/window.{h,cpp}`: creating the window, the message loop,
handling focus and shutdown.

## Scope

**In:** window, message loop, focus, shutdown, cursor.

**Out:** the inputs (E06-S02), the audio (E06-S03), the pacing (E06-S04).

## Work

1. Create the window class and the window. Decide on its visibility according to the
   display mode: on a passthrough card, it can stay minimal.
2. Write the message loop, and decide how it articulates with the game loop. The game
   runs in the threads `ultramodern` manages; the message loop must run on the thread
   that created the window, without blocking it and without consuming CPU needlessly.
   That is this ticket's design point.
3. Deal with focus. Under Windows 95, losing focus in accelerated full screen requires
   an explicit decision: pause the game, or continue. Pausing is the expected
   behaviour.
4. Deal with shutdown: closing the window, `Alt+F4`, system shutdown. Every route must
   lead to a clean stop, with the display restored (E05-S01) and the state saved if
   necessary.
5. Manage the cursor: hidden in full screen, restored on exit.
6. Deal with `Alt+Tab` and task switching, which under Windows 95 with a passthrough
   card can leave the display in an inconsistent state. Decide on the behaviour —
   block the switch, or handle it cleanly — and hold to it.
7. Install a safety net: a structured exception handler that restores the display and
   writes a log before handing back. Without it, every crash in development costs a
   reboot of the machine.
8. Check that no API later than Windows 95 is used — E01-S04's guard rail does it
   automatically.

## Where it stands (4 October 2026)

**Focus and task switching.** Losing the foreground -- `Alt+Tab`, the Start
menu -- pauses the game, and the screen goes back to the desktop; getting it
back resumes the game and takes the screen again. The pause holds back the
game's retraces in the runtime's VI thread (patch 0065, asking
`dkr_game_paused()` in `window.c`): DKR advances on them, so its logic stops
where it is and nothing is owed afterwards. The screen is handed over with
`grSstControl(GR_CONTROL_DEACTIVATE)` and taken back with `ACTIVATE`, on the
graphics thread, keeping the context. On the test machine: the log reads
`lost the foreground: the game pauses`, `screen handed to the desktop
(grSstControl done)`, then the reverse; across a 20 s pause the game logged
nothing at all, and its idle thread's next report covered 20,559 ms instead
of its usual five seconds; the screenshots show the desktop during the pause
and the game moving again after it. **Decided and held to: switching away
pauses; switching back resumes.**

**A Windows program.** `DKRR.EXE` is linked as a GUI program (`-mwindows`).
As a console program it got a console window, which Windows 95 treats as a
DOS session: shutting Windows down stopped on "you must quit this program". For the test bench it means `MEASURE.BAT` returns as soon as the game
has started, and its window closes: `Alt+Tab` then has nothing to switch to,
and a pause is tried through the Start menu instead.

**Every shutdown route.**

- `Alt+F4`, the window's close button and the taskbar's Close all arrive as
  `WM_CLOSE` (the first two by way of `SC_CLOSE`), one handler; `Alt+F4` was
  run dozens of times.
- Windows shutting down, tried with the game paused behind the Start menu:
  `WM_QUERYENDSESSION` is answered yes and starts the stop; the window keeps
  being pumped meanwhile, and `WM_ENDSESSION` waits up to ten seconds for the
  runtime to have stopped before letting Windows end the process. Log:
  `the runtime has stopped: the session may end`; the machine then turned
  itself off.
- **The stop's hang, found** (`docs/research/win95-shutdown-hang.md`): a
  guest thread still unwinding read its `OSThread` in RDRAM after
  `recomp::start` had freed it. Patch 0066 frees RDRAM only once the guest
  threads have ended, and keeps it when five of them, blocked for good, never
  do. Before: 5 stops in 6 left the process behind. After: 15 in 15 clean,
  from Explorer and from a batch file, with no fault recorded.
- A watchdog started at the quit ends a process still alive fifteen seconds
  later, with kernel calls only; tried against a deliberately held lock of the
  C runtime.

**The cursor.** Hidden over the window by answering `WM_SETCURSOR` with no
cursor -- not `ShowCursor`, whose global count a crash would leave hidden.
On a passthrough card the desktop's pointer is not on the screen while the
game is; it is there during a pause, after a quit and after a crash (the
screenshots show it).

**A crash.** Tried with a deliberate access violation on the graphics
thread, the card open: the screen came back to the desktop, a message box
in front of everything says `DKR-R stopped: invalid memory access. Details
in DKR-BOOT.LOG.`, and that log holds the fault, the registers, the
plausible return addresses on the stack and `abnormal-exit cleanups run`.
Three defects of the filter were found and fixed on the way: it called the
C runtime before writing anything, and a fault during a thread's end found
it locked, so it now writes a first record by hand; its stack scan ran past
the top of the stack and faulted inside the filter; and its message box
was hidden behind the game's window.

## Acceptance criteria

- [x] The window is created and receives the system's messages under Windows 95.
      Since 28 August (`docs/research/win95-game-render.md`, "A window, a
      keyboard"); every run on the test machine ends through its `Alt+F4`.
- [x] The message loop does not interfere with the game's threads and does not consume
      CPU when idle. The sampler found the main thread moving in one of
      88,767 samples of a race (E08-S01).
- [x] Losing focus pauses the game, regaining it resumes -- above.
- [x] Every shutdown route leads to a clean stop with the display restored --
      `Alt+F4` and Windows' shutdown run on the test machine; the close button
      and the taskbar's Close share `Alt+F4`'s handler and were not clicked.
- [x] The cursor is hidden in full screen and restored on exit -- above.
- [x] The behaviour on task switching is decided and held to: switching away
      pauses and hands the screen back; switching back resumes.
- [x] A crash restores the display and writes a log -- tried, above.
- [x] No API later than Windows 95 is imported. `check_imports.py` at every build and package (E01-S04).

## Risks

The articulation between the Win32 message loop and the `ultramodern` scheduler is the
delicate point. A message loop that does not run often enough makes the system inert
from Windows's point of view; a loop that runs too much steals time from the game. That
balance is measured (E08-S01), it is not set by guesswork.

## References

- `runtime-recomp/src/game/runtime_platform.cpp` — the current SDL integration, 33 KB,
  to be replaced
- E05-S01 — Glide display mode
- E01-S04 — import guard rail
