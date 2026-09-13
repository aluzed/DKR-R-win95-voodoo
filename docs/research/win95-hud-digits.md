# The small font comes out shredded, wherever it is drawn

> **The reported screen is captured.** `CKEY0540.BIN`, Ancient Lake's vehicle
> select — the exact screen this began with — is in the corpus, and the oracle
> reproduces its shredded digits. See the last section.

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

## It is not "one screen": it is the small font

Found on 13 September 2026, by driving into Timber's Island and running into Taj,
whose greeting opens a dialogue box. **The two lines of text in that box are
shredded in exactly the way the best-time digits are** — glyph shapes legible
only in outline, eaten through by a regular speckle.

That box is nowhere near the vehicle-select screen, and it is not a timer. What it
shares with the digits is the **font**: the small proportional face the game uses
for running text and for numerals. Everything drawn in the large outlined display
face — `BEST TIME`, `BEST LAP`, `GAME SELECT`, `PLAYER SELECT`, `ENTER YOUR
INITIALS`, `COPY`, `ERASE` — is clean, on every screen seen so far.

So the question is no longer "what is wrong with that screen". It is "what is
wrong with that font", and the hub is a far cheaper place to ask it than a screen
five menus deep: the dialogue is about twenty seconds of driving from the start of
a run.

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

## Caught on a capture, and the card is out of it

13 September 2026. `CKEY1622.BIN` and `CKEY0951.BIN` — Taj's dialogue in Timber's
Island, taken with F9 — replayed through the **software oracle** on the
development machine. The oracle has no Glide, no TMU, no ARGB1555 upload and no
Voodoo of any kind.

**It renders the text shredded, speckle for speckle, exactly as the card does.**

So the defect is not the backend, not the texture upload, and not the emulated
Voodoo's one alpha bit — the hypothesis this document was about to spend a run on.
It is in the decoder or in the texture conversion the two backends share, and it
is now reproducible on the host, offline, on demand, in a debugger.

Both captures are in the corpus and compare at 0 divergent pixels oracle-to-oracle.

### What the texture says

The text comes from an **IA16** font atlas, which the key names as tile **248×11**
and which arrives as a **256×32** texture: 248 rounded up to a power of two, 11
rounded to 16, then padded to 32 to stay inside the card's 8:1 aspect limit. The
dump shows the alphabet `@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^` repeated three times
down the image and one-and-a-bit times across, which is the documented padding
doing exactly what it says it does — repeating the pattern rather than filling
with zero. That part is not the defect and it took a while to stop reading it as
one.

What is left, and not yet attributed: within each eleven-row band, the glyphs are
mottled through.

### A test that proved nothing, recorded as such

I reflowed the dumped atlas at 248, 252, 254 and 256 columns to see which width
straightened the alphabet. 256 did. That was **circular** — the dump is already
laid out at 256, so the test could not have returned anything else. It is written
down because the reflow images look like evidence and are not.

The instrument that would settle it is a dump taken **before** the padding, at the
tile's true 248×11, which does not exist yet.

## The stride was a good hypothesis and it is wrong

`G_SETTILE` carries `line`, the number of 64-bit words between two rows of a tile,
and **nothing in this decoder read it**: the handler took `w1` for the wrap modes
and let `w0` go by. Meanwhile `dkr_texture_convert` walks RDRAM linearly, so it
places row *y* at `width x bytes-per-texel x y`. Two numbers that have to agree,
one of them never looked at, and a defect that looks exactly like rows read at the
wrong offset. That is as good a shape as a hypothesis gets here.

So `line` and the tile's own `siz` are decoded now, and the two strides are
compared at every conversion. On the scene that contains the shredded text:

    stride: 16 of 132 textures disagree with their tile line (12032 texels)
      line=64 bytes, a row of 32 texels needs 128 (32x32)
      line=32 bytes, a row of 16 texels needs 64 (16x16)
      ... fourteen more, every one of them 32x32 or 16x16

**The font atlas is not among them.** It is 248x11, and its stride agrees. The
hypothesis is out — measured, not argued, and out on the first run.

Two further things the measurement says about itself. Every disagreement is a
factor of exactly *two*, in every scene of the corpus; and sixteen genuinely
sheared textures in one frame would be visible, which they are not. So the
sixteen are far more likely the check comparing a conversion against a
`G_SETTILE` from another tile or another moment than sixteen real defects. The
counter ships labelled as unvalidated, in those words, in the header.

What survives is worth keeping anyway: the decoder now reads `line` and the
tile's `siz`, which it did not, and the day a stride does matter the number is
already there.

### Where that leaves the font

