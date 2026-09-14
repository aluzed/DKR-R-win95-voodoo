# The small font comes out shredded, wherever it is drawn

> **Fixed for the timers, on the card.** `REPLAY.EXE --both` on the machine now
> renders the reported screen as `BEST TIME 01:25:90` / `BEST LAP 00:27:36`,
> legible, at 117 divergent pixels of 307,200 against the oracle. The cause was
> the RDP's odd-row swap; see the last sections. The dialogue box is a separate
> defect and is still open.

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

### Where the true stride is declared — answered below

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

## The answer: RGBA32 loaded by `LoadBlock`, and the split this port does not model

14 September 2026. `replay --trace` — the decoder's trace hook, which had existed
since the decoder was written and which only the game had ever wired — now works
on a capture. The question that had cost four runs on the machine took one grep.

Around the digit texture:

    SetTextureImage RGBA32 at 0x32D9E0 (shift 0)
    SetTile tile=7 w0=0xF5180000 w1=0x07080200
    LoadBlock  w0=0xF3000000 w1=0x070EF000
    SetTile tile=0 w0=0xF5180800 w1=0x00080200
    SetTileSize 16x15 RGBA32 at 0x32D9E0

Beside the label on the same screen, which renders perfectly:

| | label 72x12 | digit 16x15 | digit 12x15 |
|---|---|---|---|
| tile `line` | 18 words, 144 B | 4 words, 32 B | 3 words, 24 B |
| a row at RGBA32 | 288 B | 64 B | 48 B |
| ratio | half | half | half |
| `LoadBlock` `lrs` | 863 = 72x12-1 | 239 = 16x15-1 | 179 = 12x15-1 |
| **`LoadBlock` `dxt`** | **57** | **0** | **0** |

`line` is half the naive row for **all three**, label included — that is the RDP's
32-bit split, where each texel's halves live in the two banks of texture memory,
and it is not the difference. **`dxt` is.** For the label, `2048/57` is 36 words,
288 bytes, exactly a 72-texel row: the load declares its rows. For the digits
`dxt` is zero, which says the block has no row structure at all — it is one
linear run, and the RDP fills texture memory from it under the 32-bit split.

**Our converter does not model texture memory.** It reads RDRAM straight through,
`width x bytes-per-texel` to a row, which reproduces the RDP only when the load
declared rows of that length. For these glyphs it does not.

### The measurement that settles it

Read the same bytes at **twice** the pitch and the glyphs come out clean — all
three of them, and they are the timer:

    16x15 at  64 B/row -> a speckled blob      at 128 B/row -> 0
    12x15 at  48 B/row -> a speckled blob      at  96 B/row -> 7
    12x10 at  48 B/row -> a speckled blob      at  96 B/row -> :

`00:27:15`. The factor of two is systematic across every glyph in the buffer, and
it is the same factor by which `line` differs from the naive row — which is what
makes the 32-bit split the explanation and not a coincidence.

### Stated at the strength it deserves

The mechanism is **inferred from the measurements above, not read from a
specification**: three glyphs, one clean control on the same screen, a factor of
two that appears in two independent places. Before a line is changed, the rule
should be checked against the RDP's actual 32-bit `LoadBlock` addressing, because
a conversion that starts doubling pitches on a guess would break every RGBA32
texture in the game to fix three.

**And it is a defect in this port, not in the game.** No render-to-texture targets
that buffer — the colour image is the framebuffer and nothing else, over the whole
list. The game wrote what the RDP asked for; the port reads it the wrong way.

## The pitch is the fault, and no rule from the display list can fix it

14 September 2026. `dkr_texture_convert_strided` — a tile that is a window into a
wider image — and `replay --rgba32-pitch2`, which applies a doubled pitch to
RGBA32 tiles loaded with `dxt == 0`. Both are **probes**, not a fix, and the
measurements below are why.

**The doubled pitch is right for the digits.** With the switch on, the
vehicle-select screen reads `00:27:36`: clean numerals, outlines intact, legible.
That is the proof that the pitch is the fault and the pixels are sound.

**And it is wrong for everything else.** Applied across the corpus the switch
changes three scenes of twelve, and on the race scene it destroys the banana
sprite — a clean tall sprite becomes fragments. A rule conditioned on `dxt == 0`
is far too broad: 85 of 310 loads in that scene have `dxt == 0`, most of them
benign.

**Nothing in the display list separates the two cases.** Side by side, a 44x23
RGBA32 — which I took to render correctly at linear pitch, wrongly; see the
correction at the end — against the two digits:

| | sign 44x23 | digit 16x15 | digit 12x15 |
|---|---|---|---|
| `dxt` | 0 | 0 | 0 |
| `lrs` | 1011 = w·h-1 | 239 = w·h-1 | 179 = w·h-1 |
| tile `line` | 88 B = w·2 | 32 B = w·2 | 24 B = w·2 |
| renders | correctly | shredded | shredded |

