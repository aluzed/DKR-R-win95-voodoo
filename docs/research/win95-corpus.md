# The corpus of captures

E09-S02 asks for coverage: title, menus, a lap of each level, cutscenes, split
screen, results. This records what exists, how it was obtained, and what it has
already shown.

## What is in it, 4 September 2026

| capture | scene | commands | triangles | emitted | textures |
|---|---|---|---|---|---|
| `CAP0050.BIN` | the Nintendo 64 logo, intro | 640 | 419 | 234 | 35 |
| `CAP0150.BIN` | the copyright screen, logo face-on | 459 | 293 | 99 | 18 |
| `CAP0160.BIN` | the same, ten lists later, logo turned | 459 | 293 | 181 | 18 |
| `CAP0250.BIN` | the hub, Pipsy on the beach | 3592 | 1293 | 904 | 190 |
| `CAP0400.BIN` | Ancient Lake, Bumper racing | 1539 | 943 | 510 | 95 |

Five captures of four scenes that share almost nothing: one large model on a sky, a mostly
two-dimensional screen with text, an outdoor hub with 190 textures, and a race
with five-pass text. That is coverage of a kind — of the *decoder's* paths, not of
the game — and it is what the harness needed to stop being a one-scene instrument.

**It is four scenes, and the ticket asks for a lap of each level.** Recorded as
incomplete rather than presented as a corpus.

## How they were obtained, and what that cost

`DKR_CAPTURE_LIST=50,150,400` and the like, several to a run. The comma is what makes a corpus
practical: a capture costs a boot, a launch and a wait, so one per run is a day's
work for a dozen scenes.

Two obstacles were met on the way and both are recorded elsewhere:

- **Five captures of six were lost to the disk cache.** `fclose` hands the bytes
  to Windows 95 and Windows 95 keeps them; stopping the emulator took them. The
  writer commits before closing now. See the commit of 4 September 2026.
- **A run does not reach an arbitrary list, and that is what bounds the corpus.**
  Measured on 4 September 2026 over four runs: the game reaches display list
  **300** and stops there, in `gGameMode=1 (MENU)`, with no error and with its
  window still answering the message loop. A single capture armed at list 800 was
  never written. Two armed at 200 and 250 were both written, so it is not the
  captures that shorten the run — the run is short.

  The log is durable (`dkr_diag_commit` calls `FlushFileBuffers` at every report),
  so this is read from the last report rather than inferred: `list=300 cmd=291998
  tri=152849 emitted=92252 rejects=0`, and nothing after it.

  That is well short of the 1500 lists E02-S06 recorded, and it is why the corpus
  has no lap of any level: the game does not get there. **Recorded as a limit on
  E09-S02's coverage and as a question for E02-S06, not diagnosed here.**

## What the corpus has already shown

**The copyright screen looked broken and is not.** See below: it took two
captures ten display lists apart to establish that, and the first one on its own
said the opposite.

**And the hub draws large grey rectangles over the scene.** Several flat
light-grey quads and one black one sit across Pipsy and the water in
`CAP0250.BIN`, again in the **oracle**. A flat untextured quad where a textured
one belongs is the signature of a texture that never arrived or a combiner that
fell through; 190 textures is twice any other scene in the corpus, so a cache or
an allocator limit is the first thing to ask about.

Neither is explained. `conversions: distinct-keys=64 overflow=48` in the same
report is **not** the cause, tempting though it looks: that counter says the
64-slot set used to *count* distinct tiles ran out of room, which is a limit on
the measurement and not on the rendering. Checked rather than assumed, because a
plausible number next to a defect is how a wrong diagnosis starts.

Both are recorded and not chased. That is exactly what a corpus is for — defects
that no amount of looking at the race would have found — and each is now a frozen
input that can be pointed at whenever E04 or E05 next has a hypothesis.

## Checking it

    tools/render/check-corpus.sh <corpus-dir>

Counts first, then images, with a per-scene threshold. See
`docs/VISUAL-TESTING.md`.

The five captures replay to their references at **0 divergent pixels of 307,200**,
which is what one expects of a deterministic replay and which is checked rather
than assumed — it is the property every other measurement in this harness rests
on.

## Where it lives

