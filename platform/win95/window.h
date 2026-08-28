#ifndef DKR_WIN95_WINDOW_H
#define DKR_WIN95_WINDOW_H

/* E06-S01 — the Win32 window and its message loop.
 *
 * **The window is a message receiver, not a rendering surface.** On a Voodoo 1 or
 * 2 the 3D card is a *passthrough* accelerator: it seizes the screen full-screen
 * through an analogue relay, and the desktop — with this window on it — stays on
 * the 2D card, invisible while the game draws. Nothing is composed, nothing is
 * blitted here. What the window is for is the keyboard, the focus and the orderly
 * shutdown, none of which Windows delivers to a process that owns no window.
 *
 * That is why this is small, and why it must exist all the same: without it the
 * port renders the game's attract mode to a player who cannot press Start.
 *
 * Everything here is ANSI (`RegisterClassA`, `CreateWindowExA`, `PeekMessageA`).
 * Windows 95's wide API is a set of stubs — E02-S01 measured `CreateSemaphoreW`
 * exported and empty — and the guard rail in E01-S04 refuses the W variants for
 * that reason.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the class and the window and shows it. Returns 1 on success.
   Calling it twice is a no-op and returns 1: the game's bring-up path is not
   linear and a second call must not destroy the first window. */
int  dkr_window_open(const char *title);

/* Drains the queue without blocking. Returns 0 once a shutdown has been asked
   for — `WM_CLOSE`, `Alt+F4`, `WM_QUIT`, the end of the session — and 1 otherwise.
 *
 * **It must not block**, and that is the design point this ticket names. The game
 * runs in the threads `ultramodern` manages; the main thread already polls them
 * every millisecond in `DkrMain`. `GetMessage` would park that thread until a
 * message arrived, and a machine with no input produces none — the game would
 * stop being watched. `PeekMessage` in that existing loop costs nothing when the
 * queue is empty and keeps the one-millisecond cadence that is already there. */
int  dkr_window_pump(void);

/* Non-zero while this application holds the foreground. The pacing (E06-S04) and
   the pause on focus loss read it; it is exposed rather than acted on here
   because "pause the game" is a decision about the game, not about the window. */
int  dkr_window_focused(void);

/* The window handle, as an integer, or 0 if there is none.
 *
 * **This is what `grSstWinOpen` wants**, and passing it zero — which this port did
 * until 28 August 2026 — is not equivalent. Glide binds its full-screen context to
 * the window it is given: that window then keeps the focus, receives the keyboard
 * and is what the task switcher switches back to. With a zero handle Glide owns a
 * screen that belongs to no window, and a window created afterwards competes with
 * it for the foreground rather than owning it.
 *
 * Returned as `unsigned long` so that this header stays free of `windows.h`: it is
 * included by the renderer, which has no business seeing the Win32 API. */
unsigned long dkr_window_handle(void);

/* Restores the cursor and destroys the window. Safe to call without a window. */
void dkr_window_close(void);

/* --- The keyboard, captured where the messages arrive ----------------------- *
 *
 * The key state belongs to this file because `WM_KEYDOWN` is a window message and
 * arrives nowhere else. What it *means* — which key is the A button — is
 * E06-S02's, and lives with the rest of the controller mapping.
 *
 * **Message-based rather than `GetAsyncKeyState`.** The asynchronous call reads
 * the physical keyboard whoever owns the foreground, so a game that has lost
 * focus would go on being driven by whatever the player types into another
 * window. Messages stop arriving when focus is lost, which is the behaviour that
 * is wanted and which costs nothing to obtain.
 *
 * **A key is reported down if it is down now *or* if it went down since the last
 * `dkr_window_latch_clear`.** That latch is not a convenience; it is what makes
 * the keyboard work at all at this frame rate.
 *
 * The game reads its controller once per frame, and E00-S03 measured a frame at
 * **170 ms**. A key pressed and released inside that window is invisible to a
 * poll that samples the instantaneous state: measured on the machine on 28 August
 * 2026, four keystrokes sent and **one** seen, which is exactly what a 10 ms tap
 * against a 170 ms sampling interval predicts. On the console the controller is
 * read at 60 Hz and the question does not arise.
 *
 * The cost of the latch is that a very short tap is reported for a whole frame
 * rather than for part of one, which is the right trade: a press held slightly
 * too long is a press, and a press dropped is a player pushing the button again.
 *
 * `vk` is a Windows virtual-key code. Out-of-range codes answer 0 rather than
 * reading past the array. */
int  dkr_window_key_down(int vk);

/* Forgets what was pressed-and-released, keeping what is still held. Called by
   the input poll once it has read the state -- the latch spans exactly one poll,
   so it cannot accumulate a press the game never asked about. */
void dkr_window_latch_clear(void);

/* Clears every key. Called on focus loss: a key held when the window went away
   never gets its `WM_KEYUP`, and would stay pressed for ever — the accelerator
   stuck on after an `Alt+Tab`. */
void dkr_window_keys_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_WINDOW_H */
