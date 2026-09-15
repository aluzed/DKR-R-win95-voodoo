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
| `CAP0800.BIN` | Wizpig and Diddy, the attract sequence | 1427 | 1397 | 755 | 85 |
| `CG0060.BIN` | **Ancient Lake, the start line, in play** | 4644 | 1995 | 1412 | 290 |

Five captures of four scenes that share almost nothing: one large model on a sky, a mostly
two-dimensional screen with text, an outdoor hub with 190 textures, and a race
with five-pass text. That is coverage of a kind — of the *decoder's* paths, not of
the game — and it is what the harness needed to stop being a one-scene instrument.

**It is four scenes, and the ticket asks for a lap of each level.** Recorded as
incomplete rather than presented as a corpus.

## How they were obtained, and what that cost

`DKR_CAPTURE_LIST=50,150,400` and the like, several to a run.

`CG0060.BIN` is the first taken from **play**: the game driven from the host
through PLAYER SELECT, GAME SELECT, TRACKS, DINO DOMAIN / ANCIENT LAKE, the
vehicle and the ghost prompt, into the race — then captured sixty display lists
after `gGameMode` reached 0, which is what `DKR_CAPTURE_MODE=0` means. The
anchoring did its job: mode 0 arrived at absolute list 1657 and the capture
landed at 1717, a number no run could have been asked for in advance.

It is also the densest scene here — **290 textures**, against 190 for the hub —
and the first that carries a HUD. The comma is what makes a corpus
practical: a capture costs a boot, a launch and a wait, so one per run is a day's
work for a dozen scenes.

Two obstacles were met on the way and both are recorded elsewhere:

- **Five captures of six were lost to the disk cache.** `fclose` hands the bytes
  to Windows 95 and Windows 95 keeps them; stopping the emulator took them. The
  writer commits before closing now. See the commit of 4 September 2026.
- **"The game reaches list 300 and stops" was wrong.** It was recorded here on
  4 September 2026 from four runs, and it is retracted on 10 September: a plain
  run with no capture armed reaches **list 2520 and 19,800 presents** in thirteen
  minutes and is still going when the machine is stopped, sitting on its title
  screen — the logo, the kart, START and OPTIONS, on the Voodoo.

  The error was in the instrument, not in the observation. `dkr_diag_commit`,
  which forces the log to the platter, ran **only from the display-list report**.
  So once the lists stopped the log stopped being written, and every run's log
  ended at its last list report whatever the rest of the runtime was doing. The VI
  thread's own report now commits too, and that is what showed presents and lists
  advancing together into the thousands.

  **What shortens the run is the capture itself.** With `DKR_CAPTURE_LIST=800`
  armed, the same build reaches list 800, writes the file — and stops there. The
  log's last line is the capture's own confirmation, and nothing follows it:
  neither a display-list report nor a VI present, on a log now committed from both
  threads. The screen goes back to the desktop with the game's window black, which
  is what every capture run has looked like since the first.

  So the rule is **one capture per run**, and that is a limit on the harness, not
  on the game. It is also enough: lists deep into a race are reachable now that
  the game is known to run to 2520 and beyond, and the corpus can grow one scene
  at a time.

  Why an eight-mebibyte write ends the run is not diagnosed. It predates the
  `_commit` added on 4 September — the run of 1 September ended at its capture
  too — so it is the write and not the flush.

## What the corpus has already shown

**The copyright screen looked broken and is not.** See below: it took two
captures ten display lists apart to establish that, and the first one on its own
said the opposite.

**And the hub draws large grey rectangles over the scene** — resolved the same
day, and not by finding a defect. They are a two-cycle combiner whose **second
cycle neither backend computes**: `DKR_CC_MULTIPASS` is a classification, not an
implementation, and both the oracle and the Glide backend fall through to the
nearest of four single-pass modes. The card and the oracle agree on that scene to
165 pixels of 307,200 and are both wrong in the same place. See
`docs/research/win95-multipass-visible.md`.

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

## Two scenes of the shredded font — 13 September 2026

`CKEY1622.BIN` and `CKEY0951.BIN`, both Taj's dialogue box in Timber's Island,
taken with the F9 key. They are the first captures of a defect the user reported
from a photograph, and they turn it from something only visible on the machine
into something reproducible on the development host.

    cmd=2430 tri=2864 emitted=1602 culled=593 clipped=719 rejects=0 textures=132

Ten scenes now, all at 0 divergent pixels oracle-to-oracle.

## The card against the oracle, scene by scene — 15 September 2026

E09-S02's remaining line is "the corpus: one capture is not coverage". This is the
first pass at the other half of it: every scene replayed through **both** backends
on the machine, and the two images differenced the same way each time — pixels
differing at all, and pixels at a gap of 32 or more, which is the threshold that
separates the Voodoo's dither from a defect.

Every row below is **verified**: the oracle image the target brought back was
held against a host render of the same capture first, and every one of the five
came back at zero sampled pixels beyond 8 levels. How, and why that matters, is
the section after the table.