The atlas arrives at its declared width, from the declared address, at the
declared stride. The shear is therefore not in *getting* the texels. It is in the
texels themselves, in how they are sampled, or in the geometry that carries them —
and the next instrument is the one this document asked for last time and still
does not have: a dump taken before the aspect padding, at the tile's true 248x11,
so that what is read can be compared with what the game stored.

## The atlas at its true size, and a number instead of an eye

`replay --dump-textures` now writes each texture at the **tile's** dimensions
rather than the padded ones, cropping with the width and height the key already
carries. The name records both: `tex096_248x11_of_256x32_...`.

That was worth doing for one reason: the padded dump of this atlas had been read
as corrupt three separate times, and each time the repetition was the padding
doing what its own comment says it does.

At 248x11 the atlas holds **two copies of the same alphabet, side by side**, 124
columns each, plus a row of lowercase under each. So the question became whether
the second copy is an outline layer the game stores on purpose, or a damaged copy
— and after three misreadings of this image, not one I was going to settle by
looking at it:

    left  mean 56.8  ink 458/1364
    right mean 68.3  ink 504/1364
    agreement on ink/no-ink, left against right shifted by:
      -2: 66%   -1: 60%   0: 77%   +1: 60%   +2: 69%

**Aligned, not offset.** Agreement peaks at shift zero and falls away either side,
which is what two copies at the same position look like and not what an outline
looks like — an outline would peak off-centre. The left copy carries 9 % less ink
and 17 % less mean intensity than the right, at the same alignment.

So: two aligned copies of one alphabet, one thinner than the other. That is
equally consistent with a font stored in two weights and with one copy arriving
damaged, and nothing here separates them. What would: the atlas as the ROM
stores it.

### The state of it

- not the alpha cutout (one switch, one run)
- not the card, not Glide, not the upload — the oracle shreds it identically
- not the tile stride — measured, the atlas agrees with its `line`
- the atlas is read at its declared width, address and stride

What is left is the texels themselves, how they are sampled, or the geometry
carrying them. The next measurement is the ROM's own copy of this atlas against
the one in RDRAM, which decides in one comparison whether anything is damaged at
all before another afternoon goes into how.

## Five measurements, and the renderer is innocent of all of them

13 September 2026, all on the host, no machine involved.

**The atlas is the same bytes in four captures.** Key `6039F00B` appears in
`CAP0150`, `CAP0160`, `CKEY0951` and `CKEY1622` — two sessions nine days apart,
two different scenes — and the dumps are **md5-identical**. Nothing corrupts it
between the game writing it and the port reading it. Whatever is in there is what
the game put there.

**The tile is at the image's corner.** `uls`/`ult` have been decoded and recorded
since they were first read, with a comment saying that whether the offset matters
should be measured before anything acts on it. Measured: `tile origin: 0` on every
scene of the corpus. The decoder converting from the image origin costs nothing,
because the tile *is* at the origin.

**The magnification is exactly two, and clean.** In the rendered frame the text
rows come in identical pairs, and on a text row every run of ink is an even number
of columns with all 150 even boundaries agreeing. That is 2x2 point magnification
aligned to the grid — no half-texel drift, no fractional scale.

**And it samples at step one, not step two.** Matching the rendered row against
the atlas, allowing any row and any offset:

    step 1: 165/200 (82%)   step 2: 84/200 (42%)

A shredded look is what minification does to small glyphs, so the question was
whether the port was dropping every other texel. It is not. It reads the atlas
one texel at a time and doubles it.

**The format is the declared one.** The same bytes were re-read from the capture
as I8 at two strides and as I4, to see whether any of them produced a cleaner
alphabet than IA16 does. None did: the 8-bit readings give glyphs at twice their
proper width, the 4-bit one likewise. IA16 at 248 is the reading that gives
correctly proportioned letters.

### What that leaves, stated narrowly

Every step from RDRAM to the screen has now been measured and each is faithful.
The glyph data being sampled is itself thin and broken up, and it is stable data
the game wrote. The atlas holds **two aligned copies** of the alphabet, and the
one the coordinates address is the thinner one.

So the remaining question is not about texels at all. It is: **which of the two
copies should be sampled, and does something put s 124 texels away from where the
game meant it?** That is a question about coordinates, and it is the first
formulation of this defect that the instruments here can attack directly.

## A third scene, and a second UI element

`CKEY1150.BIN` — Taj's vehicle prompt in the hub: two boxes with a strip of label
text under them. On the machine the labels are a row of dots and fragments. The
oracle reproduces it identically.

