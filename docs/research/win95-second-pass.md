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
| and over a blended one | 9,051 | 124 |
| and the per-channel form | 8,701 | 124 |
| **and the catalogue's first-cycle setups for `MULTIPASS`** | **8,657** | 124 |

Twenty-four per cent over five changes, of which the last two account for four.
The reason they account for so little is the section below: they address the
second cycle of a configuration whose **first** cycle is what is wrong.

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

On the hub, before and after the second pass:

| configuration | painted | divergent before | after |
|---|---:|---:|---:|
| `G_CC_BLEND_SHADEALPHA` + `G_CC_BLENDI_SHADE` | 6,420 | 6,420 (**100 %**) | 6,420 |
| `G_CC_MODULATEIA_PRIM` + `G_CC_BLEND_ENV_ALPHA2` | 44,362 | 5,693 | **3,604** |
| `G_CC_MODULATEIDECALA` + `G_CC_BLENDI_ENV_ALPHA_PRIM2` | 235,717 | 356 | 357 |

**That is the second pass verified rather than merely measured.** It improved the
one entry whose shape it reproduces, by 37 %; it left untouched the entry whose
shape it refuses; and it disturbed nothing else. A feature that improved a total
would have been consistent with a dozen stories, including several bad ones.

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

## The black character is the **first** cycle, not the second

Three attempts were spent on that configuration's second cycle, and the third
measurement is what named the mistake. At one pixel of the character —
(417,161), where the oracle puts `0xFF0000` and the card puts `0x000000`:

    RDP cycle 1   (TEXEL0 - PRIM) * SHADE_ALPHA + PRIM
    the shorthand  texel x shade                        (TEXTURE_SHADE_ALPHA)

`PRIM` is black there and the shade's **colour** is near zero while its **alpha**
is one. So the RDP's first cycle is the texel — a red cap — and the four-mode
shorthand is texel × 0. The second cycle then lerps by the shade colour, which is
near zero, so it changes almost nothing: the pixel the oracle draws red is red
*before* cycle 2 ever runs.

Two days on the second cycle of a configuration whose first cycle was the defect.
The recipe map is what finally said so, by attributing the divergence to
`G_CC_BLEND_SHADEALPHA` and holding it there through three changes that each
moved the total by a few hundred pixels.

**The catalogue's own setup does not express it either.** Recipe 10's generated
setup is `(other − local) × factor + local` with other = texture and local = the
constant, which is the right shape — but the factor it can name is the local's,
and the RDP's factor is the **iterated alpha**. Glide offers a factor from the
local or from the other, and `PRIM` and `SHADE_ALPHA` cannot both be the local.
Irreducible in one pass, like the second cycle and for the same reason.

It decomposes into passes that use only factors this file has seen work, and it
is written:

    A:  colour = PRIM,   blend ONE / ZERO                  -> dst = PRIM
    B:  colour = TEXEL0, blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA
                         with the source alpha = the iterated alpha
                                                           -> TEXEL0*sa + PRIM*(1-sa)

A **pre**-pass, not a post-pass: it *replaces* the ordinary draw instead of
following it, and the second-cycle pass then composes on top of its result as it
would over any other first pass.

One guard: **not while a cutout is in force.** Pass B has to put the iterated
alpha in the alpha combiner, because that alpha is its blend factor — and the
alpha test reads the same output, so a state cutting holes by alpha would have
them cut by the vertex alpha instead of the texel's. Refused and counted; the
ordinary draw then applies, wrong in the old way rather than wrong in a new one.
On the hub the refusal costs nothing: **0 of 162** batches carry an alpha test.

### What it was worth

    hub CAP0250, card against oracle          divergent   recipe 10
    before the pre-pass                           8,657   6,133 of 6,420
    with it                                       1,629      67 of 6,420

**Eighty-one per cent of what remained, and the configuration it targets goes
from wrong on every pixel to wrong on one in ninety-six.** From the start of the
day's work — 11,396 — that is 86 % of the divergence gone. `COMPARE.EXE` reports
0 failures, and the corpus still replays to 0 divergent pixels, the oracle being
untouched by any of this.

The character is a red cap and a blue plane on the card now.

## The whole corpus, before and after

One sweep of the four scenes through both backends, with every change of this
work in place:

| capture | scene | before | after | second passes | pre-passes |
|---|---|---:|---:|---:|---:|
| `CAP0050` | the Nintendo 64 logo | 829 | **20** | 161 | 157 |
| `CAP0150` | the copyright screen | 801 | 801 | 0 | 0 |
| `CAP0250` | the hub | 11,396 | **1,629** | 314 | 162 |
| `CAP0400` | Ancient Lake | 124 | 124 | 0 | 0 |

The intro falls by 98 %, the hub by 86 %, and **the two scenes that draw neither
a second pass nor a pre-pass are unchanged to the pixel**. That is the shape a
correct change makes: it moves what it addresses and leaves the rest exactly
where it was. `COMPARE.EXE` reports 0 failures and the corpus still replays to 0
divergent pixels.

## The copyright screen's 801, attributed

The recipe map, once it recorded the last configuration that **changed** a pixel
rather than the last that wrote one:

