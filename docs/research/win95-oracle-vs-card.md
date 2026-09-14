# The oracle confronted with the hardware

Measured on 14 August 2026 on the test machine, by
`platform/render/tests/test_compare.c` (`COMPARE.EXE`).

## What the confrontation established

The same synthetic scene passes twice through the complete chain — decoder,
transformation, clipping, backend — first towards the reference rasteriser, then
towards the Voodoo 2, whose frame buffer is read back.

    triangles emitted: software 4, card 4
    painted area: software 94848, card 94848 (0% deviation)
    plainly differing pixels: 0 out of 307200
    worst per-channel deviation over the whole image: 9

The worst deviation is **9 out of 255**: one quantisation step of red (8) plus one
of green (4), rounded differently. 77.45 % of the pixels are strictly identical.
No divergence of geometry, of depth or of colour remains.

That is not proof that the rendering is *right* — the game will be needed for
that. It is proof that two independent implementations of the same specification
agree, which is the only verification available without a ROM, and the one that
catches the costliest class of errors: those where every stage declares itself
satisfied while producing something other than what it announces.

> **Correction of 4 September 2026 — how independent they are, and where they
> are not.** They are independent below `dkr_render_state` and not above it. Both
> receive the decoder's collapse of the RDP's two-cycle combiner into one of four
> modes, and both then compute *that* faithfully. So an agreement proves the
> backend right about the state it was given and says nothing about the
> translation. Measured on a real frame: the hub agrees to 165 pixels of 307,200
> and both sides draw large grey quads where a second cycle should have blended a
> glow away. See `docs/research/win95-multipass-visible.md`.

## The point was not settled: the first measurement diverged over 24 % of the image

Three defects were found, all **in the oracle**, none in the card. That is the
point of the exercise: the reference rasteriser is the component nobody can check
any other way.

### 1. The colour was interpolated with perspective correction

The rasteriser divided `r`, `g`, `b`, `a` by `w` as it does — rightly — for `s`
and `t`. Neither Glide 2 nor the RDP corrects the colour: `GrVertex.r/g/b/a` are
iterated in screen space.

On an ordinary surface, the two interpolations differ by a few units and the error
stays invisible. It exploded on the first triangle **clipped at the near plane**:
the vertex created there carries an enormous `1/w` which, once weighted, imposes
its colour on the whole polygon. The oracle displayed a flat magenta where the card
produced a green gradient — over a third of the image.

An oracle whose target is Glide must iterate as Glide does, failing which it
accuses the hardware of a deviation it is itself the author of.

### 2. The depth was sorted on `z`, clamped at the vertex

A z buffer is legitimate, and it was the first choice. `dkr_clip_project` clamps
`z` to [0,1] — it must, a vertex created by the clipping comes out with a depth on
the order of −200000.

But **clamping at the vertex distorts the gradient over the whole primitive**: the
two ends are no longer at the same scale, and the interpolation lies everywhere
between them. The defect stays invisible over a whole surface and only appears
where a clipped primitive crosses another — here a corner of 3,500 pixels, where
the oracle and the card each named a different surface as being in front.

`oow` does not have that problem: it is `1/w`, it is affine in screen space, it
never needs clamping, and it is exactly what the Voodoo stores in its buffer. The
rasteriser therefore now sorts on `−1/w`.

`dkr_render_vertex.z` stays filled in: the RDP does sort in z, and the day one
wants to confront the port with the original rather than with the hardware, that
is the value that will be needed.

### 3. The scene was not testing the depth it claimed to test

With `z_clip = 0.5 z` and `w = z`, the ratio `z/w` is 0.5 for *every* vertex: the
quad and the clipped triangle ended up at exactly the same depth. Their overlap
produced a sorting conflict, to which the rasteriser answered with a dither
pattern and the card with a clean edge — two equally arbitrary answers to a badly
put question. The comparison was measuring that ambiguity rather than the
rendering.

A constant term on z makes `z/w = 0.5 − 20/z`, which varies with distance.

## Two trials had to be corrected, and that is normal

Changing the depth semantics made two checks that were passing fail. Neither was a
regression:

- the rasteriser's suite set `oow = 1` everywhere and stored the depth in `z`
  alone. The fixtures now fill both consistently;
