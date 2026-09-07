# A second pass, and what it is worth

Implemented and measured on 6 September 2026, on the strength of
`win95-multipass-cost.md`.

## Why it was affordable

`DKR_CC_MULTIPASS` had been a classification for months: everything that was not
`DKR_CC_EXACT` fell through to `apply_combine`'s four single-pass modes, which
compute the RDP's first cycle and drop the second. The reason nobody wrote the
second pass is that the multipass share of the fill is 90 to 100 % on this game's
scenes, and doubling the fill on a Voodoo 2 at 640 × 480 is not a small thing.

That was the wrong figure. The dominant second cycle is a lerp toward the
environment colour **by that colour's alpha**, and an alpha of zero makes it the
identity: the fill where the second cycle actually changes a pixel is 0 % of the
copyright screen, 1.4 % of the race and 6.3 % of the hub.

## What is drawn

The same triangles, once more, with:

| | |
|---|---|
| colour | the constant register, alone (`FUNCTION_LOCAL`, `LOCAL_CONSTANT`) |
| alpha | the texel's times the constant's — the lerp factor, masked by the texture |
| blend | `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA` |
| depth | `LEQUAL`, no write |

The alpha keeps the texture's because that is what makes the second pass cover
exactly what the first covered: a cut-out texel has alpha zero and contributes
nothing, here as there. The depth comparison is `LEQUAL` because the second pass
sits at exactly the depth the first left, and `LESS` — what everything else uses —
would reject every pixel of it.

The first pass's programming is **put back immediately**, through the same path
that applied it. Clearing the tracked state so that the next `set_state`
reprograms is not enough: a batch drawn without an intervening `set_state` — and
nothing in the interface promises there will be one — would render with the second
pass's combiner still loaded, which is a flat constant colour over the geometry.

## Three guards, and one of them was measured wrong first

- **The second cycle would be the identity.** The constant's alpha is zero.
- **The shape is not the one this pass reproduces.** Recognised by the mux fields
  rather than by the entry's name — names are the generator's, fields are the
  hardware's. Four entries in the table have the shape.
- **The entry is not classified `MULTIPASS`.** An `EXACT` entry is one a single
  setup reproduces, second cycle included, so a pass on top would apply that cycle
  twice; a `TWO_TEXELS` entry is answered by chaining the units. No entry today is
  both `EXACT` and the lerp shape, which is why the guard is written down rather
  than left to the table's current contents. The table is generated.

A fourth guard was written, measured, and removed. The composition is exact only
over an **opaque** first pass: the RDP computes cycle 2 and then blends, whereas
this draws cycle 2 as a blend against a frame buffer that already holds cycle 1.
Refusing the blended case drew **2 second passes out of 900 batches** on the hub —
the surfaces whose second cycle does anything there are precisely the
alpha-blended ones, so the correct rule applied to nothing. The gap it admits is
`dst × (1 − a) × k × a`, bounded by a quarter of the lerp factor and zero where
the surface is opaque or invisible. It is drawn and counted separately.

## What it is worth

Measured on the machine, card against the oracle:

| | hub `CAP0250` | race `CAP0400` |
|---|---:|---:|
| before any second pass | 11,396 | 124 |
| opaque first pass only | 10,908 | 124 |
| **and over a blended one** | **9,051** | 124 |

Second passes on the hub: **152 drawn** of 900 batches, 148 of them approximate;
585 skipped as the identity, 162 as a shape this does not reproduce.

The race does not move and should not: all 510 of its two-cycle batches carry an
environment alpha of zero. **The guard that makes the feature cheap is the same
one that makes it do nothing there**, and that is the correct behaviour, not a
disappointment.

`COMPARE.EXE` reports 0 failures throughout — the synthetic scene names no
catalogue entry, so it never takes this path, which is the control.

## Where the rest of the divergence is, attributed

The oracle now writes a **recipe map** — one byte a pixel, the configuration that
painted it last. Crossed with the difference map it stops the guessing.

On the hub, before any second pass:

| configuration | divergent | of painted | share |
|---|---:|---:|---:|
| `G_CC_BLEND_SHADEALPHA` + `G_CC_BLENDI_SHADE` | 6,420 | 6,420 | **100 %** |
| `G_CC_MODULATEIA_PRIM` + `G_CC_BLEND_ENV_ALPHA2` | 5,693 | 44,362 | 13 % |
| `G_CC_MODULATEIDECALA` + `G_CC_BLENDI_ENV_ALPHA_PRIM2` | 356 | 235,717 | 0.15 % |

**The entry that carries 85 % of the fill is wrong on one pixel in seven
hundred.** The entry that is wrong on *every* pixel it paints covers 6,420 of
them — and they sit at x 371–527, y 161–339, which is exactly where the character
the card renders as a black silhouette is.

So the headline defect has a name: `G_CC_BLEND_SHADEALPHA` +
`G_CC_BLENDI_SHADE`, whose second cycle is

    (ENV - COMBINED) * SHADE + COMBINED

a lerp toward the environment colour by the **vertex colour** — per channel, not
by an alpha. That is the one thing the pass above cannot do: Glide's frame-buffer
blender takes scalar alpha factors, and a per-channel factor is not among them in
the positions that would be needed. Named, not solved.

It also explains why the fill figures were the wrong guide. Ranking
configurations by the area they cover put `MODULATEIDECALA` first at 85 %; ranking
them by the area they get *wrong* puts it last of the three. Fill says what a fix
would cost, not what it would buy.

## What it is not worth

**A fifth of the divergence, not the whole of it.** 11,396 to 9,051 is 21 %. The
hub's remaining 9,051 divergent pixels are not explained by this and are not
explained by anything yet; the 162 batches skipped as an unreproduced shape are
the first place to look, and they are other two-cycle forms —
`G_CC_MODULATEIDECALA + G_CC_MODULATEIA_PRIM`,
`G_CC_BLENDI_ENV_ALPHA_A_PRIM + G_CC_MODULATEIA_PRIM2`,
`G_CC_BLEND_SHADEALPHA + G_CC_BLENDI_SHADE`.

Recorded as a fifth, and not as a fix.
