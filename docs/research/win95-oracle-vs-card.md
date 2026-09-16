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

## The witness was written, and both candidates are wrong

`constant_alpha_probe.c` (`CONSTA.EXE`), run on the machine, 14 September 2026.
The frame buffer has no alpha, so the blender is made to reveal it: over a black
destination with `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` the stored pixel is
`source x alpha`, and the source is measured first with blending off. One
division against one measured reference, rather than a model of the 1555 texel,
the 255/256 truncation and the 565 store stacked on each other.

    the constant alone, FUNCTION_LOCAL / LOCAL_CONSTANT
       0 ->   0     51 ->  48    102 -> 101    153 -> 150    204 -> 203   255 -> 255

    the game's setup, SCALE_OTHER x FACTOR_LOCAL, LOCAL_CONSTANT, OTHER_TEXTURE
       texel alpha 255:  102 -> 101          (wanted 102)
       texel alpha 136:  102 ->  52          (the product is 54; the constant
                                              alone would be 102)

    102 in each byte position, FUNCTION_LOCAL
       0x00000066 -> 0    0x00006600 -> 0    0x00660000 -> 0    0x66000000 -> 101

So the alpha unit **does** take its local from the constant register, the top
byte **does** reach it, and the product with the texel is what comes out. Both
candidate causes are refuted, and the caption's opacity is not in that setup.

## It is in the pass that replaces it

The state is not in doubt either: `replay --probe 267,389` on `CAP0800`, run on
the host and again on the target, reports the same five draws at the pixel where
the card paints `(0,255,255)` and the oracle `(45,102,132)`:

     4  0x5E0000 -> 0x4B0033  TEX*CONST  const=0xFF0000FF ascale=51   recipe=20
     5  0x4B0033 -> 0x2D6684  TEX*CONST  const=0xFF00FFFF ascale=102  recipe=20

`alpha_scale` is 102, and `apply_combine`'s `TEXTURE_CONSTANT` would put it in
the constant register's alpha — the setup the witness just certified. **That path
does not run.** Recipe 20's first cycle is `(ENV - TEXEL0) * ENV_ALPHA + TEXEL0`,
which `prepass_shape` recognises as `PREPASS_TEXEL_ALONE`, and
`prepass_draw_texel_alone` *replaces* the ordinary draw. Its alpha combiner is

    SCALE_OTHER / FACTOR_ONE / LOCAL_ITERATED / OTHER_TEXTURE

the texel's alpha and nothing else. The mux's factor never reaches the card, the
blender gets 255 where the RDP says 102, and the caption is opaque.

## Why `CAP0400` gets away with it

The question this note left open — more of the entry, a hundred and forty times
less divergence — has an answer, and it is not "how far the shorthand lands".
`--recipe-map` finds the 9,272 recipe-20 pixels of `CAP0400` and the probe reads
them:

    (220,386)   ascale 51, 102, 153, 204       four passes, then another draw
    (424,404)   ascale 51, 102, 153, 204, 255  the fifth writes 0x000000

The **same ladder** as `CAP0800` — DKR paints its text in five passes at 51, 102,
153, 204 and 255. Where the ladder's last pass lands on a pixel, it writes over
the four wrong ones and the defect is invisible. On the attract caption the
visible pass is the one at 102, and nothing covers it. The cost is not in how
much of a configuration a scene paints, nor in the configuration's context: it is
in **whether a later pass covers it**.

## Supplying the scale: tried, measured, refused

The one-line change is obvious and it was made: program the constant's alpha with
`alpha_scale` and take the alpha through `FACTOR_LOCAL / LOCAL_CONSTANT`, the
setup the witness had just certified. Measured on the machine, same capture, same
metric:

    CAP0800, gap >= 32     12,141  ->  18,695
    newly beyond 32                    7,657, every one in the caption's band
    brought under 32                   1,103

**Worse**, and the change is reverted. The reason is the other half of the same
pass, and it is now measured too.

## Factor `0x0B` fetches nothing: it is a constant 0.97

`prepass_draw_texel_alone` carries the environment in the vertex so that
`BLEND_OTHER / ONE_MINUS_LOCAL_ALPHA` fetches the lerp factor `k` from the
iterated alpha. The file already doubted it — "what this setting actually
computes on a font atlas is not known". The witness settles it. With a white
texel as `other` and a cyan iterated colour as `local`, the result
`(T - E) * f + E` puts `255 x f` in the **red** channel and nothing else:

    iterated alpha   0 .. 255  ->  factor 247, unchanged
    constant alpha   0 .. 255  ->  factor 247, unchanged
    texel alpha         136    ->  factor 247

A constant `247/255`. The pass computes very nearly **the texel alone**, the
RDP's cycle at `k = 0`, which is exactly what its name says and not what its
comment says. So scaling its alpha composites a wrong colour more visibly rather
than less, which is the 18,695 above. The colour and the alpha have to be fixed
together or not at all, and neither route reaches it today: no Glide factor
delivers a register's alpha in one pass, and the two-pass decomposition was
measured four to eighteen times worse on 9 September.

What is left unexplained is narrower than before, and sharper. `SCALE_OTHER /
ONE` computes `other`, and `BLEND_OTHER / 0x0B` at `f = 0.97` computes very
nearly `other` as well — yet the two give 801 divergent pixels against 17 on the
copyright screen, both firing twenty times. Two settings that compute almost the
same thing do not differ by forty-seven times. One of those two images is saying
something that has not been read yet, and that is where the next measurement
goes.

