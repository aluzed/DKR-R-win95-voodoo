# What multipass would cost, measured on real frames

Measured on 5 September 2026 on E09-S02's corpus, by the oracle. E05-S03 has
carried this criterion unchecked since it was written:

> *The multipass fill cost is measured — not yet done. It needs a race, not the
> menu, and the port does not render one legibly yet.*

It renders one legibly, the corpus holds a frozen capture of it, and this is the
figure.

## The measurement

The oracle counts, for every pixel it writes, the category of the combiner
configuration that wrote it. **Writes and not distinct pixels**: a pixel written
five times costs five times the fill, and fill is what limits a Voodoo 2 at
640 × 480. A count of distinct pixels would read like a coverage figure and be
the wrong number for the only question it is asked.

| capture | scene | total writes | exact | multipass | approximate |
|---|---|---:|---:|---:|---:|
| `CAP0050` | the Nintendo 64 logo | 822,234 | 61.8 % | **38.2 %** | — |
| `CAP0150` | the copyright screen | 820,758 | 61.9 % | **37.4 %** | 0.7 % |
| `CAP0250` | the hub, Pipsy on the beach | 530,782 | 0 % | **99.99 %** | — |
| `CAP0400` | Ancient Lake, Bumper racing | 615,192 | 0 % | **89.5 %** | 10.5 % |

The frame already carries 1.7 to 2.7 writes per pixel of overdraw before any
second pass is added.

## What it means

**On the game's own scenes, nearly all the fill goes through a configuration the
card cannot express in one pass.** The two screens that are mostly a background —
the logo and the copyright — are around 38 %; the hub and the race, which are the
game, are 90 % and 100 %.

A second pass over *all* of those surfaces would therefore roughly double the
frame's fill. That is the trade E05-S03 named without a figure, and taken alone
the figure looks worse than "several configurations out of thirty-three"
suggests: what matters is the area they cover, and they cover almost everything.

**Taken alone it is also the wrong figure**, and the section below gives the
right one.

Two consequences, in opposite directions, and both are real:

- **Correctness.** The card renders those surfaces through `apply_combine`'s four
  single-pass modes. Since 4 September the oracle evaluates the real combiner, so
  the gap is measurable: on the hub it is 11,396 divergent pixels of 307,200, and
  the difference map is a character the card paints black
  (`win95-oracle-combiner.md`).
- **Speed.** The frame already costs 170 ms against a 33 ms budget (E00-S03), of
  which the renderer is 23 ms. Doubling the fill on 90 % of it is not free, and
  it lands on the one resource this card has least of.

## One configuration is most of it

The same counter, broken down by catalogue entry:

| scene | share of all fill | configuration |
|---|---:|---|
| hub | **85.1 %** | `G_CC_MODULATEIDECALA` + `G_CC_BLENDI_ENV_ALPHA_PRIM2` |
| race | **82.6 %** | the same |
| intro | **37.9 %** | the same |
| race | 10.5 % | `G_CC_BLENDT_ENV_ALPHA_A_TxP` (approximate, one cycle) |
| hub | 11.9 % | `G_CC_MODULATEIA_PRIM` + `G_CC_BLEND_ENV_ALPHA2` |
| race | 6.4 % | `G_CC_MODULATERGBA` + `G_CC_BLENDI_ENV_ALPHA_PRIM2` |

**One entry carries the whole problem.** "Ninety per cent of the frame is
multipass" is a wall; "one configuration is 85 % of it" is a task.

And that configuration's second cycle is a shape worth reading:

    cycle 1  (TEXEL0 - 0) * SHADE + 0            texel modulated by the vertex colour
    cycle 2  (ENV - COMBINED) * ENV_ALPHA + COMBINED

Cycle 2 is a **lerp from the first cycle's result toward the environment colour
by the environment's alpha**. The catalogue's note is right that one Glide stage
cannot do both — the stage that multiplies the texel by the iterated colour is the
same one that would have to blend toward the constant.

But the second pass it needs is **untextured**: draw the same triangles flat in
the environment colour, alpha `ENV_ALPHA`, through the frame-buffer blender. No
texel fetch, no TMU traffic, no state beyond a constant. That is the cheap end of
what "double the fill" can mean on this card, and it is the case that matters
most.

Not implemented here. Measured, and the shape of the work named.

## And the share that would actually cost a pass is far smaller

The share of the frame painted by a two-cycle configuration is **not** the share
the card gets wrong, and the corpus says so loudly. The race and the hub are both
about 85 % `G_CC_MODULATEIDECALA + G_CC_BLENDI_ENV_ALPHA_PRIM2`, and the card
diverges from the oracle by **403 per million** on one and **37,096** on the
other.

The reason is in the shape of the second cycle. It is a lerp toward the
environment colour **by the environment's alpha**, so an alpha of zero makes it
the identity. Whether it does anything is a property of the constants at draw
time, not of the configuration.

Measured — the oracle evaluates cycle 1, feeds it in, evaluates cycle 2, and
counts the pixels where the two differ by more than one level in any channel:

| capture | total fill | two-cycle | **cycle 2 changes the pixel** | share of all fill |
|---|---:|---:|---:|---:|
| `CAP0050` intro | 822,234 | 314,072 | 633 | **0.08 %** |
| `CAP0150` copyright | 820,758 | 307,200 | **0** | **0 %** |
| `CAP0250` hub | 530,782 | 530,737 | 33,628 | **6.3 %** |
| `CAP0400` race | 615,192 | 550,792 | 8,872 | **1.4 %** |

**A second pass that is skipped when it would be the identity costs at most 6.3 %
more fill on the worst scene in this corpus, and nothing at all on one of them.**
Not ninety per cent. The figure that made multipass look prohibitive was the
wrong figure — the right one is two orders of magnitude smaller, and the test
that separates them is a comparison of two constants at draw time.

The counters share a denominator, and that took care: shading a pixel is not
writing it, since the scissor, the depth test and the alpha cutout sit between
the two. Counted where the write happens, `two-cycle` matches the `multipass`
category exactly — which is the check that the two counters mean what their
names say.

## What this does not say

It does not say multipass is the wrong choice. It says the choice is between a
character rendered black and a renderer that costs more fill, and that the second
term is now a number rather than an intuition.

Nor does it say the doubling is unavoidable. The catalogue distinguishes
`DKR_CC_MULTIPASS` from `DKR_CC_EXACT` by whether **one** Glide setup reproduces
the configuration; a two-cycle combiner whose second cycle is a scaling or an
identity already folds onto the first, and E05-S03 records two such forms. How
many of the 90 % are irreducible has not been asked, and asking it is cheaper
than implementing the general case.

## How it was measured

`build/render-tools/replay <capture> out.bmp` prints the table. The counter lives
in the oracle because the oracle is the one that knows what a pixel should have
been; the card cannot report on a configuration it cannot express.
