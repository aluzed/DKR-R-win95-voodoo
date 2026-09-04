# The alpha the RDP asks for, and the one the card was given

Measured on 4 September 2026 with E09-S02's harness, on the capture of display
list 400 of a race on Ancient Lake.

## The finding

The first real frame the harness compared showed one divergence of substance:
2416 pixels of the character's nameplate outline, blue in the oracle and black on
the card. `docs/research/win95-oracle-vs-card-capture.md` records the
confrontation; this records what was underneath it.

The oracle's pixel probe — armed at one pixel, recording every draw that writes
it — gave the answer in one run:

    probe (318,437): 7 draw(s):
       1  0x3163DE -> 0xFEDF00  TEX*SHADE+A const=0x00FFFFFF ascale=255 ...
       2  0xFEDF00 -> 0xFEDF00  TEX*SHADE+A const=0x00FFFFFF ascale=255 ...
       3  0xFEDF00 -> 0xCBB22C  TEX*CONST   const=0xFF0000FF ascale=51  ...
       4  0xCBB22C -> 0x796A73  TEX*CONST   const=0xFF00FFFF ascale=102 ...
       5  0x796A73 -> 0x302A2D  TEX*CONST   const=0xFF00FF00 ascale=153 ...
       6  0x302A2D -> 0x090808  TEX*CONST   const=0xFFFFFF00 ascale=204 ...
       7  0x090808 -> 0x0000DE  TEX*CONST   const=0x00FFFFFF ascale=255 ...

**Five passes of the same glyph texture, at the same coordinates, differing only
by a constant colour** — and, it turned out, by an alpha that climbs 51, 102,
153, 204, 255. Exact fifths. The game builds the nameplate by laying the same
image down five times at a rising opacity.

## What each side did with the alpha, and why both were guessing

The passes go through `DKR_COMBINE_TEXTURE_CONSTANT`, texel modulated by the
constant register. What neither backend had asked is what the RDP's **alpha** mux
does — it is a separate mux from the colour one, and nothing in
`dkr_rdp_to_render_state` had ever read it for this mode.

So each side decided alone:

| | alpha of the result |
|---|---|
| the software oracle | the texel's, untouched |
| the Glide backend | the texel's, times the alpha of `constant_color` |

The second is wrong twice over.

**It scales when the RDP may not have asked for any scaling.** And when the RDP
does ask, it may name the **other** register. The configuration this game's text
uses is `G_CC_BLENDT_ENV_ALPHA_A_TxP`: its colour comes from ENVIRONMENT and its
alpha stage is `(TEXEL0 − 0) × PRIMITIVE + 0`. `constant_color` carries the
register the colour side named, which is ENVIRONMENT — so multiplying by its
alpha was not an approximation of the primitive's alpha. It was an unrelated
number.

The fifth pass carries an environment alpha of **zero**. `GR_COMBINE_FACTOR_LOCAL`
multiplied by it, the pass that paints the letters contributed nothing, and the
nameplate lost its outline.

## The fix

`dkr_render_state` gains `alpha_scale`, the 0..255 factor the RDP's alpha mux
applies to the texel's alpha, 255 meaning none. It occupies the byte that was
`pad_` — explicit padding kept there to hold the block free of implicit padding —
so the layout is unchanged and `backend_layout_check.c` still passes.

It is derived in `dkr_rdp_to_render_state` from the alpha stages, for the shape
`(TEXEL0 − 0) × C + 0` with `C` a constant register, plus the degenerate shapes
that yield the texel's alpha. **Narrow on purpose**: that is what this game's text
uses and what one byte can carry, and anything else keeps 255, which is what both
backends already did.

- The oracle multiplies the texel's alpha by it.
- The Glide backend puts it in the alpha byte of the value it hands
  `grConstantColorValue`. `GR_COMBINE_LOCAL_CONSTANT` reads the constant's RGB for
  the colour and its alpha for the alpha, so one register serves both correctly,
  and `GR_COMBINE_FACTOR_LOCAL` becomes the right factor instead of the wrong
  register's.

**Not changed: the E05-S03 catalogue path.** It programs its alpha combiner from
the table's own `ac_*`, generated from the real alpha stages. It shares the
register question — a catalogued entry whose alpha names the register the colour
side did not still receives the colour register's alpha — but no measurement has
reached that case, and substituting `alpha_scale` there would be wrong for every
entry whose alpha shape this byte cannot carry. Left alone and written down.

## What it measured

The oracle's image is **byte-identical before and after**, on this capture: the
intermediate passes now blend differently, and the last one, at full alpha,
overwrites them everywhere. So the change is provably neutral on the reference,
which is what one wants of a change made to fix the other side.

On the card, against the target's own oracle:

| | before | after |
|---|---|---|
| frankly different, off-edge | 5071 | **95** |
| per million | 16507 | **309** |
| forgiven as an edge | 1682 | 586 |
| painted surface | 304825 | **306365** (oracle: 306367) |

And the control still holds: the software oracle on the development machine and
on the Windows 95 machine still differ by **0** pixels out of 307,200.

## What is left, and what it is

Of the 95 remaining off-edge pixels, **80 fall in the 25–49 band** — just past the
threshold of 24, and of the shape `0x84FFFF` against `0x6BFFFF`: a red channel
differing by 25 in the sky's gradient. That is the Gouraud iterator, not a defect
of substance.

**Twelve exceed a gap of 100**, isolated single pixels scattered over the
character model between x 230–382 and y 218–320. Single pixels with large gaps
are the signature of a thin feature the neighbour rule cannot excuse, and they are
recorded here rather than explained: nothing has measured them yet.

## The instrument that answered this

`dkr_software_probe(x, y)`. The oracle rasterises, so at the moment it writes a
pixel it holds the state that asked for it; recording that costs one comparison
per write. The first version kept only the last writer, and the first pixel it was
pointed at had been painted **seven** times — a record of the last one could not
have shown the ramp, which is the whole answer.

The first question about a divergent pixel is always "what drew that". It now
takes a run instead of a morning.