## The factor sweep nobody had run, and what it refutes

The question above — two settings that compute nearly the same thing, 801
divergent pixels against 17 — was taken up the same evening. First it was
reproduced with today's build and today's metric, on `CAP0150`:

    BLEND_OTHER / 0x0B    36 pixels at a gap of 32 or more
    SCALE_OTHER / ONE    640

Then the two card images were differenced against each other, which no earlier
run had done:

    776 pixels differ, every one of them in rows 400..440 — the copyright text
    (369,421)  0x0B = (255,255,255)   ONE = (140,138,140)   oracle = (255,255,255)

So `BLEND_OTHER / 0x0B` is exact on those pixels and `SCALE_OTHER / ONE` is 115
levels dark. The obvious reading is that one of the two factors is not what it is
called, and `constant_alpha_probe.c` was extended to sweep all sixteen with
`other` driven from the **texture** — the configuration
`combine_enum_probe.c` never covered, and the one the game draws text in. With a
white texel as `other` and a cyan iterated colour as `local`, the red channel is
`255 x factor` under either function:

    factor   SCALE_OTHER   BLEND_OTHER        texel alpha 136 (4444)
    0x04     255           255                132  <- the texel's alpha
    0x05     255           255                255
    0x08     255           255                255  <- called ONE, and it is one
    0x09     255           255                255
    0x0B     247           247                247  <- the one in use
    0x0C       0             0                115  <- one minus the texel's alpha
    every other value reads zero

`0x04` is `TEXTURE_ALPHA` and `0x0C` its complement, as the fourth sweep had
said. **`0x08` is one, even on a texel that is not opaque**, and `0x0B` is one
less three per cent. The two differ by 8 levels out of 255, and no configuration
moves either: the iterated alpha, the constant's alpha, the texel's alpha, the
local's colour, the alpha unit's own setup, the sampling scale from 15x
magnified to 2x minified, and point against bilinear — seven sweeps, and the
columns read 247 and 255 throughout.

**So the scene's 115 levels cannot come from the factor.** Whatever
`BLEND_OTHER / 0x0B` computes, `SCALE_OTHER / ONE` computes to within three per
cent of it, and the arithmetic of the composite leaves no room: the difference
between the two card images at that pixel is 115 levels, where `0.03 x (E - T)`
can be at most 8.

That is a sharper statement than the one this note started with, and it moves the
search. The next measurement is not about the factor at all — it is about what
else changes when that branch is taken. The two witnesses are in the tree and the
reproduction costs one run of each.

## The variable nobody had varied: the blender

The section above ends by saying the scene's 115 levels cannot come from the
factor, because seven sweeps read it as one and the two settings differ by
eight. Every one of those sweeps read the combiner the way a combiner is read —
**with blending off**. That is the one case in which this factor is inert.

The scene was reproduced outside itself first. The witness builds the copyright
text's state — recipe 20, `TEXTURE_CONSTANT`, constant and environment white at
full alpha, blending on — hands it to `gl_set_state`, and draws over a dark
texel:

    default  (BLEND_OTHER / 0x0B)    0xF7FBF7      the local, white
    switched (SCALE_OTHER / ONE)     0x292829      the texel, dark

206 levels apart, outside any scene. Re-issuing the same programming by hand over
that state gives the same two numbers, so the difference is in the state and not
in the draw. Varying the state one field at a time names it:

    blend opaque    BLEND_OTHER / 0x0B -> 0x292C29     the texel
    blend opaque    SCALE_OTHER / ONE  -> 0x292829     the texel
    blend alpha     BLEND_OTHER / 0x0B -> 0xF7FBF7     the local
    blend alpha     SCALE_OTHER / ONE  -> 0x292829     the texel

**`GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA` is exactly what it is named — while
the blender is on.** With `ONE / ONE` over black, a texel of 41 against a local
of 255:

    alpha   0 ->  41    the texel        (factor one)
    alpha 128 -> 148    halfway          (the lerp)
    alpha 255 -> 247    the local        (factor zero)

`(T - L) x (1 - alpha) + L` matches every row. With the blender off it reads one
and nothing moves it, which is what the seven sweeps saw.

## And the alpha it reads is the alpha unit's output

The sweep above moves the vertex alpha while the alpha unit is parked on it, so
it cannot say which of the two the factor reads. Holding the vertex at 255 and
making the alpha unit deliver something else separates them:

    the alpha unit delivers   0   51  102  153  204  255
    the colour comes back    41   82  123  165  206  247
    predicted by the lerp    41   84  127  170  213  255

It reads the **alpha combiner's output**. Three consequences, and they close
three open questions at once.

**The vertex route works.** `prepass_draw_texel_alone` really does compute the
RDP's `(ENV - TEXEL0) * k + TEXEL0`, with `k` taken from the alpha unit. The
comment saying the factor fetches `k` from the *iterated* alpha names the wrong
register, and the note added on 14 September — "a constant 247/255, fetching
nothing" — was read with the blender off and is wrong.

**The copyright screen's 801 against 17 is explained.** On a glyph the texel
alpha is 255, so the factor is zero and the card paints the environment — white,
which is what the RDP paints at `k = 1`. `SCALE_OTHER / ONE` paints the texel
instead, and the texel is not white.

