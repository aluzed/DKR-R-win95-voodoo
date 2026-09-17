# What is left, and where each piece stands

Status is one of **PENDING**, **RUNNING**, **DONE**. Keep it current in the same
commit as the work: a tracker that lags is worse than none, because it is trusted.

Ordered by usefulness to the port, not by difficulty.

---

## 1. The copyright logo renders as a flat quad — DONE (not a defect)

**Resolved 17 September 2026: there is no defect.** The logo is **rotating**, and
`CAP0150` catches it showing a plain gold face. Counting colours in the logo's
area of the two captures ten lists apart:

    CAP0150   (255,222,90) 10388   (255,222,89) 2873   (254,221,89) 2762
    CAP0160   (255,222,89)  1933   (254,221,88) 1111   (0,2,60) 1078   (0,1,59) 859

The dark navy is present at 160 and absent at 150. The two live screenshots show
the same rotation at two angles, which I read backwards as a fade destroying a
texture.

`win95-corpus.md` already said "the copyright screen looked broken and is not",
and said why it is easy to get wrong. It was right.

What the investigation left behind, all of it still true and useful: the probe now
records **where in a texture** it sampled; the trace prints the raw per-corner
coordinates and vertex colours; and the polygon record's layout is confirmed
against `include/structs.h`. See `docs/research/win95-fade-loses-texture.md`,
which is kept with a retraction on top because how it went wrong is worth more
than what it concluded.

---

## 2. Fog: the blocker is found and named, fog still does not render — PENDING

**The question this item asked is answered, and it was the wrong question.** It
read "why does the blender half never fire". It does fire, and my "zero draws
fogged" was a bad measurement - a `grep` over a log that never printed the
counter. With the counter printed:

    CG0060    emitted=1412  fogged=1390     (98 %)
    CAP0250   emitted= 904  fogged= 739
    CAP0800   emitted= 755  fogged= 147

**Fog is enabled on most draws and moves the image by not one pixel**, because the
oracle's coefficient is `clampf(z, 0, 1)` with `z = -oow` - negative for every real
fragment, so `k` is zero at every pixel. That is a genuine latent bug and it is
why "fog is off" and "fog is on and does nothing" were indistinguishable for a
month.

**Refuted the same hour:** using the vertex alpha as the coefficient. With
`k = sa/255` the race collapses to 167 distinct colours and 89 % pure black -
August's black-screen disaster reproduced. DKR's vertices carry opacity in alpha
whatever the geometry mode says, so gating on `G_FOG` does not make the alpha a
coefficient.

**Found by measurement:** the lists *do* carry `G_MW_FOG` - `MOVEWORD` type
0x08, twice per capture, ignored by the decoder until now. It is decoded and
recorded (not applied):

    race and hub   w1 = 0x64009867     attract   w1 = 0x0F26F127

packing a signed multiplier in the high half and a signed offset in the low.

**What remains.** Compute the coefficient from depth with those two constants,
since this port does its own vertex transform and nothing else writes it. Two
things make that more than an afternoon: the convention has to be derived rather
than recited, and **validating it is the hard part** - both backends move
together, so the corpus can only check that they agree, not that they are right.
That is item 7 in miniature.

---

## 3. The rest of the geometry mode is read and unused — DONE (class is empty)

**Resolved 17 September 2026 by measurement: the two sources never disagree.**

Depth and culling are *derived* - from the blender and from the winding - and the
geometry mode *states* them. Both were available and nothing had confronted them,
so wherever they disagreed there would be a class of error. Counted per batch:

    CG0060    agrees on all 300 batches
    CAP0250   agrees on all 209 batches
    CAP0800   agrees on all  93 batches
    CKEY1622  agrees on all 231 batches

833 batches, zero disagreements. The class is empty, and the counters stay in the
report - silent while they agree, so that the day one disagrees it says so instead
of nobody asking again.

`G_SHADE` is still read and unused, which is deliberate: nothing derives shading
from anywhere else, so there is no second source to confront it with and no defect
to suspect.

---

## 4. `G_TEXTURE` (0xBB) — DONE (decoded; it carries no scale to miss)

**Resolved 17 September 2026.** Decoded, recorded, compared - in that order, which
was the point of the item. `gSPTexture` packs `w1 = s << 16 | t`, two unsigned
0.16 factors with 0xFFFF meaning one, the mip level at bits 11..13, the tile at
8..10 and the enable in the low byte of `w0`.

