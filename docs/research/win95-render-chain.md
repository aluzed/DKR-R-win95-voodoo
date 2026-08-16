# The render chain, assembled on a synthetic scene

A report from 14 August 2026. Five modules fit together for the first time:

```
f3ddkr     reads the display list, validates the ranges, keeps the 32-vertex cache
transform  applies the model-view-projection matrix (E04-S03)
clip       clips at the near plane, culls back faces (E04-S05)
backend    E04-S01's interface
software   the reference rasteriser, which writes the image (E04-S08)
```

What is established is **not** that the rendering is right — the game will be
needed for that — but that the chain is **continuous**: a command written into
RDRAM comes out as pixels, and each stage hands its neighbour what that neighbour
expects.

## The scene

Built by hand in a fake RDRAM: that is what makes the trial possible without a
ROM, and what makes it conclusive — one knows the answer in advance, so one checks
it pixel by pixel rather than by eye.

A Gouraud square facing the camera, and **a triangle that crosses the near
plane**. Without the latter, the chain would be only half exercised: the clipping
would never be called upon.

```text
triangles requested: 3, emitted: 4
clipped in two: 1, discarded: 0
```

Four emitted for three read: the straddling triangle did produce a quadrilateral,
hence two triangles.

## A graphics-porting trap, met along the way

The scene's first version used a projection where `w = z` **and** `z_clip = z`.
The division then gives `z/w = 1.0` for every vertex, that is exactly the value the
depth buffer is cleared to. The test fails everywhere, and **nothing is painted**.

The symptom is the one one dreads: an empty screen while **every stage declares
itself satisfied** — four triangles emitted, no rejection, no error. It was the
`emitted` counter that allowed the matter to be settled, by showing that the
problem was downstream of the decoder. Without it, the suspicion would have fallen
on the display list.

That is that counter's reason for being, and it is worth keeping.

## The oracle is exact across platforms — except where it cannot be

The same code compiled for the host and for Windows 95, on the same scene:

| | |
|---|---:|
| differing pixels | 1,986 out of 76,800 — **2.6 %** |
| maximum per-component deviation | 251 out of 255 |

A deviation of 251 is not a rounding: those are pixels of an entirely different
colour. Their distribution says where they come from:

| Zone | Differing pixels |
|---|---:|
| the square, well conditioned (`y < 115`) | **0** |
| the clipped triangle's band (`115 ≤ y < 160`) | **1,970** |
| the rest | 16 |

**Zero differences where the geometry is sound.** All the differences are
concentrated on the clipped triangle.

### Why

A vertex created by the clipping comes out with `w` equal to the near plane's
margin — `0.0001`. Projected, that gives:

```text
x = 16 000 160  y = 120  z = -250 000  1/w = 10 000
```

Sixteen million pixels. The rasteriser's edge functions then subtract numbers of
that order to obtain results on the order of unity: that is **catastrophic
cancellation**, and the result depends entirely on the intermediates' precision.

And that precision differs between the two targets: the host computes in SSE at 32
bits, the target in x87 at **80 bits** (`-mfpmath=387`). The same operations, in
the same order, therefore do not return the same pixels — but only where the
cancellation bites.

### The remedy, applied and measured

Rather than choose a margin by guesswork, the clipper now bounds the coordinates
**by construction**: it also clips against a **guard band**, in homogeneous space —
four lateral planes at four half-screens from the centre, handled by the same loop
as the near plane.

This is not the complete six-plane clipping that E04-S05 rightly rules out. The
band is far wider than the screen, so almost no triangle crosses it, and those that
stay entirely inside leave through a **short circuit** without a single edge being
computed.

| | Differing pixels | Share |
|---|---:|---:|
| near plane alone | 1,986 | 2.59 % |
| near plane + guard band | **479** | **0.62 %** |

### What remains, and why it cannot disappear that way

The 479 remaining pixels are **all on an edge** of the reference image — 479 out of
479. Their deviation is large (up to 250) because an edge separates two very
different colours: a shift of a single pixel is enough to exchange magenta for dark
blue.

That is no longer catastrophic cancellation but the ordinary divergence between
two computing units: the host evaluates the edge functions in SSE at 32 bits, the
target in x87 at 80 bits, and the coverage tips over on the pixels the edge grazes.

**Consequence for E09-S02:** the comparison must tolerate one pixel of deviation
along the edges. That is not an admission — it is the only form of comparison that
has any meaning between two rasterisers, and it stays severe: the other 76,321
pixels are identical to the bit.

There is another route, and it is not taken: forcing the x87's precision to a
24-bit mantissa (`_controlfp`) would make the target identical to the host. But
that setting holds for **the whole process**, including the game's recompiled code,
whose computations expect double precision. Trading the game's correctness for the
convenience of a comparison would be a bad bargain.

The depth, for its part, is now clamped to `[0,1]` at projection time. A value
outside that interval has no meaning for the buffer and won the test everywhere, in
a dither pattern that suggested a rasterisation defect rather than a depth defect.
It took projecting a vertex by hand to see it.