**And so is why supplying `alpha_scale` made the caption worse.** The value the
alpha unit delivers is two things at once: the blend factor the frame buffer
will use, and `k` in the colour unit's lerp. Scaling it to 102 to carry the RDP's
alpha mux scales the lerp to 0.4 with it, the texel bleeds into what should be
flat environment colour, and 7,657 pixels go wrong to buy 1,103 back. One
register, two meanings — that is the whole of why this configuration is hard on
this card, and it is a sharper statement than the catalogue's "approximate".

Carrying both needs a second pass, measured four to eighteen times worse on an
alpha-blended background, or the scale folded into the texture's own alpha, which
nothing does yet.

## The hub's 1,540: a two-pass decomposition that composites in the wrong order

`CAP0250` is the corpus's second worst scene, and its divergence is not spread:
66 % of the 1,540 pixels at a gap of 32 or more sit in three adjacent 40×40
blocks, a single compact blob beside Pipsy's kart where the sea foam is drawn.
The probe says what paints it — a stack of translucent sprites, one draw each:

     1  0x3163DE -> 0x4A90FF  TEX*SHADE+A const=0x00FFFFFF ascale=255 recipe=3
     2  0x4A90FF -> 0x7AACE9  TEX*CONST   const=0x45FFFFFF ascale=69  recipe=8
     3  0x7AACE9 -> 0x97B9D0  TEX*CONST   const=0x55FFFFFF ascale=85  recipe=8
     4  0x97B9D0 -> 0xB3CAC6  TEX*CONST   const=0x45FFFFFF ascale=69  recipe=8
     5  0xB3CAC6 -> 0xB3CAC6  TEX*CONST   const=0x62FFFFFF ascale=98  recipe=8

Recipe 8 is `G_CC_MODULATEIA_PRIM + G_CC_BLEND_ENV_ALPHA2`, which is also **what
DKR draws its shadows with**:

    cycle 1  rgb = TEXEL0 x PRIMITIVE          alpha = TEXEL0_A x PRIMITIVE_A
    cycle 2  rgb = (ENV - COMBINED) x ENV_A + COMBINED    alpha = COMBINED_A

The alpha is right on the card — `constant_color` carries the primitive's alpha,
which is what `alpha_scale` reads, and the recipe's setup multiplies the texel by
it. The **colour** is where the two part company, and it is the decomposition
rather than any single register.

The RDP composites the finished cycle-2 colour once, with the cycle-1 alpha:

    out = [ C + (ENV - C) e ] a + dst (1 - a)        a = t p,  C = cycle 1

The card draws it as two blended passes, `pass2_draw` over the recipe's own:

    dst1 = C a + dst (1 - a)                        the first pass, correct
    out  = ENV (t e) + dst1 (1 - t e)               the second, over the result

The second pass does not add the environment to the *colour*; it lays the
environment over the **already composited frame buffer**, and darkens what is
behind it by `1 - t e` into the bargain. The two agree when `t e` is small or when
the destination happens to equal `C`, and the sea foam is neither: a dozen sprites
stacked at alphas from 19 to 176, each one compounding the previous one's error.

That makes this the second known cause in the corpus, and the two together are
its whole tail. `CAP0800`'s caption is `prepass_draw_texel_alone` dropping
`alpha_scale`; `CAP0250`'s foam is `pass2_draw` compositing in the wrong order.
Both come of the same structural fact: **a two-cycle combiner decomposed into two
frame-buffer blends is not the same arithmetic**, and only the first pass can be
made exact by programming.

### And the measurement says keep it

The same scene with the second pass suppressed, against the same reference,
verified the same way:

    CAP0250, gap >= 32     with the second pass   1,540
                           with --no-multipass   12,624

**Eight times worse without it.** Pixel by pixel the second pass fixes 11,504 and
breaks 420 — twenty-seven to one — and what it fixes is not the foam at all but
the water and the beach down the right of the screen, three whole blocks of it.
The decomposition is a large net gain and it stays; the 1,540 is its residue, and
420 of those are pixels it actively breaks.

So the gate has its number, and it is not the one the reading above suggested.
The wrong-order composition is real arithmetic, and it is still far better than
leaving the environment out of the frame entirely — which is what the first pass
alone does.

### The pair that is exact, and what it recovered

The residue has a fix, and it needs no new capability — only the right constant
in each pass. Expand what the RDP computes:

    out = [ C + (ENV - C) e ] a + dst (1 - a)
        = C a (1 - e)  +  ENV a e  +  dst (1 - a)

Three terms, and the last two lines are two frame-buffer blends with no negative
source anywhere:

    pass 1   src = C (1 - e),  alpha = a      SRC_ALPHA / ONE_MINUS_SRC_ALPHA
    pass 2   src = ENV,        alpha = a e    SRC_ALPHA / **ONE**

Pass 1's `(1 - e)` goes into the constant's colour, folded in by `gl_set_state`
where the CPU knows `e` and the card has no factor that would deliver it; its
alpha byte still carries `p`. Pass 2's constant keeps the environment's colour
and takes `p x e` for its alpha, so the alpha unit's `texel x constant` hands the
blender `t p e` — and `ONE` on the destination **adds** the term instead of
compositing over the finished frame buffer and darkening it by `1 - t e`.

Measured over the whole corpus, each scene verified against a host render first:

| capture | before | after |
|---|---|---|
| `CAP0050` | 107 | 108 |
| `CAP0150` | 36 | 36 |
| `CAP0160` | 100 | 100 |
| `CAP0250` | 1,540 | **573** |
| `CAP0400` | 489 | 489 |
| `CAP0800` | 12,141 | 12,171 |
| `CG0060` | 1,071 | **753** |
| `CKEY1622` | 1,295 | **557** |

The three scenes that paint this configuration in quantity fall by 30 to 63 %,
1,992 pixels in all; the four that do not are unchanged to within a pixel. The
exception was `CAP0800` at +30, and chasing it found the other half of the
derivation — the section below. The column above is superseded by the one there.

Thirty-five pixels newly beyond the threshold, five brought under it, and
twenty-nine of the thirty-five in **one 40x40 block** at x 280-320, y 280-320.
The probe finds two recipe-8 draws there, one opaque and one alpha-blended, over
four earlier ones. Their after-values carry the oracle's **red to the level** -
66 against 66 - and fall about thirty short in blue: 74 where the oracle puts
106.

The first reading was that an opaque first pass composites no `a`, so the term to
add should be `ENV e` and not `ENV a e`. It is a distinction without a
difference: those draws carry `alpha_scale` 255, the two expressions are the same
number, and writing the branch changed nothing on any scene - `CAP0800` 12,171,
`CAP0250` 573, `CKEY1622` 557, all unmoved. The branch was removed again, with
the reason left where the next hand will reach for it.

So the thirty-five are not the alpha and not the environment's own colour, whose
red they reproduce exactly. One channel that lands and one that does not, inside
a single block: narrow enough to chase, and the chase found it.

### An opaque first pass adds `ENV e`, and no coverage with it

The probe at `(287,292)` shows two recipe-8 draws over five earlier ones, and the
first of the two is **opaque** with an alpha test. That changes the arithmetic,
and the earlier attempt had the right suspicion and the wrong factor.

An opaque first pass does not composite: it writes `C (1 - e)` **over** the
destination, no `a` takes part, and the term to add is `ENV e` alone. The pair as
written added `ENV t p e`. Dropping `p` changed nothing because `p` was already
255 — that is the branch that was written and removed. It is **`t`** that does
not belong, and `t` cannot simply be taken out of the alpha unit: the **alpha
test** reads the same value, and it is what keeps the second pass inside the
cutout the first one applied.

So the coverage stays in the alpha and `e` moves into the colour, where the CPU
can apply it. The constant carries `ENV x e`, the colour unit passes it through,
and the blender adds it with `ONE / ONE` instead of scaling it by an alpha that
is there for the test:

| capture | baseline | the exact pair | with the opaque term |
|---|---|---|---|
| `CAP0050` | 107 | 108 | **106** |
| `CAP0150` | 36 | 36 | 36 |
| `CAP0160` | 100 | 100 | 100 |
| `CAP0250` | 1,540 | 573 | **450** |
| `CAP0400` | 489 | 489 | 489 |
| `CAP0800` | 12,141 | 12,171 | **12,099** |
| `CG0060` | 1,071 | 753 | 753 |
| `CKEY1622` | 1,295 | 557 | 557 |

16,779 → **14,590** over the corpus — 2,189 pixels — and **every scene is now at
or below its baseline**: the regression is gone, `CAP0800` is 42 below where it started, and
the hub takes another 123 off. The soft edge of a sprite is where it showed —
`ENV e (1 - t)` lost on every pixel whose texel alpha is not full, and the
environment is where this scene keeps its blue.

`CKEY1622` is the one to notice: recipe 8 is what DKR draws its **shadows** with,
and the dialogue scene's shadow is what E09-S02 spent two days on in September.
More than half of what was left of it was this.


## The caption, finished: the same pair, and 12,141 becomes 1,570

`prepass_draw_texel_alone` drew `(ENV - TEXEL0) x ENV_ALPHA + TEXEL0` in one
stage, with the environment carried in the vertex and the lerp factor fetched
from the alpha unit. The measurements above established what that really did:
the factor **is** `1 - alpha` and it **does** read the alpha unit's output - but
the alpha unit was delivering the texel's alpha, so the pass computed `1 - t`
where the RDP wants `1 - e`, and handed the blender `t` where the mux says
`t p`. One register, two meanings; no programming of one pass can carry both.

The same expansion that fixed `pass2_draw` fixes this, and for the same reason:

    out = [ T (1 - e) + ENV e ] a + dst (1 - a)          a = t p
        = T (1 - e) a  +  ENV e a  +  dst (1 - a)

    pass A   src = T (1 - e),  alpha = t p      the state's own blend
    pass B   src = ENV e,      alpha = t p      SRC_ALPHA / ONE

One constant serves each pass whole — `(1 - e)` as a grey in A's colour and `p`
in its alpha, `ENV x e` in B's colour and `p` again in its alpha — so the CPU
supplies both scalars the card has no factor for, and nothing is read from the
vertex. An opaque first pass takes `ONE / ONE` and `ENV e` alone, as in
`pass2_draw`.

| capture | baseline | before | **after** |
|---|---|---|---|
| `CAP0050` | 107 | 106 | 106 |
| `CAP0150` | 36 | 36 | 36 |
| `CAP0160` | 100 | 100 | 100 |
| `CAP0250` | 1,540 | 450 | 450 |
| `CAP0400` | 489 | 489 | 492 |
| `CAP0800` | 12,141 | 12,099 | **1,570** |
| `CG0060` | 1,071 | 753 | 753 |
| `CKEY1622` | 1,295 | 557 | 557 |