Outside the repository. A capture is eight mebibytes and has to be: the decoder
reads at addresses the display list itself computes, so there is no knowing in
advance which bytes matter. Five captures are forty megabytes; a lap of each
level would be hundreds. What is versioned is the script, and the counts, which
are text.

## The copyright screen, measured — 4 September 2026

Two instruments were added to answer "why is the Rare logo a blank plate", and
both belong to the oracle for the same reason the probe does: it rasterises, so
it knows.

**How many pixels each texture actually painted.** "Uploaded" and "reached the
screen" are different facts, and only the second says whether an object is in the
image.

    textures: 15 written, 11 of them painted nothing
      tex001  32x32   19091 px    a yellow gradient — the plate
      tex003  64x32       0 px    RAREWARE
      tex004  32x32       0 px    TM
      tex006  64x32  288109 px    one sky tile, over the whole background
      tex014  256x32   1236 px    the font atlas
      tex015  256x32   4160 px    the font atlas
      (nine more sky tiles, 0 px each)

So the logo's texture is decoded, uploaded, correct — `RAREWARE` is legible in
the dump — and **nothing samples it**. The plate is a 32×32 yellow gradient drawn
flat: measured over the plate's area, four colours in 9,600 pixels, so the quad
samples essentially one texel of a texture that has a gradient.

**Where every triangle went.** The replay's count line accounted for `tri` and
`emitted` and nothing between them, which left 194 of 293 unexplained on this
screen — and an unexplained gap of two thirds is indistinguishable from geometry
silently going missing. Culled and clipped are now printed, and `lost` is what
remains:

    CAP0050  tri=419  emitted=234 culled=162 clipped=29 rejects=0 lost=0
    CAP0150  tri=293  emitted=99  culled=171 clipped=28 rejects=0 lost=0
    CAP0250  tri=1293 emitted=904 culled=306 clipped=108 rejects=0 lost=0
    CAP0400  tri=943  emitted=510 culled=332 clipped=131 rejects=0 lost=0

**`lost=0` everywhere.** No geometry vanishes between the decoder and the
backend, on any scene in the corpus. That is a negative result worth having: it
was the first hypothesis and it is now excluded rather than still open.

**And it is not culling.** `--no-cull` draws both faces of everything. On the
copyright screen that raises emitted from 99 to 270 — and the same four textures
paint, `RAREWARE` still at zero. So the logo's quads are not being culled: no
triangle is drawn while that texture is bound.

**Nothing is drawn with it, by any path.** A count of *triangles* per texture
separates the two causes of "zero pixels" — nothing drawn while it was bound, or
something drawn that covered no pixel — and they are both present on this screen:

    tex001  82 triangles  19091 px    the plate
    tex003   0 triangles      0 px    RAREWARE
    tex006   3 triangles 288109 px    the visible sky
    tex007   3 triangles      0 px    a sky tile drawn off screen
    tex009   2 triangles      0 px       "
    tex013   2 triangles      0 px       "

The sky tiles are drawn and cover nothing, which is what a scrolling backdrop
outside the viewport looks like. `RAREWARE` is not drawn at all — and texture
rectangles go through `draw_triangles` in this decoder, so that path is counted
too.

## It was not a defect, and a second capture is what said so

The Rare logo **spins**. `CAP0160.BIN`, ten display lists later on the same
screen, shows it turned: a gold frame around a navy face carrying the letters.
The flat yellow rectangle of `CAP0150.BIN` is the back of that plate, seen
face-on, and it is correct.

The numbers follow the animation exactly:

| | list 150 | list 160 |
|---|---|---|
| emitted | 99 | 181 |
| `tex001` the gold plate | 82 tri, 19091 px | 113 tri, 7119 px |
| `tex002` the navy face | 0 tri, 0 px | 41 tri, 6355 px |
| `tex003` RAREWARE | **0 tri**, 0 px | **3 tri**, 0 px |

At 150 the letters are not drawn at all; at 160 they are drawn and face away.
Both are what a spinning logo does.

**The lesson is about the corpus and not about the renderer.** A scene sampled
once is a still, and a still cannot tell an animation from a defect. The first
capture supported a confident wrong reading — "a texture that never arrives" —
and the instruments built to chase it were what showed there was nothing to
chase. Two frames of a screen are worth more than one of each of two screens,
where anything moves.

`CAP0160.BIN` stays in the corpus for that reason: it is the control for
`CAP0150.BIN`.