Every field agrees. The renderer treats them identically and correctly. And all
three are runtime-composed — the sign's bytes differ across captures too — so
"runtime buffer" is not the difference either.

**So the anomaly is in the contents, not in the description of them.** Something
writes those two glyph buffers at twice the row pitch their own display list
declares, and the next question is what — which is a question about the
recompiled game code, not about the renderer.

### What ships

- `dkr_texture_convert_strided`, with the ordinary conversion implemented as the
  `stride == width` case of it, so the common path is one comparison and no
  division.
- Two tests. The second one **failed first**, and usefully: one of the six source
  reads had not been rewritten, because the line I matched said
  `const unsigned short w` and the code said `const unsigned int w`. A silent
  no-op edit, caught by the test that existed to catch exactly that.
- `--rgba32-pitch2`, labelled a probe in the header, with the banana recorded
  beside it as the reason it is not a default.
- `--trace`, which put the decoder's trace on the bench and turned a four-run
  question into one grep.

Corpus with the probe off: twelve scenes, 0 divergent pixels.

## Correction, and a measurement instead of an assumption

The section above rests on "a 44x23 RGBA32 that renders correctly at linear
pitch". I never checked that. The scene looked right, so I assumed the sprite in
it did, and built an argument on it.

Read at the doubled pitch, **that sign is clean too** — a smooth blue and yellow
emblem where the linear reading is speckled. So it was never the control it was
presented as.

The replacement is a measurement over every RGBA32 texture in two captures, not
one example. Roughness is the mean absolute difference between horizontally
adjacent texels; an image laid out correctly is smooth, one read at half its pitch
is not. A texture is called for the pitch whose roughness is at least 20 % lower.

    CKEY0540 (vehicle select)          CG0060 (race)
      16x15  dxt=0    DOUBLE x6          16x16  dxt=256  linear x6
      12x15  dxt=0    DOUBLE             32x32  dxt=128  linear x4
      12x10  dxt=0    DOUBLE             24x12  dxt=171  linear x2
      44x23  dxt=0    DOUBLE x2          44x23  dxt=0    DOUBLE x2
      16x16  dxt=256  linear             28x14  dxt=0    linear
      11 of 27 favour doubling           2 of 28 favour doubling

**`dxt == 0` is necessary and not sufficient.** Every texture that wants the
doubled pitch has `dxt == 0`; but so do `12x23` and `28x14` in the race scene,
and those want the linear one. A rule on `dxt` alone is what broke the banana.

And the same 44x23 sign wants the doubled pitch in **both** captures, so whatever
decides it is a property of the texture and not of the frame.

### Where this leaves it

The display list describes every one of these the same way — `lrs = w·h-1`,
`line = w·2`, tile at the image origin — and the memory does not match that
description for a particular set of them. Nothing readable from the list
separates the set.

So the next instrument is not another rule. It is **watching the writes**: what
fills those buffers, and why it lays some out at twice the pitch its own display
list declares. That is a question about the recompiled game code, and the
policy file already shows this port patching `load_texture`'s heap arithmetic in
six places — which is the first place to look and was not, on inspection, an
obvious cause: those patches move where a frame's data begins, not how far apart
its rows are.

## It is a wider strip, and nothing writes it wrong

> The section below first called this "the digit font sheet". It is not one sheet:
> see the correction under it. What it got right, and what matters, is that the
> data is **wider than the tile** and nothing is written wrongly.

Read the same memory as a **32-texel-wide image over 120 rows** and the whole
numeral font appears, stacked: `0`, `2`, `4`, `5`, `6`, `7`, `8`, `9`, clean and
correctly proportioned, with more glyphs in the right-hand column.

So `0x32D9E0` is not one glyph. It is **the digit font sheet**, 32 texels wide.
The game points `G_SETTEXTUREIMAGE` at each numeral's position inside it and
draws a 16-texel-wide tile — a window. Our conversion reads sixteen texels to a
row from that address, which slices across the sheet and interleaves each glyph
with whatever is beside it.

**That corrects the previous section, and it corrects it the whole way.** I had
concluded that "something writes those two glyph buffers at twice the row pitch
its own display list declares", and that the next question was about the
recompiled game code. Nothing writes anything wrong. The sheet is exactly as it
should be, and it is this port that reads a window as if it were a whole image.

It also explains what the doubled-pitch probe was really doing: not undoing a
writer's error, but supplying the sheet's stride by accident — 16 x 2 happening to
equal 32 for these glyphs. And it explains why the same probe destroys a sprite
that is genuinely its own image: there the stride was right already.

### What is left