**16,779 → 4,064 over the corpus, 75 % of the tail gone**, and the attract
sequence alone falls from 12,141 to 1,570. Nothing regressed but `CAP0400`, by
three pixels.

Two earlier readings are corrected by this. The decomposition tried on 9
September was measured four to eighteen times worse and rejected: it laid the
texel down **opaque** under states that blend, which is composition rather than
summation - pass A here takes the state's own blend and pass B adds. And the
note of 14 September, that supplying `alpha_scale` to the single pass made
`CAP0800` worse, was true of the single pass and is now beside the point: there
is no single pass to supply it to.

`dkr_glide_backend_texel_factor_one` now selects the old one-stage form rather
than a variant of it, so one boot can still measure both.

## What is left in the attract sequence, and it is older than any of this

`CAP0800`'s remaining 1,570 are not the caption. The worst of them lie along a
diagonal at the top right — `(639,154)`, `(632,156)`, `(608,163)`, down to
`(553,179)` — and the probe finds **one draw** on each: recipe 3, texture 4,
`blend=additive`, `ascale=255`.

    oracle   0x3163DE -> 0x8CFFFF        (49,99,222) + src
    card      (132,77,0)

The card's value is **lower than the destination** in green and blue. An additive
blend cannot do that: `ONE / ONE` only ever adds. So whatever that draw was given
on the card, it was not the additive blend the state asks for — and the pixels
read the same in the baseline images from before any of this week's work, so it
is older than the two pairs and untouched by them.

Widening the second pass's "did the first one composite" test from opaque to
*anything but the alpha composite* — which is right by the derivation, since
additive weights the source by nothing either — left all eight scenes exactly
where they were. No capture in the corpus reaches a second pass from an additive
state. The change is kept on the arithmetic and is recorded as unmeasured.

That leaves the sky edge as the next thing to chase, and it is a narrow one: a
single draw, a known texture, and a blend that demonstrably is not the one the
state names.
## How far the `(1 - e)` fold reaches, and why it does not matter

`gl_set_state` folds `1 - e` into the **constant's** colour, which reaches every
configuration whose first pass reads `LOCAL_CONSTANT`. `G_CC_MODULATEIDECALA`
does not: its setup is `SCALE_OTHER / FACTOR_LOCAL / LOCAL_ITERATED /
OTHER_TEXTURE`, the texel by the iterated colour, so the constant it never reads
can be scaled to no effect. And that configuration paints **826,644 ppm of the
attract sequence**, more than any other — which made this look like the next
thing to fix.

It was written: the vertex colours scaled by `1 - e` into a batch buffer, in the
draw path, for exactly the states where the first pass reads the iterated colour
and a second pass by the environment's alpha is coming. Measured on four scenes,
it changed **nothing at all** — `CAP0800` 1,570, `CAP0150` 36, `CAP0250` 450,
`CG0060` 753, every one identical.

The reason is in the probe and is worth keeping. Those draws carry
`const=0x00FFFFFF`: the environment register's **alpha is zero**, so the second
cycle `(ENV - COMBINED) x ENV_ALPHA + COMBINED` is the identity, `pass2_wanted`
refuses it by the guard that exists for precisely that, and no second pass is
drawn — so there is no `1 - e` to fold. The configuration that paints most of the
scene asks nothing of this machinery.

The code was removed again rather than kept on the argument. A branch in the draw
path and a static buffer, to serve a case no capture reaches, is the shape of
something that rots unverified — and this repository has a file of them.

What the corpus does still hold is `CAP0800`'s 980 real pixels, in two blocks
(`x 200-240, y 160-200` and `x 280-320, y 320-360`), where the card paints near
black — `(41,24,41)` — and the oracle paints white. They read identically in the
images from before any of this week's work. Three recipe-3 draws touch them, two
of which the oracle uses to build that white. That is the next thing, and it is
not a combiner arithmetic problem: it is a draw that is not arriving.

## What the two backends agree on, and the one count that differs

`CAP0800`'s 980 real pixels are in two blocks where the card paints near black
and the oracle white. "A draw that is not arriving" was the guess; the log says
otherwise. Run with `--log`, the two backends report:

    oracle   cmd=1427 tri=1397 emitted=755 culled=385 clipped=275 rejects=0 lost=0 textures=85
    card     cmd=1427 tri=1397 emitted=755 culled=385 clipped=275 rejects=0 lost=0 textures=58

**Every geometry count is identical** — the same commands, the same triangles
emitted, the same culled, the same clipped, nothing rejected and nothing lost. So
no draw is missing, and the difference is not in the decoder's arithmetic either,
since both ran the same code to produce those numbers.

The one count that differs is `textures`, 58 against 85. That is the decoder's
count of conversions, not of uploads, and the obvious suspicion — that the card
refused twenty-seven textures and drew with whatever the unit held — is now
answerable, because `replay` prints the four causes the backend has always kept
apart:

    uploads refused: none

Nothing was refused for its aspect ratio, its size, a full descriptor table or
exhausted TMU memory, and no slot was reclaimed.

