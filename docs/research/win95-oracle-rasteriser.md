# The reference rasteriser, and why it is measured instead of looked at

A report from
[E04-S08](../stories/E04-hle-f3ddkr/E04-S08-reference-software-rasteriser.md),
14 August 2026.

## Its reason for being fits in one sentence

When an image comes out wrong under Glide, one will need to know whether the error
comes from the decoder or from the backend. Without an intermediate oracle, a
wrong pixel can come from ten stages — transformation, clipping, texture decoding,
combiner translation, Glide setting, driver. With a second backend implementing
the **same interface** (E04-S01), the question is settled in one run.

## An oracle one checks by eye is not an oracle

Its entire value lies in the trust placed in it, and "it looks right" does not
transfer. Every check therefore compares a pixel read back against an
**analytically computed** value.

The central check is the one on perspective correction, because it is the one a
naive rasteriser fails silently. A triangle whose vertices have very different
`w` — 1 and 4, the case of a surface seen obliquely — gives, at the middle of the
edge:

```
1/w = 0.5 × 1 + 0.5 × 0.25       = 0.625
s/w = 0       + 0.5 × 0.25       = 0.125
s   = 0.125 / 0.625              = 0.20
```

Measured: **0.2039**. The texture used is a 256-texel intensity ramp where texel
`i` is worth `i`; reading a pixel back therefore gives directly the coordinate
that served to sample it.

### The self-test corrected my reasoning

The first version of this check said "it is not the affine value 0.50". By
deliberately breaking the perspective correction, the self-test showed that
forgetting the division does not give 0.50 but **0.125** — and that the check was
therefore passing on a broken rasteriser.

There are two ways of getting it wrong, and they do not give the same value:

| | `s` obtained |
|---|---:|
| correct — divide `s/w` by `1/w` | **0.20** |
| interpolate `s/w` and forget to divide | 0.125 |
| store `s` instead of `s/w` and interpolate in screen space | 0.50 |

The check aims at 0.20 with a tight tolerance and **rejects the other two by
name**. A loose tolerance would have accepted 0.125.

## The host and Windows 95 render the same pixel

The same code compiled on both sides produces files **identical, byte for byte**.
That is what allows an image produced on the host to be compared against one
produced on the machine — without which the oracle would serve only where it runs.

26 checks, green on the host and on the target.

## What is covered

| | |
|---|---|
| Perspective correction | measured against the analytical value |
| Wrapping | repeat, clamp, mirror, each at a known coordinate |
| Filtering | point and bilinear, the bilinear verified on the average of two neighbouring texels |
| Depth | and **independence from the emission order**, which a depth buffer promises |
| Alpha test | on either side of the threshold |
| Blending | half alpha over black |
| Scissor window | cuts on the left, lets through, cuts on the right |
| File output | 24-bit BMP, header and dimensions read back |

The bilinear filter's half-texel deserves a mention: without it the image is
shifted by half a texel width, which does not show on a test pattern and shows
perfectly on an image comparison — exactly the kind of deviation this backend
exists to arbitrate.

## What is not done, and why

- **The RDP's combiner in its complete form.** The interface's four modes are
  implemented faithfully, without a hardware constraint; but the RDP's combiner
  has two stages with four inputs, and the inventory of those DKR really uses is
  **E04-S06**'s work, still `TODO`.
- **An image from the game.** The criterion "title screen, menu, one race"
  requires the game to run, hence the ROM. The rasteriser is ready to receive it.

It is allowed to be slow, and not allowed to be complicated: an optimised
rasteriser is a rasteriser whose own correctness must in turn be checked, and the
oracle disappears. Floats everywhere, no tiles, no precomputed table, one loop per
pixel of the bounding box.
