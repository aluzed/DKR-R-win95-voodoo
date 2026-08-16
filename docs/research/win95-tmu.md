# What the TMU demands — measured before designing the allocator

Recorded on 14 August 2026 on the test machine (Voodoo 2, 2 TMUs), by
`tools/win95/witnesses/tmu_probe.c`.

## Why measure rather than assume

Glide has no texture manager. It exposes the TMU's memory as a raw address space:
one picks an address, downloads there, and binds that address for drawing.
Allocation, fragmentation and eviction are entirely ours to write.

An allocator content with `width × height × 2` would stack the textures too
tightly. The symptom would not be an error — Glide validates nothing — but **one
texture overwriting another**, hence a piece of scenery carrying another's pattern,
in a place that depends on the load order.

## The report

    TMUs present     : 2
    memory per TMU   : 2048 KB each
    addressable space: 0x00000000 .. 0x001FFFF8, identical on both

`grTexMinAddress` returns **zero**. That is not trivial: zero is therefore a
perfectly valid address, and cannot serve as a failure sentinel. Hence
`DKR_TMU_NONE` at `0xFFFFFFFF`.

**Eight bytes** are missing for the TMU to be a full 2 MiB.

### What a texture really costs

| size | computed | returned by the card |
|---|---|---|
| 256×256 | 131072 | 131072 |
| 128×128 | 32768 | 32768 |
| 64×64 | 8192 | 8192 |
| 32×32 | 2048 | 2048 |
| 8×8 | 128 | 128 |
| 2×2 | 8 | 8 |
| **1×1** | **2** | **8** ← rounded up |
| 64×32 | 4096 | 4096 |
| 8×64 | 1024 | 1024 |

Fourteen sizes out of fifteen cost exactly what the computation announces. The
fifteenth gives the granularity: **8 bytes**.

The five formats tested — RGB 565, ARGB 1555, ARGB 4444, 8-bit alpha, 8-bit
palettised — cost exactly what their depth announces.

These figures also validate the enumeration values, written from memory for want
of a `glide.h`: a wrong `GR_LOD_*` or `GR_ASPECT_*` would have given a visibly
wrong size. The LOD names the **largest dimension** and decreases — 256 is zero, 1
is eight — which is the reverse of intuition.

## What the measurement decided

**Every texture size is a power of two.** That is not a statistical observation but
a consequence: the dimensions expressible by the (LOD, aspect ratio) pair are
powers of two, and the depth is one or two bytes.

The ticket considered a size-class allocator "if the game's textures fall into a
small number of sizes". The measurement gives better than a distribution: it gives
a property. A **buddy** allocator is exactly a size-class allocator with coalescing,
and on requests that are all powers of two it produces **no external
fragmentation** at all.

That counts here more than elsewhere. On a card without paging, fragmentation that
refuses a texture does not degrade performance: it makes a piece of scenery go
missing.

## The eight missing bytes cost 128 bytes, not 8 KiB

The tree covers a full 2 MiB; the card offers only `0x1FFFF8`. The top of the tree
is therefore reserved, descending down to the smallest block that really contains
the boundary.

A consequence measured by the trial suite: the TMU holds **255** 64×64 textures and
not 256 — the last 8 KiB block is cut, hence no longer allocatable whole. But its
tail stays usable at a fine grain: 63 blocks of 128 bytes still fit there. The
reserve's real cost is 128 bytes per TMU.

The distinction is not academic: losing 8 KiB would be the price of four 32×32
textures.

## The scale of the texture coordinates

Discovered while checking that the texture really reaches the screen, and it goes
beyond E05-S02: **Glide expects `s` and `t` in a 256-texel space, whatever the
texture's real size.**

Measured by `glide_texture_probe.c` on a 64×64 checkerboard whose four corners
carry distinct colours, trying five scales:

    s,t over  64 : TL red   TR red    BL red   BR red      (a single cell)
    s,t over 128 : TL red   TR grey   BL grey  BR grey
    s,t over 255 : TL red   TR green  BL blue  BR yellow   <-- the four corners
    s,t over 256 : TL red   TR green  BL blue  BR yellow   <-- the four corners
    s,t over 512 : TL grey  TR grey   BL grey  BR yellow

That is a factor of 256/width, here 4.

The contract keeps that convention rather than [0,1] for a reason of cost: the
factor is a **constant**, independent of the bound texture. The Glide backend
therefore receives the vertices as they stand — that is the whole point of having
mirrored `GrVertex` field for field — and it is the reference rasteriser, which has
no speed constraint, that divides to get back to [0,1]. The reverse would have
imposed a copy of every vertex before every triangle, on a machine where the
transformation already costs 0.682 µs per vertex.

The trap this closes: **the two backends were not speaking the same language** —
the rasteriser sampled in [0,1], the card in 256 — and E09-S02's comparison did not
see it, for want of a texture in the scene.

## A black-and-white checkerboard would have proved nothing

The texture witness uses four distinct colours at the four corners, which verifies
in one go that the texture arrived, that it is read the right way round, that the
aspect ratio is right, and that the combiner takes the texel rather than the vertex
colour. A symmetric checkerboard would have passed a swap of `s` and `t` without
flagging it — and such a swap gets the whole game wrong.

The negative control counts as much: in vertex-colour mode, the screen must be
uniformly white. Without it, the four preceding checks could be measuring state
inherited from the previous frame.

## What remains assumed

**The smallest block's size**, 128 bytes, that is an 8×8 texture in 16 bits.
Descending to the hardware granularity of 8 bytes would multiply the tracking table
by sixteen for textures that probably do not exist. It is the only place in the
allocator that does not rest on a measurement, and it will fall as soon as the ROM
allows the real sizes to be recorded.