And the reason the card converts 58 where the oracle converts 85 is the decoder's
own fast path, which the same report now counts:

    oracle   textures=85 (resident=0  reused=0)
    card     textures=58 (resident=27 reused=0)

`f3ddkr.c` asks the backend `texture_lookup` before converting anything, and
skips the conversion when the answer is a handle — counting `textures_resident`
instead of `textures_loaded`. The software rasteriser offers no such service and
converts all eighty-five; the card finds twenty-seven of them already in its
memory. **58 + 27 = 85**: the same textures, one of the two backends simply
doing less work for them.

So the 980 are neither a missing draw, nor a missing upload, nor a missing
texture. Three classes excluded, and the instruments that excluded them are in
the tree: the four refusal causes and the two residency counters, each of which
existed and none of which was ever printed.

What is left to look at is narrow and stated: two blocks of the attract sequence
where the card paints near black and the oracle white, with the same geometry,
the same textures and the same decode behind them.

The card's own comparison, in the same log, reads 1,730 frankly different of
which 832 on an edge — against 1,570 and 590 by the neighbour test used in
`win95-corpus.md`. Two metrics, two thresholds, the same shape.

## The by-shade second pass carries the same two errors, and 97 % of its fill hides them

`G_CC_BLEND_SHADEALPHA + G_CC_BLENDI_SHADE` is the biggest second-cycle
contributor in the attract sequence — 25,134 pixels changed by its second cycle,
against 1,511 for the next one — and `pass2_draw_by_shade` decomposes it the way
`pass2_draw` used to:

    A:  dst *= 1 - k          src = shade,  ZERO / ONE_MINUS_SRC_COLOR
    B:  dst += ENV x k        src = ENV x shade,  ONE / ONE

with `k` the vertex colour, per channel. The RDP computes

    out = [ C + (ENV - C) k ] a + dst (1 - a)
        = C a (1 - k)  +  ENV k a  +  dst (1 - a)

so the pair is wrong twice, and in the two ways already met here: the destination
is scaled by `1 - k` where the RDP leaves it at `1 - a`, and the environment term
is not weighted by `a` at all.

**Both vanish when `a = 1`**, and that is why this has been tolerable: the fill
report says this configuration is drawn *97 % opaque* in `CAP0800`. On an opaque
surface `dst (1 - a)` is zero and `ENV k a` is `ENV k`, and the card's pair is
exactly the RDP's. The error lives in the other three per cent.

Fixing the second term is the smaller half: pass B needs `SRC_ALPHA / ONE` with
the alpha unit delivering `t p`, as `pass2_draw` now does. The obstacle is the
alpha *test*, which reads that same value and which `pass2_draw_by_shade`
deliberately leaves as the first pass set it — a note in the file records what
touching it cost on 8 September. Fixing the first term is harder: it wants the
first pass's colour scaled by `1 - k` per channel, and the only factor that could
deliver it is `GR_COMBINE_FACTOR_ONE_MINUS_LOCAL`, whose value the sweep in
`constant_alpha_probe.c` **could not identify** — it read `0x09` as one, with a
local whose red was zero, where `1 - local` is also one. A sweep with a mid-range
local would separate them, and that is one run of the witness.

Expected payoff, stated before the work rather than after: small. The whole
remaining tail of that scene is 1,570 pixels of which 980 are real, and this
configuration's error is confined to the three per cent of its fill that is not
opaque.


## `ONE_MINUS_LOCAL` exists, and it is 0x09

The by-shade note above ends on a question the witness could not answer: whether
any factor delivers `1 - local` per channel, which is what a first pass would
need to carry `1 - shade` when its colour comes from the vertex. The sweep that
read five values as "one" could not have answered it — it used a cyan local,
whose red is zero, and `1 - local` is one there as well.

With the local's red swept instead, and `other` still the white texel:

    factor   L=0  51 102 153 204 255
    0x01       0  49  99 148 198 255    the local, per channel
    0x09     255 198 148  99  49   0    one minus the local, per channel
    0x05, 0x08, 0x0E, 0x0F              one

So the factor exists, at the value Glide's canonical table gives it, and the
by-shade pair is implementable: `SCALE_OTHER / ONE_MINUS_LOCAL / LOCAL_ITERATED /
OTHER_TEXTURE` computes `T x (1 - shade)` in one stage. What remains before
writing it is the other half — the first pass of that class is itself a
decomposition (`prepass_draw`, the `PRIMITIVE` to `TEXEL0` lerp), so the scalar
has to reach a pair rather than a single stage, and the alpha test still reads
the value the second pass would want to change.

The expected payoff has not moved: three per cent of one configuration's fill,
in a scene whose whole remaining tail is 980 real pixels. The measurement is
worth more than the fix it enables — it closes a hole in the table every
generated setup is built on.
## Taking the alpha from the mux instead of the vertex: measured, and worse

`G_CC_MODULATEIDECALA + G_CC_BLENDI_ENV_ALPHA_PRIM2` takes its alpha from
`TEXEL0_ALPHA` and multiplies by `PRIMITIVE_ALPHA`; its generated setup takes
`texel x iterated alpha` instead, because the one constant register was spoken
for by the environment. Where the vertex alpha is low and the primitive's is
full, the card would blend away a sprite the RDP paints solid — which is the
shape of the two blocks left in `CAP0800`.