| configuration | divergent | of painted |
|---|---:|---:|
| `G_CC_BLENDT_ENV_ALPHA_A_TxP` | **784** | 1,208 |
| `G_CC_MODULATEIDECALA` + `G_CC_BLENDI_ENV_ALPHA_PRIM2` | 51 | 305,992 |

The 801 are the **copyright text**: 784 of its 1,208 pixels, white in the oracle
and near-black on the card. The sky, which is 99.6 % of the screen, is wrong on
51 pixels.

That configuration is

    (ENVIRONMENT - TEXEL0) * ENVIRONMENT_ALPHA + TEXEL0

the texel tinted toward the constant by that constant's alpha — catalogued
**approximate**, with the note *"no Glide factor delivers it"*.

### And the obvious fix was written, measured, and thrown away

It decomposes like everything else here: the texel, then the constant over it by
the constant's alpha. Measured:

| | before | after |
|---|---:|---:|
| copyright screen | 801 | **3,828** |
| the race | 124 | **2,313** |

Four to eighteen times worse. The fault is the first of the two passes: it lays
the texel down **opaque**, and these states blend. Over an alpha-blended
background the pair computes `(T·a + dst(1−a))(1−k) + E·k` where the RDP computes
`(T(1−k) + E·k)·a + dst(1−a)`, and the difference is the background showing
through by the wrong amount.

Reverted. It could be gated on an opaque first pass, where the two do agree — and
that is now measured rather than guessed at.

### The rule, and it is measurable before anything is written

The oracle counts, per configuration, the share of its fill drawn with **no
blending**. That share is exactly the share a two-pass decomposition can be
exact on:

| configuration | fill | opaque | decomposition |
|---|---:|---:|---|
| `G_CC_BLEND_SHADEALPHA` + `G_CC_BLENDI_SHADE` | 16,007 | **100 %** | written, 6,420 wrong → 67 |
| `G_CC_BLENDT_ENV_ALPHA_A_TxP` (copyright) | 5,396 | **0 %** | written, reverted |
| `G_CC_BLENDT_ENV_ALPHA_A_TxP` (race) | 64,400 | **0 %** | — |
| `G_CC_MODULATEIDECALA` + `G_CC_BLENDI_ENV_ALPHA_PRIM2` | 451,760 | 50 % | second pass, half exact |
| `G_CC_MODULATEIA_PRIM` + `G_CC_BLEND_ENV_ALPHA2` | 62,970 | 40 % | second pass, part exact |

**The copyright text is 0 % opaque on both scenes it appears in.** A gated
version of that pre-pass would apply to nothing at all, so it is not written —
and that is a measurement now, not a hunch.

It also explains the two results side by side. The pre-pass that worked fires on
a configuration whose fill is **100 %** opaque; the one that failed fires on one
that is **0 %**. The rule is not "decompose lerps" but "decompose lerps over
opaque first passes", and the oracle can say which those are before a line is
written or a minute spent on the machine.

### And a route that is not a decomposition at all

The two-pass route needs an opaque first pass and this configuration has none, so
the answer was not another pass. Glide's `BLEND` function already has the *shape*
— `(other − local) × factor + local`, and the RDP's form rearranges onto it as
`(T − ENV) × (1 − k) + ENV`. What it lacks is a factor delivering a constant
register's alpha.

`gen_combiner_table.py` had recorded the way round and marked it untested: the
environment and its alpha are constants the CPU knows, so they can be carried in
the **vertex** instead, where a factor reading the local's alpha fetches `k` from
the iterated alpha. It is free for this configuration, which names TEXEL0 and
ENVIRONMENT and nothing else — overwriting the shade costs nothing the RDP was
using.

| | before | after |
|---|---:|---:|
| copyright screen | 801 | **17** |
| the race | 124 | 124 |

One pass, no extra fill, and nothing assumed about what lies underneath.

**The mechanism is measured by difference, not by sweep, and that is stated
rather than glossed.** `combine_enum_probe.c` sweeps the sixteen factor values and
finds none that delivers an alpha — but both of its sweeps drive `other` from the
constant register with no texture bound, and this drives it from the texture.
Replacing the factor with a plain `FACTOR_ONE`, which would draw the texture
alone, puts the screen back at **801**. So something is scaling the texel toward
the constant, and `1 − local_alpha` is the only candidate on offer. Extending the
sweep to a textured `other` is what would settle it, and it has not been done.

That caveat is worth its lines: this project has already classified a whole
family of configurations as exact on the strength of an enumeration value written
from memory, and the measurement harness caught it out by 140 units.

## What it is not worth

**A fifth of the divergence, not the whole of it.** 11,396 to 9,051 is 21 %. The
hub's remaining 9,051 divergent pixels are not explained by this and are not
explained by anything yet; the 162 batches skipped as an unreproduced shape are
the first place to look, and they are other two-cycle forms —
`G_CC_MODULATEIDECALA + G_CC_MODULATEIA_PRIM`,
`G_CC_BLENDI_ENV_ALPHA_A_PRIM + G_CC_MODULATEIA_PRIM2`,
`G_CC_BLEND_SHADEALPHA + G_CC_BLENDI_SHADE`.

Recorded as a fifth, and not as a fix.
