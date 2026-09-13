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

## It is not the alpha cutout — refuted, in one run

The speckle is regular, which is what a **dithered alpha** looks like when a hard
threshold resolves it instead of coverage. `rdp_state.c` already names that
approximation:

> `CVG_X_ALPHA` multiplies the coverage by the alpha … The threshold is 1 rather
> than 128 because that is what the mechanism says … Where the alpha has more bits
> the N64 dithers a partial coverage and a hard threshold cannot; that is an
> approximation.

A best-time readout drawn semi-transparent would be exactly that case, so the
hypothesis had a shape — and `DKR_NO_ALPHA_TEST=1` was written to switch it off
and settle it rather than argue it.

**The switch works and the digits do not change.** With the cutout disabled the
character-select screen shows it plainly: every foliage sprite and flower gains
the black rectangle its transparent border had been cut from, which is precisely
what "draw every texel whatever its alpha" looks like. On the vehicle screen, in
the same run, the digits are shredded exactly as before, speckle for speckle.

So the cutout is not the mechanism. One hypothesis, one switch, one run, and it
is out.

**What that leaves.** The dropped pixels are not being *killed*, so they are being
*drawn* wrong: the texture the numerals come from is arriving corrupt, or being
sampled wrong. A regular checkerboard in a texture is the signature of a format
read at the wrong width — a 4-bit atlas taken for 8-bit, a colour-indexed one
taken for direct — and the labels beside the digits, which come from a different
texture and are clean, fit that.

Not measured. The next thing is a capture of that screen, not another guess.

## Capturing the screen: written, not proven

`DKR_CAPTURE_KEY=1` makes `F9` capture the display list being drawn when it is
pressed. Anchoring on `gGameMode` cannot reach this screen — the vehicle select
and the race are both preceded by MENU — and a screen one can see but cannot
freeze is a defect one cannot attribute.

`F9` because nothing else uses it: the controller mapping takes space, shift, Z,
return, the arrows, Q/E, IJKL and WASD, and a capture key that also steers would
fire while the player was driving.

**It has produced no capture, and the run it was in stopped early — three times.**
That was written up here as "the capture key destabilises the run, three for three",
and **that conclusion was wrong.** A fourth run, with `DKR_CAPTURE_LIST=3000` and
no key feature at all — a capture that never fired — stopped at display list 300
in exactly the same way.

So the stopping is not the key's. What it is remains unknown, and it leaves no
trace: the last report before a stop shows 893 KiB of texture memory in use, no
refusals, no failures, nothing at its limit, and the display-list and VI-present
counters advance together at their usual ratio right up to it and then both cease.
It has now happened with the key feature and without it, with a capture armed and
with none pending.

Two runs have gone far — one to list 2,520 with no keystroke sent at all, one to
1,717 through a full navigation into a race, ending at its own capture by design.

**And then the premise itself turned out to be false.** See below: the runs were
not stopping. Everything between here and that section is the record of three
hypotheses built on a reading that was wrong, and it is kept because the shape of
the mistake is the useful part.

**A fifth run then refuted the next hypothesis too.** If the long one differed by
having been sent no keystrokes, a run left equally untouched should go equally
far. One was: armed at list 1,500, no key sent, nothing driven. It stopped at 300.

So three explanations have now been tried and excluded — the capture key, the
keystrokes, and an armed capture pending — and the honest reading is the one the
repository has recorded elsewhere about this game's bring-up: **the stop is
intermittent.** Two runs in a row have differed in nothing under my control and
gone 2,520 and 300. Each correlation I found was pattern-matching on a sample of
three or four against noise, and each was published before the fourth run that
broke it.

That is worth its own line, because it is the third time in a week: a small number
of runs on an unstable subject will always offer a variable that separates them,
and it will usually be the one most recently changed.

The trigger was rewritten once on the strength of the wrong conclusion:
`dkr_window_take_capture_request` is now a single byte set in the window procedure
where the message arrives and taken by whoever asks first, the render thread never
touching the key arrays or the latch. That is a better design than polling the key
state from another thread and it is kept on those grounds, not as a fix for
anything — it changed nothing, which at the time looked like evidence and was not.

The screen still cannot be frozen, and until it can, the shredded digits cannot be
attributed to the decoder or to the card.

## The runs were not stopping. They were not finished.

Measured on 13 September 2026. An untouched run, nothing armed, no key sent,
photographed every thirty seconds for six minutes and then stopped by the script:

    last list:    list=1080
    last present: present=8700

Twelve screenshots, twelve different pictures — mean brightness 4225, 23066,
21361, 7888, 16959, 11388, 22866, 17017, 16488, 21459, 17473, 15235. The attract
sequence animated for the whole six minutes and was still animating when the
machine was stopped, by me, on a timer.

So the game reaches **list 1080** and is still going. Three lists a second. A
capture armed at list 1500 needs about eight minutes of wall clock to fire.

**Every "the run stopped at list 300" was a run that had not got there yet.** The
log was read after a fixed wait, the wait was shorter than the run, and the last
line in a log that is still being written looks exactly like the last line of a
log that has stopped. Four runs, four readings, one instrument reporting on my
patience rather than on the program.

That is the fourth correction in this thread, and the previous three are all
downstream of this one: the capture key, the keystrokes, and the "intermittent"
reading each explained a stop that never happened.

### What the method has to be

A run is read when the log shows it has passed the list in question — not after a
wall-clock wait chosen by guess. The wait is not evidence and must never again be
allowed to stand in for one:

    until <log shows list >= N>; do sleep; done

The one durable fact from all of it is the rate, and it is worth having: **three
display lists a second**, eight and a half presents to the list.