The register is not always spoken for: this configuration's *colour* reads
`LOCAL_ITERATED` over `OTHER_TEXTURE` and never touches the constant, so its
alpha byte is free for `alpha_scale`. That was written — the setup copied, its
`ac_local` moved to `LOCAL_CONSTANT`, the constant's alpha replaced — and
measured:

    CAP0800   1,570 -> 1,570      unchanged
    CAP0150      36 ->    36      unchanged
    CG0060      753 ->   753      unchanged
    CAP0250     450 ->   562      **worse by 112**

Reverted. Two readings, and the second is the useful one.

The change is inert on `CAP0800` because those draws carry `alpha_scale` 255:
`texel x 255` is `texel`, which is what the vertex path already delivered there,
so the vertex alpha was **not** low at those pixels and the hypothesis about them
is dead. And where the change does act, on the hub, the generated setup's choice
is the better one — the vertex alpha is closer to the mux's answer there than
`alpha_scale` is.

So the two blocks of near black are still unexplained, and the list of what they
are not is now: not a missing draw, not a missing upload, not a missing texture,
not the alpha source, not the second pass's composition order.

## The oracle's probe now records what it refused, and the card's blend factor is not the fault

Two instruments, one afternoon, and both were built because the list of what the
980 pixels *are not* had stopped growing.

**The probe recorded only the draws that painted.** A pixel the card leaves
alone and a pixel the card paints wrongly look the same from the oracle's side
unless the oracle also says which fragments it turned away and why, so
`put_pixel` now logs the scissor, the alpha test and the depth test as entries of
the same log, with the fragment's `z` and the buffer value it was tested
against. The probe also printed `constant_color` and not `env_color`, which for
this configuration is the register the second pass loads - reading
`const=0xFF000000` as "the environment is black" was available and wrong.

At the two pixels that carry the blocks, with both corrections:

    228,167  painted z=-0.000096 buf=1.000000  recipe=3   additive depth=0
             painted z=-0.000113 buf=1.000000  recipe=3   alpha    depth=1
             painted z=-0.002753 buf=1.000000  recipe=10  alpha    depth=1
    304,323  painted z=-0.000106 buf=1.000000  recipe=3   additive depth=0
             painted z=-0.000667 buf=1.000000  recipe=3   opaque   depth=2
             painted z=-0.002881 buf=-0.000667 recipe=10  opaque   depth=2

    env  = 0x96504105  (80,65,5)      prim = 0xFF000000  (0,0,0)

The oracle refuses nothing at either pixel. Its depth is `-oow`
(`software.c:749`), so the two draws that matter at (304,323) sit at w = 1499 and
w = 347 - a separation no sixteen-bit W buffer collapses, and the nearer one is
the later, which is the one that has to win under `LESS` on both sides.

**And the card's value at (304,323) is the oracle's value after the second of the
three draws.** (132,73,0) against (128,72,0), where the oracle's third draw takes
the pixel to (2,7,39). The third draw is recipe 10, and on the card that whole
logical draw is `prepass_draw` - whose first pass writes the *primitive*, which
here is black, opaque, whatever the state's blend - followed by
`pass2_draw_by_shade`. Not one of those four physical passes left a mark.

### The one thing in that path nothing had ever measured

`pass2_draw_by_shade`'s first pass is `dst *= 1 - shade`, and it rests entirely
on `GR_BLEND_ONE_MINUS_SRC_COLOR` being 0x6 in the destination position. The
comment beside the define said, since the day it was written, that the value came
from the canonical table because its four neighbours were right there and that it
had never been seen to work on this card. The four blends `set_state` can express
do not include it, so there was no way to put the question to the hardware at
all.

`dkr_glide_backend_set_blend` is that way, and the witness asks in two draws: a
known D = (204,136,68) laid down opaque from the constant, then a second quad
whose source is the iterated colour S with the blend at `ZERO / factor`, so the
source contributes nothing of itself and is there only to be the factor. D is not
a grey, so a factor reading one channel and broadcasting it could not hide.

    factor   S=0             S=102           S=255
    0x06     (198,134, 66)   (115, 77, 33)   (  0,  0,  0)     1 - S
    0x02     (  0,  0,  0)   ( 74, 52, 24)   (198,134, 66)     S
    0x00     (  0,  0,  0)   (  0,  0,  0)   (  0,  0,  0)     zero

Both factors are what the table says. The hypothesis is dead and the hole in the
table is closed: every enumeration value this backend uses has now been read back
from the card.

So the list of what the two blocks are not gains a sixth entry - not the
per-colour blend factors - and the shape of the remaining question is sharper
than it was: at (304,323) the card keeps nothing at all of a draw the oracle
paints, and its own triangle counter, which nothing printed until today, is the
figure that says whether the card was asked.

## The card gets a probe, and it names the cause on the fourth reading

Six hypotheses about `CAP0800`'s two blocks had been written, measured and
refuted, and every one of them cost a run because the card could not be asked
anything. The oracle has had `dkr_software_probe` since the beginning; the card
had nothing. So it was built, in four passes over an afternoon, each one added
because the reading before it answered half a question.

**One. Which draws changed the pixel.** `dkr_glide_backend_watch(x, y)` and a
single-pixel back-buffer read between draws. At (304,323) the log stopped three
draws in, where the oracle has four.

