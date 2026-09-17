# What is left, and where each piece stands

Status is one of **PENDING**, **RUNNING**, **DONE**. Keep it current in the same
commit as the work: a tracker that lags is worse than none, because it is trusted.

Ordered by usefulness to the port, not by difficulty.

---

## 1. The copyright logo renders as a flat quad — PENDING

**What.** The Rare logo comes out as one flat gold shape where it should have a
gold border and a dark navy interior. Live and in `CAP0150`, which has carried it
for a fortnight under the heading "looked broken and is not".

**Where it stands.** Cornered. Three guesses were made and two were refuted by
measurement: `G_TEXTURE` (a wrong scale multiplies, it cannot flatten) and the
coordinate offsets (confirmed against `include/structs.h` — flags, three vertex
indices, three coordinate pairs at 0x04/0x08/0x0C, sixteen bytes). The uniform
coordinate per triangle is **real data**, and the object is drawn in flat colours
with the texture supplying shade rather than pattern.

**The question now.** Why do the triangles that should be dark come out gold?
Candidates, none measured: vertex colours ignored where they should modulate; a
separate batch not drawn; drawn and lost to the depth test, as draw 3 of the probe
is.

**Next step.** Probe a pixel inside the dark region of `CAP0150` and compare it
with a gold one. The probe now reports which draws, at what depth, with which
coordinates and which registers. **No machine, under an hour.**

See `docs/research/win95-fade-loses-texture.md`.

---

## 2. Fog is decoded, gated, and never fires — PENDING

**What.** Fog was forced off since the port began. It now needs both of its real
conditions: the blender set up to mix a fog colour in, and the geometry mode
saying `G_FOG`.

**Where it stands.** The geometry-mode half works — `G_SETGEOMETRYMODE` (0xB7) and
`G_CLEARGEOMETRYMODE` (0xB6) are read, and the game sets `G_FOG` on most 3D
batches, `0x00010205` being its ordinary mode. The gate is in place and **inert**:
zero pixels moved, zero draws fogged, across three scenes.

**The question now.** Why is `G_RM_FOG_SHADE_A` never deduced from these lists, in
a game that plainly renders fog? That is the blender half, in `rdp_state.c`.

**Next step.** Trace the blender words the lists actually carry and compare with
what the deduction expects. **No machine.**

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