One question, and it is now a narrow one: **where does the sheet's width come
from?** Every field in the display list has been decoded and printed —
`G_SETTEXTUREIMAGE`'s width is 1, the tile's `line` is `w x 2` for every texture
in the game, `LoadBlock`'s `dxt` is 0 here, `uls`/`ult` are 0. None of them says
32.

The answer is likely to be in how `LoadBlock` fills texture memory for a 32-bit
texture — the split across the two banks that already explains why `line` is half
the naive row. `dkr_texture_convert_strided` is the shape of the fix and is
already written and tested; what it still needs is the number.

### Correction: not one sheet, a strip per allocation

The glyph addresses are `0x32D9E0`, `0x32DE70`, `0x32E210`, `0x32E6A0`,
`0x32EFC0`… — 1168, 928, 2336, 1328 bytes apart, irregular and not multiples of
128. They are **separate allocations**, not offsets into a single sheet. A 16x15
RGBA32 tile is 960 bytes and the commonest gap is 1168, which leaves about 208
bytes of command list per texture — exactly what `load_texture` builds.

So the 120-row render that showed `0 2 4 5 6 7 8 9` stacked was showing
*successive allocations*, each read 32 wide, not one sheet. The shape of the
conclusion survives; its extent does not.

### What one allocation actually contains

The 960 bytes at `0x32D9E0`, read **32 texels wide and 7 rows tall** — entirely
inside its own allocation, nothing borrowed from the next — show `0`, a colon, and
the start of another glyph. It is a **strip of rendered text**, seven texels tall.

That fits what is on screen: the timer glyphs measure about eighteen screen pixels
tall at the 2x magnification already established, which is nine texels, not
fifteen.

And it explains the interleave exactly. The tile is declared 16 wide, so the
conversion takes texels 0..15 as row 0 — the strip's **left half** — then 16..31
as row 1 — the strip's **right half** — and so on. Consecutive rows of the tile
alternate between the two halves of the strip. That is the speckle, precisely.

### The number is still not in the display list

Every field has now been decoded and printed for six textures side by side, the
working ones and the broken ones:

- `G_SETTEXTUREIMAGE` width: 1, for all of them
- tile `line`: `w x 2` bytes, **for every texture in the game**, so it carries no
  information about which are strips
- `LoadBlock` `lrs`: `w·h - 1`, for all of them
- `LoadBlock` `dxt`: 0 for the strips — and also 0 for textures that are not
  strips, so necessary and not sufficient
- `uls`/`ult`: 0 everywhere
- `cms`/`masks`: the one texture that differs is a sprite that reads correctly

Nothing says 32. `dkr_texture_convert_strided` is written and tested and takes
exactly that number as its argument; what is missing is where to get it.

## The geometry does not carry it either

The trace now prints, beside each triangle batch, the texture extent its corners
ask for, in texels:

    digit   Triangle 2  s=0..15 t=0..14 texels (tile 16x16)
    banana  Triangle 2  s=0..11 t=0..22 texels (tile 16x32)

Both quads ask for **exactly their declared tile**, edge to edge. So the geometry
agrees with the display list in the broken case as well as the working one, and it
is not the missing source either.

That closes the list. Every field that describes these textures has now been
decoded and compared between a texture that reads correctly and one that does
not — image width, tile `line`, tile `siz`, `uls`/`ult`, `cms`/`masks`,
`LoadBlock`'s `lrs` and `dxt`, and now the quad's own s and t. **None of them
distinguishes the two.**

### Why the doubled reading is nonetheless the true one

Because of what it produces. With the pitch doubled the vehicle-select screen
reads `00:27:36`: six well-formed numerals and two colons, on two lines, with
outlines intact. An interleaving read at the wrong pitch does not produce
well-formed digits by accident, and certainly not eight of them in a row.

So the data is what it is, and the number that recovers it is `2 x tile width` for
these textures — established by what comes out, not by a field that declares it.

### Where it now stands

- **The defect is understood**: a tile read at its own width where the image is
  wider, so consecutive rows alternate between halves of the source.
- **The fix's shape exists and is tested**: `dkr_texture_convert_strided`.
- **The fix's input does not**: nothing in the display list says how wide the
  image is, and a rule derived from any field measured so far is either wrong for
  the digits or wrong for the sprites.

What would settle it is a reference for what the RDP does with a 32-bit
`LoadBlock` — the fill order into the two banks of texture memory. That is a
question about hardware, answerable from a specification or from another
implementation, and not from this capture. It is the one thing this investigation
has needed and not had.

## Found: the RDP swaps every other row, and `dxt == 0` means nobody swapped it back

14 September 2026. The missing reference was a hardware one, and it is one line of
angrylion's `fetch_texel`:

    taddr = (tbase << 2) + s;
    taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