It matters because it is a **different UI element** from the dialogue box, drawn
at a different place on screen, and it comes apart the same way. Three scenes in
the corpus now carry this defect, and it follows the small font wherever the font
goes.

Reached with `Drive-Win95-VM.sh pad-hold`, which holds several of the game's
controls at once. Four earlier attempts to drive the twenty seconds to this
landmark failed because `hold` takes one host key and `pad` only taps: steering
and accelerating had to alternate, and the car went where the alternation sent it.
A sustained hold drives straight to the arch first time.

## The screen the report named, captured

13 September 2026. `CKEY0540.BIN`: **Ancient Lake's vehicle-select screen**, the
one the defect was first reported from. In it:

- `ANCIENT LAKE`, `BEST TIME`, `BEST LAP`, `VEHICLE`, `CAR`, `HOVER`, `PLANE`,
  and the initials `DkR` — all clean;
- the digits `01:25:90` and `00:27:15` — shredded, eaten through by a speckle
  that takes the colour of what is behind them: green over the first line, orange
  over the second, exactly as reported.

The oracle reproduces it, so the whole screen is now available on the host.
Twelve corpus scenes, 0 divergent pixels, four of them carrying this defect.

**It was not reached by driving.** Four attempts to drive there wandered off,
wedged against cliffs, or died. The route that works has no driving in it at all:
`GAME SELECT -> TRACKS -> DINO DOMAIN / ANCIENT LAKE`, and the vehicle screen is
the next thing the game shows. Two keystrokes from a menu the attract sequence
leaves you one button from.

That is worth writing down as a method and not just as a route: the hub is a
physics simulation driven blind through a virtual X display, and the menu is a
list. Where both reach the same screen, the list is the instrument.

### And a run that keeps dying with no explanation

Twice today a run stopped dead — lists and presents both, at 300 — shortly after
`pad start` was sent several times while the attract demo was playing, leaving the
game's window black on the Windows desktop, unrecoverable. Untouched runs reach
1080 and keep going. The sequence that works sends its first `start` about eighty
seconds in and spaces the presses six seconds apart; the two that died sent theirs
later, into the demo race.

**Not attributed.** I blamed `grab`'s mouse click first, with a plausible
mechanism — a click into the guest taking focus from a full-screen Glide context —
and committed the fix; the next run died the same way with no `grab` in it at all.
The `grab` change stands on its own merits and is not the cause of this.

## Why. The rows are read half a row apart

14 September 2026, on `CKEY0540.BIN`, entirely on the host.

**The digits are not a static asset.** The labels beside them (`BEST TIME`, at
`0x2430D0`) and the font atlas (`0x1F4960`) are **md5-identical in all four
captures**, taken nine days and two sessions apart. The digit glyphs
(`0x32D9E0`, `0x32DE70`, `0x330730`) are **different in every capture and all
zeros in the two that have no timer on screen**. They live in a buffer the game
fills at run time with the timer text.

**And the buffer is read with the wrong row stride.** The tile is 16 texels wide
at RGBA32, so `dkr_texture_convert` walks the texels linearly, 64 bytes to a row.
Read the same bytes **128 bytes to a row** and the blob becomes a clean, smooth
`0`, counter and outline intact:

    64 bytes a row -> a speckled orange blob
    128 bytes a row -> 0

So the image in that buffer is twice as wide as the tile taken from it, and every
row after the first is read sixteen texels to the left of where it belongs —
interleaving each glyph with its neighbour. That is the shredding, and it is one
line of arithmetic.

### What it is not

- **Not the alpha threshold.** `pack_argb1555` keeps one alpha bit and these
  glyphs do carry a soft outline, so it was the obvious suspect. Rendering the
  same texels with their full eight-bit alpha changes 58 of 240 texels and leaves
  the glyph exactly as broken. Measured, and out.
- **Not the card, not Glide, not the upload, not the alpha test, not the tile
  origin.** All measured earlier in this document.

### The one thing still missing: where the true stride is declared

Three places could carry it, and all three have now been decoded and measured,
and none of them says 128 for this tile:

| source | what it says |
|---|---|
| `G_SETTEXTUREIMAGE` width | **1** — the `LoadBlock` idiom sets it to one |
| `G_SETTILE`'s `line` | 4 words, 32 bytes |
| `G_LOADBLOCK`'s `dxt` | agrees with 64 to within the formula's rounding |

The counters for all three ship with this commit and print beside the replay's
counts, so the next attempt starts from measurements rather than from guesses.
The likeliest remaining answer is that the render tile's `line` is being read from
the wrong `G_SETTILE` — the decoder keeps one render tile and the conversion is
not ordered with respect to it, which is the same doubt already written against
the stride counter.
