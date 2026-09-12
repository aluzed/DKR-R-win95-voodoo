# The port plays

Driven on 12 September 2026 from the host, with no screen and no hands:
`scripts/Drive-Win95-VM.sh key`, into a build that renders on the Voodoo.

## What happened

| | |
|---|---|
| the title screen | the logo, the kart, `START` and `OPTIONS` |
| `Return` ×n, `space` ×n | **GAME SELECT** — `ADVENTURE` and `TRACKS`, on their wooden plaques |
| `Down`, `space` | the file screen — three `NEW` slots, `ENTER YOUR INITIALS`, the letter carousel, `AA` already typed by the presses that got there |
| `Left`, `space` | Timber's Island loading in: sky, foliage, a dirt path |

Every screen is the card's own output. The keyboard is E06-S02's, message-based
with the latch that E06-S01 measured as necessary at 170 ms a frame — four
keystrokes sent and one seen, before it existed.

This is the first time the port has been **played** rather than watched. Until
now every image in this repository came from the game driving itself: the intro,
the attract sequence, a demo race. These came from keys.

## What it establishes, and what it does not

It establishes that the chain holds through menu navigation — text, plaques, a
carousel, a 3D hub loading — and that the input path reaches the game's own menu
logic rather than merely arriving in a buffer.

It does **not** establish a lap of a level, which is what E09-S02's corpus still
lacks. And the reason is worth recording precisely, because it cost a run:

> **`gGameMode` stays at 1 (MENU) through Timber's Island.** The hub's fly-in, the
> foliage, the path — all of it is drawn under MENU, with only `level` changing
> from `0x801FB780`. A capture anchored on `DKR_CAPTURE_MODE=0` (INGAME) therefore
> never fires there, and mode 0 means a race proper.

So the corpus's first gameplay scene needs the navigation carried further. And
carrying it further ran into something worth its own section.

## The buttons arrive; the directions arrive sometimes

Navigating properly means *choosing* menu items, not accepting the first one, and
that needs a direction. Measured screen by screen on the letter carousel, where a
move is unmistakable — the strip reads `? SP DEL Ok A B C D` and the selection is
visible:

| sent | effect |
|---|---|
| `space` (A), `Return` (Start) | every time: letters typed, screens advanced, five screens deep |
| `d` `d` `d` — the stick right | the selection moved **A → D** |
| `Right` `Right` — the D-pad | nothing |
| `a` `a` `a` `a` — the stick left | nothing |

**The directions are intermittent, which is worse than absent.** Absent would
have been diagnosed on the first screen; intermittent let five screens of
navigation look like success while every menu quietly took its default — which is
why the first attempt landed on `ADVENTURE` and the hub when it had asked for
`TRACKS`.

Two things are worth separating here. The arrow keys may simply not be read by
this game: **DKR navigates its menus with the analogue stick**, and the D-pad's
silence is as likely to be the game's as the port's. The stick moving three times
and then not at all is not explicable that way.

A mechanism suggests itself and is **not** established. `dkr_window_key_down`
answers true while a key is held *or* while it is latched since the last
`dkr_window_latch_clear`, and the poll clears the latch once it has read it. A
keystroke injected by `xdotool` is pressed and released within milliseconds, so it
exists only as a latch — and if two polls happen between the keystroke and the
game's own read of the controller, the first clears what the second would have
reported. E06-S01 built the latch precisely because a 10 ms tap against a 170 ms
sampling interval is invisible otherwise, and measured four keystrokes sent and
one seen before it existed. One in four is close to what the carousel just did.

That is a hypothesis with a measurement behind it and no test yet. What it
predicts is that a *held* key would work where a tap does not, which
`Drive-Win95-VM.sh` cannot currently send.

## The capture is anchored on the mode now

`DKR_CAPTURE_MODE=<n>` makes `DKR_CAPTURE_LIST` count from the moment the game's
own state variable reaches `n` — the same anchoring the frame dump has had since
August, and whose comment already said it was what the corpus needed.

The list number is not a stable landmark. How far the game has got by its
four-hundredth display list depends on load times and on how long a cutscene
took; "twenty lists after entering the menu" is the same moment in every run.
That matters more for a capture than for a dump, because a capture is replayed
and compared, and it decides what the corpus can cover at all: **a capture ends
the run that takes it**, so each run buys exactly one scene, and naming the scene
by the game's own state is how the six the ticket asks for get chosen rather than
hunted.

Files are named for the moment — `CG0060.BIN` is sixty lists into INGAME — so two
runs anchored on different modes cannot overwrite each other's scene.
