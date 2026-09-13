# The timers come out shredded, and only on one screen

Reported by the user on 13 September 2026 from the driven play session's
screenshots, and confirmed here.

## What it looks like

On the **vehicle-select** and **ghost-prompt** screens of Ancient Lake:

- `BEST TIME`, `BEST LAP` and `DkR` — the *labels* — render cleanly: magenta
  glyphs with their outline, exactly as intended;
- the **digits** beside them — `01:25:90`, `00:27:15` — are shredded. The glyph
  shapes are barely legible under a regular speckle of dropped pixels, green over
  the first line and orange over the second, looking like a coarse checkerboard
  eaten out of the numerals.

On the **in-race HUD**, the same font renders perfectly: `8TH`, `LAP 1/3`, the
banana count, `TIME 00:00:00`, all clean.

## What is established

**The HUD is not the problem, and the card is not the problem there.** The race
capture `CG0060.BIN` was replayed through both backends: 755 divergent pixels out
of 307,200 over the whole frame, and the HUD strip is identical between the
oracle and the card, digit for digit.

So whatever shreds the timers is specific to those menu screens, and this
document cannot yet say whether it is the decoder or the backend — **there is no
capture of that screen**. Every image of it is the card's own output with nothing
to compare against, which is the exact condition E09-S02 exists to remove.

## What it might be, and none of it is measured

The speckle is regular, which is what a **dithered alpha** looks like when it is
resolved by a hard threshold instead of by coverage. `rdp_state.c` already names
that approximation:

> `CVG_X_ALPHA` multiplies the coverage by the alpha … The threshold is 1 rather
> than 128 because that is what the mechanism says … Where the alpha has more bits
> the N64 dithers a partial coverage and a hard threshold cannot; that is an
> approximation.

A best-time readout is plausibly drawn semi-transparent, which would make it
exactly the case that comment describes. That is a hypothesis with a shape, not a
finding.

## Capturing the screen: written, not proven

`DKR_CAPTURE_KEY=1` makes `F9` capture the display list being drawn when it is
pressed. Anchoring on `gGameMode` cannot reach this screen — the vehicle select
and the race are both preceded by MENU — and a screen one can see but cannot
freeze is a defect one cannot attribute.

`F9` because nothing else uses it: the controller mapping takes space, shift, Z,
return, the arrows, Q/E, IJKL and WASD, and a capture key that also steers would
fire while the player was driving.

**It has not yet produced a capture, and the run that carried it stopped at
display list 300** — presents last reported at 2,700 — where a plain run on
10 September reached list 2,520 and 19,800 presents. The only difference is that
this build polls the keyboard from the render thread on every list, and only when
the feature is enabled. That makes the poll the first suspect and nothing more:
one run, one difference, no isolation. The obvious control — the same build with
`DKR_CAPTURE_KEY` unset — has not been run.
