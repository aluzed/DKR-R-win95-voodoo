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

## 3. The rest of the geometry mode is read and unused — PENDING

**What.** `G_ZBUFFER` and `G_CULL_FRONT`/`G_CULL_BACK` are decoded now and nothing
reads them. Depth mode and culling are derived elsewhere, by other means.

**Why it matters.** Two independent sources for the same fact. Wherever they
disagree there is a class of error, and nobody has confronted them.

**Next step.** For each batch, compare the geometry mode's claim against the
derived `dkr_depth_mode` and cull mode, and count the disagreements. A counter and
one replay. **No machine.**

---

## 4. `G_TEXTURE` (0xBB) is still dropped — PENDING

**What.** One per capture, counted as `deferred`. It carries the microcode's s and
t scale factors.

**Where it stands.** Exonerated for the flat logo — that was guess one, refuted.
But it is still a command with a rendering effect that is thrown away, and the
scale it states is currently *derived* from the tile size instead.

**Next step.** Decode it, record it, compare with the derived scale, and only then
decide whether to apply it. The order matters: the same mistake as fog is to wire
something before knowing what it says.

---

## 5. Nobody has driven the game into a race — PENDING

**What.** The largest gap. The game reaches the character-select carousel and the
copyright sequence. Menus, track select, a race, the results screen and split
screen are **entirely unverified**.

**Where it stands.** Input reaches the guest only after `grab` (X focus without a
click — clicking drops the Voodoo full screen). With that, Start works and the
menus respond.

**Cost.** About seven minutes a run, and the path is several screens deep.

**Why it matters.** It is the only thing that attacks item 7.

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