**The RDP exchanges the two halves of a 64-bit word when it fetches from an odd
row.** `LoadBlock` normally applies the matching exchange as it fills texture
memory, and the two cancel: the texels lie in memory exactly as they are sampled,
and reading straight through is right. The load only does it when `dxt` is
non-zero — `dxt` is the row-advance that tells the load where the rows are. **With
`dxt == 0` the load has no rows, nothing is swapped going in, and the fetch still
swaps coming out.**

So a texture loaded with `dxt == 0` has every odd row's texels exchanged in pairs.
That is the interleave, and it is not a pitch at all.

The exchange, in texels, is `4 / bytes-per-texel-in-a-bank`: **2** for 16- and
32-bit texels, 4 for 8-bit, 8 for 4-bit. Applied at `s ^ 2` on odd rows, the
16x15 tile at `0x32D9E0` becomes a clean, complete `0` at its **own declared
size** — no doubled pitch, no spilling into the next allocation.

On the vehicle-select screen: `00:27:36`, outlines intact, with `BEST LAP` and
`DkR` beside it untouched.

### Measured, not hoped

Roughness of every converted texture, with the swap against without, over three
scenes:

| scene | textures | smoother | rougher | unchanged |
|---|---|---|---|---|
| CKEY0540 | 66 | **12** | 0 | 54 |
| CG0060 | 151 | **13** | 1 | 137 |
| CAP0250 | 80 | **1** | 0 | 79 |

Twenty-six textures improved, one worsened, and the rest untouched — from a rule
that comes from the hardware and not from the data it fixes.

### What this retires

- `--rgba32-pitch2` is **gone**. It was a probe that happened to undo the swap for
  glyphs whose width made `16 x 2` land on the right texels, and it damaged every
  sprite it touched. Leaving a misleading switch in the tree to commemorate a
  wrong turn is how the next person gets misled; the wrong turn is recorded here
  instead.
- The long hunt for a field that declares the image width is over: there was
  never a wider image. The tile always described its texture correctly. What it
  could not describe was that the texels had been left in the order the load put
  them, and the fetch expects a different one.

`dkr_texture_convert_swapped` carries the rule, `--no-odd-row-swap` turns it off
to measure what it is worth, and two tests pin it — one of which checks that the
swap is applied to odd rows **and not to even ones**, because a swap applied to
every row passes a test that only looks at one.

## The dialogue text is a different defect, and a second rule only half-answers it

The odd-row swap fixes the timers. It leaves the dialogue box exactly as it was —
because that text comes from a font atlas loaded with `dxt = 0x43`, not zero, so
the load did swap and the fetch swaps back. Correctly untouched.

Its own disagreement is elsewhere. `SetTileSize` says the atlas is **248** texels
wide; the tile's `line` says 248 *bytes*, which at `G_IM_SIZ_16b_LINE_BYTES = 2`
is **124 texels**. Read at 124 the atlas is one clean alphabet,
`@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^` with a lowercase row under it. Read at 248 it is
two degraded copies — the "two copies, one thinner" this document spent an
afternoon on, finally explained.

So a second rule suggests itself: **take the source row length from `line`, not
from the tile width, where they disagree.** `line` is the authoritative row length
in texture memory; the tile width is the sampled width.

**Scored the same way as the swap, it does not earn the default path:**

| scene | textures | smoother | rougher | unchanged |
|---|---|---|---|---|
| CKEY1622 | 98 | 3 | **2** | 93 |
| CAP0250 | 109 | 5 | 0 | 104 |
| CG0060 | 166 | 9 | 0 | 157 |
| CKEY1150 | 98 | 4 | **1** | 93 |

21 smoother against **3 rougher**, where the swap was 26 against 1. And the
dialogue text it was written for becomes markedly more solid without becoming
readable — better, not right.

It ships **off**, behind `--row-from-line`. A rule that improves most things and
worsens three for reasons nobody has looked into does not belong in the default
path, and the three are the next thing to look at.

## Confirmed on the Voodoo

14 September 2026. Rather than navigate five menus again on an unstable machine,
`REPLAY.EXE --both` was run on the capture of the screen itself — the same
display list, through Glide, on the emulated Voodoo 2:

    painted surface: oracle 305068, card 305121 (0% gap)
    frankly different: 117 of 307200 (380 per million)
    worst off-edge: 239 at (166,118)  oracle 0xFFFFFF  card 0x101010

And the screen reads:

    BEST TIME  01:25:90  DkR
    BEST LAP   00:27:36  DkR

`01:25:90` is the value in the original report, to the digit. The defect that
started this document is closed on the hardware path it was reported from, and
the 117 pixels that remain are on the `ANCIENT LAKE` title, not on the timers.

**Method worth keeping.** Four attempts to reach that screen by driving and by
menu failed or died; the replay needed no navigation at all, because the capture
already *is* the screen. A harness that can re-run one frame on the card is worth
more than the ability to reach it again.