**Two. Which draws *covered* it.** A draw that leaves a pixel alone either never
reached it or reached it and was discarded, and only the second blames the depth
test, the alpha test or the blend. A point-in-triangle test over the batch, by
the same edge functions a rasteriser uses. It said: batch 537 covers the pixel,
blends opaque, carries no alpha test, runs both its extra passes - and changes
nothing.

**Three. Which of the four physical passes.** A multipass configuration is four
draws on the card and one in the oracle. All four handed the pixel back as they
found it, `pre-A` included - an opaque replacement, which a covered fragment
cannot survive.

**Four. The clip window and the geometry.** Nothing in this backend had ever
recorded what the card was clipping to. It was the whole buffer. The covering
triangle, printed as the card was handed it, put the pixel 0.03 of a pixel inside
its right edge: (304,323) is a fill-rule pixel, and it had been chosen out of a
**stale** card image. The current one - measured, 1,570 tail and 980 real,
reproducing the corpus table exactly - has 221 pixels whose whole eight-fold
neighbourhood is also divergent, and those are the ones worth probing.

### What the probe says at a pixel that is not on an edge

(268,172), where the card paints (41,24,41) and the oracle white:

    batch 558  pre-A    0xFFFFFF -> 0x000000   recipe=10, blend=alpha
               pre-B    0x000000 -> 0x392039   vertex alpha 88, 102, 92 of 255
               shade-A  0x392039 -> 0x311C31
               shade-B  0x311C31 -> 0x291829

`prepass_draw`'s first pass lays the constant down with `ONE / ZERO` - an opaque
replacement - whatever the state's blend says. Here the state blends and the
triangle is about a third opaque, so the RDP keeps almost all of the white. The
card destroys it in the first of four passes and the three after it work on a
destination that is already gone.

**That is the cause of the blocks**, and the objection is written in
`glide_backend.c` a few lines above the code that commits it, against the *other*
prepass shape - `PREPASS_TEXEL_ALONE`, measured four to eighteen times worse and
left out for exactly this reason, with the note "it could be gated on an opaque
first pass, where the two do agree. That was not measured, so it is not written."

### The obvious remedy is worse, measured

Refusing `PRIM_TO_TEXEL` when the state blends, so that the ordinary draw applies
instead:

    CAP0800   1,570 / 980  ->  2,087 / 1,617    worse
    CAP0250     450 /  44  ->    451 /   44     unchanged
    CG0060      753 /  54  ->    753 /   54     unchanged

Reverted. The gate is right about the defect and wrong about the cure: the
ordinary draw for this configuration is `cc = 7/1/1/1`, which computes
`(T - P) x P + P` where the RDP computes `(T - P) x SHADE_ALPHA + P`, and being
approximate in the colour costs more than being wrong about the destination.
Predicted before the run and contradicted by it, which is the point of writing
the prediction down.

So the cure is not to refuse the decomposition but to make it compose under a
blend, the way `pass2_draw` and `prepass_draw_texel_alone` were made to. The RDP
computes

    out = [ C (1 - k) + ENV k ] a + dst (1 - a),    C = P + (T - P) sa

and the card's four passes compute the bracket and then fail to weight it by `a`
at all. `a` is `TEXEL0_ALPHA x PRIMITIVE_ALPHA`, which one alpha stage delivers
as `SCALE_OTHER / FACTOR_LOCAL` over `LOCAL_CONSTANT` and `OTHER_TEXTURE` - the
same shape `prepass_draw_texel_alone` already uses. That derivation is the next
piece of work, and it is a derivation rather than a guess, which is what the two
fixes that worked had and the six that failed did not.

## Three blends instead of four, and 489 of the 980 go

The derivation is in `prepass_shade_exact` and in the commit; the numbers are
these, predicted in writing before the run and matched by it:

    CAP0800   1,570 / 980  ->  967 / 491
    CAP0250     451 /  44  ->  451 /  44      unchanged
    CG0060      753 /  54  ->  753 /  54      unchanged

    whole cycle in three blends: drawn=6  (CAP0800), 0 and 0 elsewhere

**Six draws.** Six, out of the seven hundred and fifty-five the attract sequence
emits, carried 603 divergent pixels and 489 of the 980 that the neighbour test
calls real. That is what the two blocks were: not a class of configuration drawn
wrongly everywhere, but a handful of draws of one configuration that happened to
be alpha-blended where the rest of its fill is opaque - the three per cent this
file has been calling tolerable since 12 September.

The other two scenes are untouched **because the path never fires in them**,
which the counter says outright rather than leaving to inference. That is the
difference between a change that is safe and a change that has not been tried:
the first is a measurement, the second is a hope.

### What is left, and what it would take

`CAP0800` keeps 491 real pixels and the corpus 657, down from 1,146. The first
thing to do with the card probe is to point it at one of the survivors, which
costs a run and no longer costs a hypothesis.

Two pieces of groundwork are named and not done:

* **`GR_COMBINE_OTHER_CONSTANT`** is the last enumeration value this backend
  would use and has never read back from the card. It is what the fourth pass
  needs - `P x (1 - k)`, for a primitive that is not black - and until it is
  measured the exact path is confined to the case where that term is zero. One
  run of the witness settles it, exactly as the two destination blend factors
  were settled.
* **The same treatment for `pass2_draw_by_shade`'s opaque case** is not needed -
  it is exact there - but the *other* multipass classes have not been put through
  this derivation at all, and the card probe now makes each one a measurement
  rather than an argument.
