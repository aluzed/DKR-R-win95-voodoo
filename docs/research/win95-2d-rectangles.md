# 2D rectangles and the interface

Measured on 14 August 2026 on the test machine, by
`tools/win95/witnesses/rect_probe.c`.

## `TextureOffset` does not do what our decoder believed

The ticket asked that this command's behaviour be "recorded rather than assumed".
That is what revealed the error.

Our decoder read `w1` as **two sixteen-bit coordinate offsets**:
`texture_offset_s = (w1 >> 16)`, `texture_offset_t = (w1 & 0xFFFF)`. The
neighbouring port, which runs, makes something else entirely of it:

    data.texture_offset = w1 & 0x00FFFFFF;   // an RDRAM address
    data.texture_shift  = 0;
    data.texture_count  = 0;

It is an **addressing base for texture loading**, and the command resets the shift
and the count.

The error would not have shown at once. An address base read as two offsets
produces absurd coordinates on the surfaces concerned — hence a displaced pattern,
not an absence — and one would have looked at the texture decoding, which would
have had nothing to do with it.

## The half-texel offset: zero, and the safe band

The ticket insists: this setting "is determined by experiment, not by reasoning".
A grid of 64 texels over 64 pixels, a one-texel white line every four columns,
point sampling:

| Offset | Columns in place out of 64 |
|---|---|
| −0.50 | 64 |
| −0.25 | 64 |
| **0.00** | **64** |
| +0.25 | 64 |
| +0.50 | 33 |
| +0.75 | 33 |
| +1.00 | 33 |

**No correction is necessary.** With `s` running from 0 to the width over as many
pixels, the sampling lands correctly. The break is clean between 0.25 and 0.50,
which is exactly where the sampled point changes texel: the safe band is therefore
centred on zero.

The result matters less than the band: knowing that a quarter of a texel of margin
remains on each side says that a small rounding error elsewhere in the chain will
not tip the interface over.

### A metric that cannot fail measures nothing

The first version counted the "clean" columns — neither grey nor intermediate —
and returned 64 out of 64 for **every** offset. Under point sampling there is
never an intermediate value, only displaced lines: the metric could not fail, so
it discriminated nothing.

It is the third time in this port that a check passes without establishing
anything: after the counter of painted pixels on a non-black background, and the
TMU consistency check on a white screen. The pattern is always the same — **the
measured result is not distinguishable from the absence of a result**.

Corrected, the metric compares against the expected grid: texel `x` must fall on
pixel `x`.

## The seams

Four adjacent rectangles, edge to edge, of different colours:

    background pixels on the line of four rectangles: 0 out of 200

No seam, no overlap. A negative control further verifies that the four colours
really are distinct — without which a single rectangle covering everything would
pass the first check.

That is consistent with the fill rule already measured in E05-S01: Glide's window
is inclusive on the left and exclusive on the right, and its triangles count every
pixel once.

## The HUD in split screen

A full-screen rectangle, restricted by E04-S05's scissor window:

| Case | Painted | Expected |
|---|---|---|
| 2 players, top | 153600 | 153600 |
| 2 players, bottom | 153600 | 153600 |
| 4 players, top-left | 76800 | 76800 |
| 4 players, bottom-right | 76800 | 76800 |

Exact in all four cases, to the pixel.

## What remains open

- **The game's text**, composed of small assembled textures, which is the most
  demanding trial of positioning. Requires the ROM.
- **The five reference screens** compared pixel by pixel. The ticket rightly notes
  that, the interface being static, the comparison there can be exact rather than
  tolerant — "a rare opportunity in this project". Requires the ROM.
