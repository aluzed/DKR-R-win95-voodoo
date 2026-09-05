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

A second pass over those surfaces would therefore **roughly double the frame's
fill**. That is the trade E05-S03 named without a figure, and the figure is worse
than "several configurations out of thirty-three" suggests: what matters is the
area they cover, and they cover almost everything.

Two consequences, in opposite directions, and both are real:

- **Correctness.** The card renders those surfaces through `apply_combine`'s four
  single-pass modes. Since 4 September the oracle evaluates the real combiner, so
  the gap is measurable: on the hub it is 11,396 divergent pixels of 307,200, and
  the difference map is a character the card paints black
  (`win95-oracle-combiner.md`).
- **Speed.** The frame already costs 170 ms against a 33 ms budget (E00-S03), of
  which the renderer is 23 ms. Doubling the fill on 90 % of it is not free, and
  it lands on the one resource this card has least of.

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