- the chain trial looked for red **at the centre of the screen**. It found some
  there as long as the colour was perspective-corrected; the centre is now occupied
  by the clipped polygon, green and cyan. It now samples the quad where it stands
  alone, and further verifies that two distinct points differ — without which a
  flat colour would pass for a gradient.

The second case is worth remembering: **where one samples counts as much as what
one looks for there**, and a check aimed at a surface must aim at it where nothing
else covers it.

## The threshold was tightened afterwards

The first version tolerated 24 per channel. That was the right choice for clearing
the ground: wide enough not to be drowned by the quantisation, tight enough to see
a sorting divergence.

Once the three defects were fixed, that threshold can catch nothing more — it
would pass any regression smaller than a tenth of the scale. It is therefore
doubled by a check at 16, two quantisation steps: above the measured noise (9) and
well below any deviation that would have visual meaning.

A threshold one does not tighten after measuring the real noise ends up asserting
nothing but its own indulgence.

## What is not compared yet

Textures — the TMU allocator is E05-S02 and the combiner translation E05-S03. The
comparison today bears on geometry, iterated colour and depth, that is on
everything both backends know how to do.

## The card's divergence does not track the approximate share

14 September 2026. Ten scenes replayed on the Voodoo with `REPLAY.EXE --both`,
and `CAP0800` came out ten times worse than anything else: **40,224 divergent
pixels per million**, against 3 for `CAP0160` and 286 for `CAP0400`.

Two plausible causes were tested and both are out.

**Not a missing second pass.** `CAP0800`'s report shows a second cycle changing
38,492 ppm of the frame — near enough to the 40,224 measured to be tempting. The
card's own counters refuse it: `second pass: drawn=594`. It is drawing them.

Worth recording alongside: `--no-multipass` on the oracle changes **nothing**,
zero pixels, on this scene. That switch governs the *card's* second pass; the
oracle evaluates the real two-cycle combiner either way and has no single-cycle
mode. It cannot be used to predict what a card without a second pass would draw,
which is what it was reached for here.

**And not the approximate share**, which is the measurement that settles it:

| scene | approximate (ppm) | divergent (ppm) |
|---|---|---|
| CAP0160 | 6,574 | 3 |
| CAP0150 | 6,574 | 48 |
| CAP0050 | 0 | 78 |
| CKEY0540 | 21,409 | 185 |
| CAP0400 | **104,688** | **286** |
| CG0060 | **0** | **3,206** |
| CKEY1150 | **0** | **4,215** |
| CAP0250 | **0** | **4,475** |
| CKEY0951 | 16,270 | 5,885 |
| CAP0800 | 82,503 | 40,224 |

No relation. The scene with the largest approximate share by far diverges least
of the ones that diverge at all, and the three cleanest scenes by that measure —
zero approximate — sit in the middle of the divergence column. Whatever the card
and the oracle disagree about, the combiner shorthand is not it.

That leaves the per-pixel path: the Voodoo's dither, its 565 store, its texture
filtering. `CAP0800` is the attract sequence, which is mostly large smooth
gradients — the surface on which a dither differs from an undithered reference
everywhere at once, and small per-pixel amounts over a large area is exactly the
shape of 4 %. Testing it needs the card's image for that scene, which this run
did not bring back.

## Correction, and what the card's images actually show

The section above says "the combiner shorthand is not what the two backends
disagree about". **That is wrong, and the card's own images say so.** It was
inferred from a table of aggregate shares, and an aggregate cannot refute a
cause — only a measurement of the thing itself can, which is what was missing.

Both scenes were fetched off the machine and compared pixel by pixel.

### Nine tenths of the difference is dither, and it is not a defect

| | CKEY1622 | CAP0800 |
|---|---|---|
| pixels differing at all | 896,126 ppm | 912,893 ppm |
| of those, by 0–7 levels | 93 % | 86 % |
| sign alternation between neighbours | 62 % | 64 % |
| blocks touched, of 64 | 64 | 64 |

A difference that is everywhere, tiny, and **changes sign from one pixel to the
next** is a dither against an undithered reference. The Voodoo stores 565 and
dithers into it; the oracle does neither. Nine tenths of "the card disagrees" is
that, and no amount of decoder work will move it.

