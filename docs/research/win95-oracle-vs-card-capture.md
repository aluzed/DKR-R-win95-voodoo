# The oracle confronted with the card, on a real frame

Measured on 3 September 2026 on the test machine, by `REPLAY.EXE` on a capture
of display list 400 of a race on Ancient Lake, taken with `DKR_CAPTURE_LIST`.

`win95-oracle-vs-card.md` established the same confrontation on a **synthetic**
scene: four triangles, one texture, no multi-texturing, no multipass, no cache
under pressure. That was what the harness could reach without a ROM. This is the
first time the two backends have been given a frame the game actually drew —
1539 commands, 943 triangles, 95 textures, and the multipass path that carries
79.7 % of this game's triangles.

## What the confrontation established

    oracle   cmd=1539 tri=943 emitted=510 rejects=0 textures=95
    card     cmd=1539 tri=943 emitted=510 rejects=0 textures=68

**The chain is deterministic across backends.** Commands, triangles and emitted
triangles agree exactly. That is checked before the images are, and the order is
not cosmetic: an image difference between two chains that did not decode the same
geometry says nothing about rendering.

The texture counts differ, and that is the residency cache doing its work — 95
conversions on the oracle, which has no cache, against 68 on the card, which
looks the texture up before converting it. `textures_loaded` counts conversions,
not textures used.

## The two oracles agree, on two machines and two compilers

The same capture, replayed through the software rasteriser on the development
machine (x86-64, SSE) and on the Windows 95 machine (i686, x87, `-mno-sse`):

    frankly different pixels: 0 out of 307200
    worst per-channel gap over the whole image: 8

Zero. The worst gap of 8 is the metric's own floor — the reference side is
quantised to 565 and the candidate side is not, so a comparison of an image with
itself already reads 7.

This is the result the whole harness rests on, and it had not been established
before. It means **the image computed on the development machine is a valid
reference for the target**: a difference measured there belongs to the renderer
and not to the machine it ran on.

## Where the card disagrees: the character's nameplate loses its blue

    painted surface: oracle 306367, card 304825
    frankly different pixels: 5071 out of 307200 (16507 per million)
    of which on an edge, counted separately: 1682
    worst per-channel gap: 255
    worst off-edge: 255 at (307,218)  oracle 0xCE7D00  card 0xFFFFFF

The scene is otherwise the same picture: same canyon, same sky, same track, same
character. The divergence has one dominant location, and the difference map names
it without ambiguity — the **BUMPER** nameplate. The letter bodies match; what
differs is the **outline around them**, blue in the oracle and black on the card.

Counted directly:

> Of the **2416** pixels the oracle renders strongly blue (blue > 180, red and
> green < 110), the card renders **2416** with blue below 40. Not one keeps it.
> Red and green come through unchanged: `0x2129E7` becomes `0x212800`. Those 2416
> pixels are the glyph outline; the yellow-to-green bodies inside it agree.

So it is not a colour shift, a lighting difference or a texture mix-up. **The
blue channel is zeroed**, and only there. That is a defect in the Glide backend —
the decoder is out of the question, since both backends received the same 510
triangles from the same object code.

It is worth recording what this replaces. On 28 August the same nameplate was
observed doubled, and on 1 September it was observed clean; the difference could
not be attributed, because the two observations came from two runs of an animated
scene. `win95-game-render.md` says so explicitly rather than claiming the second
texture unit had fixed it. That question is now answerable by construction, and
the same object has produced a different, precisely located defect instead.

## Two defects found on the way, both in the instruments

### A backend entry that was never assigned, and a null guard that could not help

The first attempt did not produce an image at all: `REPLAY.EXE` faulted inside
`dkr_f3d_run`, with `EIP` inside `.bss` and `EAX` equal to the backend's `self`.

    REPLAY a causé une défaillance de page dans le module REPLAY.EXE à 0137:00463860
    EAX=00463860 ... EIP=00463860
    Octets à CS:EIP : 80 02 00 00 e0 01 00 00 ...

`0x00463860` is inside the BSS segment, and the bytes at it are `640` and `480` —
the software rasteriser's own state. Execution had jumped into a data structure.

The cause: `dkr_render_backend` gained `texture_lookup` with E05's residency
cache, and the entry was added to the **Glide** factory alone. The software and
null factories assigned every field they knew about and left that one holding
whatever was on the caller's stack — the block is a local at every call site. The
decoder guards each call with `if (backend->texture_lookup)`, which stops a null
pointer and does nothing whatever about a stale one.

The synthetic scene had been passing through the same code for weeks. Its stack
happened to hold zero there.

Fixed by zeroing the block at the head of each factory, so that an entry nobody
implements is null — which is what the guards already expect and what "this
backend has no such thing" means. `test_software.c` now poisons the block with
`0xA5` before calling each factory and refuses any pointer-sized run of poison
that survives; the check fails on the code as it stood before the fix, which was
verified rather than assumed.

### The edge rule absolved the one real defect in the image

With the image finally produced, the first measurement read **186** frankly
different pixels off-edge. The nameplate is 2416 wrong pixels.

The metric forgives a pixel that stands next to a sharp change in the reference,
because a fill rule differing by half a pixel moves a whole column and that
signals nothing. That rule was written for four large triangles, where the pixels
next to a sharp change are exactly the thin boundaries. Applied to text — glyphs
one to three pixels wide, outlined — *every* pixel of the object is next to a
sharp change, and the rule absolved the object entire.

An instrument that under-reports the only real defect in the image by a factor of
thirteen is worse than no instrument: its silence is read as agreement.

The rule now asks the question the way the excuse is actually shaped. A half-pixel
shift does not invent a colour; it gives the pixel the colour of the surface
**next door**. So a pixel is forgiven only if what the card put there matches one
of the reference's neighbours. A colour that appears nowhere around it is wrong
wherever it sits.

The count went from 186 to 5071 — and the control matters as much as the
correction: under the same tightened rule, **the two oracles still differ by
zero pixels**. The extra 4885 are real, not manufactured by the change.

The difference map was split the same way: a real divergence in red, an edge
forgiven in dim amber. A map that painted both the same colour would disagree
with the number printed beside it, and the reader would believe whichever they
looked at last.

### A per-million figure that was wrong only on the target

The tightened metric's first run on the machine printed **2526 per million** where
the host printed **16507** for the same two images.

`1000000L * differ / total` overflows a 32-bit `long` as soon as `differ` passes
2147, and the target's `long` is 32 bits. The product wrapped, and the quotient
came out at a sixth of the truth — a plausible number, in the right units, on the
one machine whose numbers the whole exercise exists to read.

The computation now goes through `dkr_image_per_million`, in `double` and back,
shared by both programs. Checked by building the host tool `-m32` and confirming
it agrees with the 64-bit build, rather than by reasoning about it.

## What this does not establish

That the rendering is *right*. Both sides are this port's own code, and they
agree about a picture neither has checked against the console. What it
establishes is that the two implementations of the same specification agree
everywhere except one object, and that the object is named.

## Files

- `D:\RPLSOFT.BMP`, `D:\RPLCARD.BMP`, `D:\RPLDIFF.BMP` — the two images and the map
- `D:\REPLAY.TXT` — the counts and the metrics
- `docs/VISUAL-TESTING.md` — the procedure and the capture format
