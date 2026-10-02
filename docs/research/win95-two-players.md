# Windows 95 — two players on one machine

2 October 2026, on the test machine (86Box, Pentium II 400, Voodoo 2).

## A second player without a second pad

The test machine has no joystick behind its game port, and a 1998 PC often
had one at most. Player two is now on the **numeric keypad**: 8 4 2 6 the stick
(5 down as well), 0 A, the decimal point B, + Start, - Z, 7 L, 9 R; a second
joystick, when there is one, joins it, as the first joins the keyboard on
player one.

**Read by scan code, not by virtual key.** With NumLock off -- as the test
machine boots, and xdotool cannot switch it on there -- the keypad's 0 arrives
as `VK_INSERT` and its 8 as `VK_UP`: a temporary diagnostic printing every key
`GetAsyncKeyState` reported down showed exactly that, and `numlock=0`
throughout. The window procedure now keeps the keypad's keys by scan code,
which is the same whatever NumLock says, using the message's extended bit to
tell them from the dedicated arrows that share the codes
(`dkr_window_keypad_down`). Player one ignores an arrow whose keypad twin is
down, so pushing player two's stick does not also press player one's D-pad.

`scripts/Drive-To-Two-Player-Race.sh` walks the route: player two joins at
PLAYER SELECT, both pick a character, two players go straight to the track
choice, each picks a vehicle, six racers.

## What the first two-player race showed

Player one's half drew; **player two's was sky and white**. The oracle replay
of a capture (`CKEY1213.BIN`, F9) drew the same, so the defect was the
decoder's, not the card's. Its pixel probe on player two's road gave the
sequence, and it was two defects.

1. **A blend read as additive that passes the colour through.** DKR draws the
   sky with `FORCE_BL` and a second blender cycle of `P*0 + M*1` with M the
   incoming colour -- that is, the colour as it is -- and `0x0F0A4000`
   elsewhere does the same in both cycles. `rdp_state.c` read every `B` other
   than `1-A` as additive, so the sky was added onto whatever was below and
   saturated player two's view to white. It is now opaque when A is 0, B is 1
   and M is the incoming colour. In one-player scenes this changes the sky's
   tint only where it was drawn over something.
2. **The scissor was not applied.** `G_SETSCISSOR` had been decoded for a
   month and kept behind `DKR_SCISSOR=1`, because switching it on turned
   frames black "for a reason not yet named". Without it each player's
   geometry spilled over the other half, wrote depth there, and player two's
   own geometry then failed the depth test against player one's. The two
   reasons, now named and fixed:
   - DKR sets the window **before** the list's `G_SETCOLORIMAGE`
     (`SetScissor 0,0..319,239` then `SetColorImage width=320`), when the
     buffer-to-screen scale is not known yet: the window went down at a quarter
     of the screen. A scissor that arrives first is now held and applied when
     the width does.
   - The card's clip window persisted from one list to the next, and
     `grBufferClear` respects it. Both backends now reset it to the whole
     buffer at the start of each frame.

   The scissor is on by default; `DKR_SCISSOR=0` turns it off.

## What else the scissor changes

Replayed through the oracle, 47 of the 66 captures at hand come out the same
with and without it. Of the rest, the large differences are improvements the
console has and the port lacked:

- the opening screens (Nintendo logo, copyright) are **letterboxed**, black
  bands above and below, as on the N64;
- the track choice shows its **sand background**, the course's flyover inside
  the frame only, where it covered the whole menu;
- a two-player race draws both views.

The small ones -- tens to two thousand pixels -- are edges that now stop at
the window.

## How fast

A two-player race at Ancient Lake, six racers, both accelerating, 77 seconds
of the timing export: **38.9 ms a frame on average, median 33.7 ms**, 77% of
frames at the full 30 fps, 14% at 20 and 9% at 15; the renderer 6.9 ms.

**A one-player race is not slowed by the scissor**: driven by the same script,
the timing export's first 40 seconds of race gave 37.1 and 37.2 ms a frame
before the change, in runs on either side, and 37.4 ms after, on a busier
stretch (1011 triangles a list against 732). So two players cost about 1.5 ms
a frame over one, 4% -- most frames still at 30 fps.