| capture | scene | differ at all | gap ≥ 32 | after both pairs |
|---|---|---|---|---|
| `CAP0050` | the Nintendo 64 logo | 958,844 ppm | 107 | **106** |
| `CAP0150` | the copyright screen | 966,526 ppm | 36 | **36** |
| `CAP0160` | the same, logo turned | 951,402 ppm | 100 | **100** |
| `CAP0250` | the hub, Pipsy on the beach | 972,360 ppm | 1,540 | **450** |
| `CAP0400` | Ancient Lake, Bumper racing | 915,325 ppm | 489 | **492** |
| `CAP0800` | Wizpig and Diddy, attract | 912,893 ppm | 12,141 | **1,570** |
| `CG0060` | Ancient Lake, the start line, in play | 959,762 ppm | 1,071 | **753** |
| `CKEY1622` | the dialogue box, Taj's kart | 870,654 ppm | 1,295 | **557** |

16,779 pixels to 4,064: three quarters of the corpus's tail, from writing two
two-cycle configurations as the pairs of blends their arithmetic asks for.

The last column is the second pass rewritten as the exact pair the arithmetic
asks for — see `win95-oracle-vs-card.md`. It is the current state of the tree;
the column before it is what this baseline was first measured at, kept because a
baseline one cannot compare against is not a baseline.

**All eight scenes of the corpus now carry a figure, and every one of them was
verified before it was written down.** That is the card-side baseline E09-S02 had
been missing: a number per scene, obtained the same way, that a later run can be
held against.

**`CKEY1622` is also the cross-check this whole chain needed.** `win95-oracle-vs-card.md`
counted 1,293 pixels at a gap of 32 or more on that scene on 14 September, by a
different route — images fetched by hand, compared by a different script. Today's
run, verified against a host render first, says 1,295. Two pixels apart, through
two independent paths.

`CAP0250` is worth a second look on its own account — 1,540 pixels is the second
worst in the corpus, and its 190 textures and open scenery share nothing with the
attract sequence's caption. Nobody has asked yet where those pixels are.

It also settles what the broken sweep was doing: 1,540 at 972,360 ppm is exactly
the figure that sweep reported for `CAP0160`, one scene early.

`CG0060` is the densest scene in the corpus — 4,644 commands, 1,412 triangles
emitted, 290 textures, a race in play with the HUD up — and at 1,071 it sits in
the same band as the hub and the dialogue box. Density is not what costs.

Two readings. **The floor is the dither and it is everywhere**: nine tenths of
every scene differs by a few levels, and no decoder work will move it — the
Voodoo stores 565 and dithers into it, the oracle does neither. **Above the
floor, the scenes are two orders of magnitude apart**: the copyright screen is
clean at 36 pixels, and the attract sequence is at 12,141, of which 63 % sit in
one band — the caption, whose cause is `prepass_draw_texel_alone` not carrying
`alpha_scale` (see `win95-oracle-vs-card.md`).

## The check that has to come first: is it even the same scene?

This note carried `CAP0050` at 100 and `CAP0160` at 1,295 for an hour, and said
of the second that it was "the number to watch next — the same screen as
`CAP0150` ten display lists later, thirty-six times worse". It was not a defect.
It was two different scenes differenced against each other: a sweep that had gone
one iteration out of step, so that each card image met the previous scene's
oracle. Measured properly the two are 107 and **100**, and the copyright screen is
clean in both captures.

The oracle renders the same image on the host and on the target: that is E09-S02's
own finding, and it is what makes the check cheap. Render the capture through
`build/render-tools/replay` on the host, and compare it with the `RPLSOFT.BMP`
the target run brought back:

    CAP0150      0 of 76,800 sampled pixels differ by more than 8 levels
    CAP0400      0
    CAP0800      0
    CAP0050 74,918      <- not this scene, as first measured
    CAP0160 76,622      <- not this scene, as first measured

and after the two were re-run, one scene at a time, both at zero.

**Compare with a tolerance, not for equality.** The two builds agree to within one
level on about a tenth of the pixels — different compilers, different
architectures, the same arithmetic rounded differently — so an equality test calls
every pair a mismatch and says nothing. At a gap of more than 8 the answer is
binary: zero, or three quarters of the image.

The two bad pairs came from sweeps that overlapped: a stale run still writing
`RPLSOFT.BMP` while the next one was starting. Deleting the images before each run
was supposed to prevent exactly that and did not, because a second sweep was alive
that the first one knew nothing about. The lesson is not about sweeps: **a number
measured against an image nobody verified is not a measurement**, and this one
survived into a commit message and a table before the check was run.

### How to repeat it, and the three traps that cost a morning

Run them **one at a time**, and judge completion by the **artefact**:

    mdel  ::/RPLCARD.BMP ::/RPLSOFT.BMP        delete first
    Drive-Win95-VM.sh run "D:\REPLAY.EXE --both D:\CAP0050.BIN"
    poll until both reappear, then pull and compare

- A `.BAT` looks like the obvious way to chain eight scenes and is not: a Win32
  program launched from `COMMAND.COM` is not waited for without `START /W`, and
  even with it the DOS session dies when the program takes the screen full
  screen. Eight launches from the host beat one batch in the guest.
- **Do not judge completion by the screen.** `--both` renders the whole scene in
  software first, with the desktop still showing, and only opens the card at the
  end: a screen watcher calls that a failed launch and retries on top of a
  program that is working.
- **Do not judge it by the directory timestamp either.** It has minute
  granularity and says nothing about which run wrote the file; two scenes came
  back with identical figures that way, which is how the mistake was caught.
