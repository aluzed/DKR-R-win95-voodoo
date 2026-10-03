# E06-S01 — Win32 window and message loop

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | REVIEW |
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

## Acceptance criteria

- [x] The window is created and receives the system's messages under Windows 95.
      Since 28 August (`docs/research/win95-game-render.md`, "A window, a
      keyboard"); every run on the test machine ends through its `Alt+F4`.
- [x] The message loop does not interfere with the game's threads and does not consume
      CPU when idle. The sampler found the main thread moving in one of
      88,767 samples of a race (E08-S01).
- [ ] Losing focus pauses the game, regaining it resumes.
- [ ] Every shutdown route leads to a clean stop with the display restored.
      *The display is restored on `Alt+F4`, and the stop now finishes*: the
      hang that kept half the processes alive was `DestroyWindow` racing the
      guest threads' exit, fixed on 3 October 2026 (6 stops in 6, 4 in 4
      from Explorer), and a watchdog ends the process if a stop is still
      running fifteen seconds after the quit. Left open
      for a second hang seen only in an older build, and for the close button
      and the session's end, not tried on their own
      (`docs/research/win95-shutdown-hang.md`).
- [ ] The cursor is hidden in full screen and restored on exit.
- [ ] The behaviour on task switching is decided and held to.
- [ ] A crash restores the display and writes a log.
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