**What the comparison should be reading is the tail**, and `compare`'s
"frankly different" already is — its threshold is what separates the two.

### Above the floor, each scene has one defect and it dominates

`CKEY1622`: 1,293 pixels at a gap of 32 or more, and **56 % of them sit in three
adjacent 40x40 blocks** — the shadow beside Taj's kart. Everything else on that
screen is dither. The shadow is not one of several disagreements; once the floor
is subtracted it is very nearly the only one.

`CAP0800`: 12,141 pixels at 32 or more, **63 % of them in one horizontal band**
at y=360..440. The band is the attract sequence's caption, and the two images are
unambiguous: the oracle blends the letters into the orange behind them, pastel
and translucent; the card paints them **saturated and opaque**.

### And the cause is the shorthand, for one entry

The probe names it. The caption is painted by `G_CC_BLENDT_ENV_ALPHA_A_TxP`,
recipe 20, catalogued **approximate**, and the catalogue's own note says why:

    cycle 1  rgb = (ENVIRONMENT - TEXEL0) * ENVIRONMENT_ALPHA + TEXEL0
    note: factor = alpha of a constant register:
          measured on the card, no Glide factor delivers it

The RDP interpolates from the texel toward the environment colour. The card's
shorthand multiplies them. With an environment of `0xFF00FFFF` — cyan at full
alpha — those are not close, and that is exactly the difference between the
pastel and the saturated cyan.

### What still does not follow, and is the next measurement

The aggregate share genuinely predicts nothing, and that part of the table
stands: `CAP0400` carries **more** of this very entry — 104,688 ppm against
82,503 — and diverges by 286 ppm against 40,224. Same recipe, same shorthand,
a hundred and forty times less visible cost.

So the cost is not in how much of the entry a scene paints but in how far the
shorthand lands from the combiner in that scene's context. Both scenes set
similar environment colours, so the answer is not simply "a saturated
environment". Finding it means sampling recipe-20 pixels in `CAP0400` the way
this note sampled them in `CAP0800`, which five blind probes failed to hit.

### The blender is programmed: 21 of 21 entry points resolve

The obvious suspicion, once the combiner and the blend setup had both been read
and both checked out on paper, was that neither was reaching the card at all —
`dkr_glide_symbol` is `GetProcAddress`, and `apply_blend` opens with
`if (!gs.blend_function) { return; }`. An export that is not there makes the call
a no-op with no message, and an unprogrammed blender draws opaque, which is
exactly the symptom.

The report added for this says otherwise, on the machine, first line of the run:

    card opened with 2 texture unit(s)
    glide entry points: 21 of 21 resolved

So the blender is programmed, the combiner is programmed, and the card still
lands on the constant colour with the destination contributing nothing —
`0x00FBFF` against a constant of `0x00FFFF`, where the oracle puts `0x7ACC84`.
Source alpha reaching the blender is therefore ~255 where the combiner should be
handing it 102.

Three things are now excluded by measurement rather than by argument: the second
pass (the card draws 594 of them), the entry points (all resolve), and the shape
of the setup (`alpha = local_alpha x texel_alpha`, `SRC_ALPHA /
ONE_MINUS_SRC_ALPHA`, both read out of the source).

**The next measurement is a witness, not another reading.** `combiner_probe.c`
already establishes the pattern and its output is the catalogue's own evidence —
"measured on the card, no Glide factor delivers it" came from that family. What
is wanted is the same thing for one setup: program

    grConstantColorValue((102 << 24) | 0x00FFFF)
    grColorCombine(SCALE_OTHER, FACTOR_LOCAL, LOCAL_CONSTANT, OTHER_TEXTURE)
    grAlphaCombine(SCALE_OTHER, FACTOR_LOCAL, LOCAL_CONSTANT, OTHER_TEXTURE)
    grAlphaBlendFunction(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ZERO)

over a known destination with a known texture, and read the framebuffer back.
Either the Voodoo's alpha unit does not take its local from the constant
register, or `grConstantColorValue`'s alpha byte does not reach it. Both are
answerable in one frame, and neither is answerable from the host.
