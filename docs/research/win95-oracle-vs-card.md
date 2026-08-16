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