What the game actually sends, every occurrence in four captures:

    CG0060    2x   s=0x0000 t=0x0000 level=0 tile=0 on=0
    CAP0250   1x   s=0x0000 t=0x0000 level=0 tile=0 on=0
    CAP0800   1x   s=0x0000 t=0x0000 level=0 tile=0 on=0
    CAP0150   1x   s=0x0000 t=0x0000 level=0 tile=0 on=0

Always the disable form, never a scale. So there is nothing here the port was
losing: `tex_scale_s` is the S10.5-to-normalised conversion Glide wants, a
microcode scale would multiply it, and the multiplier is never sent. The enable
bit is redundant too - texturing is already decided per batch by bit 16 of the
polygon command, which is the finer source.

Decoded and left unapplied, with the counter in place. Applying a scale of zero
would erase every texture in the scene, which is a good illustration of why the
order in this item was "record, compare, then decide".

---

## 5. Nobody has driven the game into a race — PENDING (port side implemented)

**Two separate faults, one fixed, one still open.**

### Fixed: the Windows 95 build had no input at all

`poll_input()`'s body sat inside `#if DKR_RUNTIME_HAS_RT64` and this target
compiles with `-DDKR_RUNTIME_HAS_RT64=0` (confirmed in `build/win95/build.ninja`).
The `#else` branch **zeroed every controller every frame** - a deliberate stub, and
the whole of input on this target.

The first replacement reached for `dkr::runtime::input::poll` and did not compile:
the SDL window, the overlay and the device layer are compiled out here too. So the
branch now reads Win32 directly with `GetAsyncKeyState`, which needs no window and
no focus - right for a game holding the Voodoo full screen and owning no focusable
window. Mapping as the boot line has always announced it.

### Open: no key reaches the guest while the game holds the screen

Measured, with an unconditional witness in the new branch:

    [input] win95 poll_input called      x2       the poll runs
    [input] win95 buttons=...            none     no key is ever down

So `GetAsyncKeyState` sees nothing for any of fourteen keys across three Start and
A presses. The loss is **below the game** - in 86Box, or in how `xdotool` delivers
keys once the Voodoo is full screen. `grab` sets X focus and 86Box reports the
input captured, and it still arrives nowhere.

**Next step.** A harness question, not a port one: do keys reach a program that
holds the Voodoo full screen? Keys reach the guest perfectly well on the desktop -
that is how every program here is launched - so the full-screen transition is the
difference under test.

**Tried and withdrawn:** bolting a ten-second key poll onto `constant_alpha_probe`,
which already takes the screen. It came back as a **zero-byte file**, which is the
failure this repository has recorded before - Windows 95 leaves the directory entry
at zero until `fclose`, so anything that faults before it takes the whole run's
output. The section was added *before* the close, proved nothing, and destroyed the
rest of the report. It is removed.

Whatever asks this question needs to be **its own small witness that closes its
file first**, not a section appended to one whose output is fragile.

**Cost so far:** ten VM runs. Four of them narrowed by one conditional line each,
and three of those were spent because a *conditional* witness's silence is
ambiguous. **Make the first witness unconditional.**

---

## 6. The corpus exercises 8 configurations of 29 — PENDING

**What.** Measured from the captures' own fill reports. Ten of the twenty-one
unexercised are two-cycle — 1, 4, 6, 9, 12, 14, 16, 22, 28, 29 — which is where
the risk is: every defect of the week of 10–17 September was in a two-cycle
configuration.

**What is already covered.** `COMBINER.EXE` puts all twenty-nine on the card and
compares against the closed form: **0 failures**, measured 17 September. So the
combiner *arithmetic* is verified for all of them. What is not exercised is their
behaviour in a scene — interpolation, blending against a real destination, depth,
multipass sequencing.

**Next step.** Choose the next capture by which configurations it would exercise,
not by which level it is. The fill report of a candidate answers that before
anyone looks at an image.

---

## 7. The corpus measures agreement, not correctness — PENDING

**What.** Every figure in `win95-corpus.md` compares the card against the oracle,
and both are fed by the same decoder. A configuration the decoder resolves wrongly
is rendered wrongly and **identically** by both, and the comparison reports perfect
agreement.

**Known instances.** The hub's grey rectangles (recorded) and the flat logo of
item 1 — which sits in the corpus at 36 divergent pixels, one of its best scores.

**Why it conditions everything else.** The corpus cannot find this class at all.
The only instrument for it today is running the game and looking, which is item 5.

**Next step.** None obvious that is cheap. Worth thinking about whether a third
reference exists — a frame from the console, a known-good emulator image — that
could turn agreement into correctness for even a handful of scenes.

---

## Recommended order

**1** first: nearly finished, needs no machine. Then **5**, because it is the only
thing that attacks **7**. The rest can wait.
