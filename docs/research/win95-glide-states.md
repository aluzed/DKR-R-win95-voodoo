# Glide 2.x's constants, verified by reading the buffer back

Measured on 14 August 2026 on the test machine (Pentium II 400, Voodoo 2, Windows
95 OSR2), by `tools/win95/witnesses/glide_state_probe.c`.

## Why verify constants

There is no `glide.h` on this machine: the 3dfx driver ships `glide2x.dll` without
its header. The enumeration values `glide_backend.c` uses come from the Glide 2.4
specification, written from memory.

The danger is not that a wrong value causes an error. **Glide does not validate its
enumerations**: it programs the corresponding register and returns. The image comes
out different, without the slightest signal, and the deviation is then blamed on
the display-list decoder or on the transformer — that is, on the two modules one
reads first and where there is nothing to find.

Reading the frame buffer back (`grLfbLock`, see `win95-glide-bringup.md`) allows
the matter to be settled: draw a figure whose answer one knows, read a pixel,
compare against a value worked out by hand.

## The result

| What is exercised | Expected | Read | |
|---|---|---|---|
| Opaque blending | `0xFF0000` | `0xFF0000` | CONFIRMED |
| Alpha blending, α=128 on black | `0x800000` | `0x7B0000` | CONFIRMED |
| Additive blending, red on blue | `0xFF0040` | `0xFF0042` | CONFIRMED |
| Depth, near after far | red | red | CONFIRMED |
| Depth, far after near | red | red | CONFIRMED |
| Scissor, right half | ~56000 px | 55960 px | CONFIRMED |
| Alpha test, α=64 < ref 128 | 0 px | 0 px | CONFIRMED |
| Alpha test, α=200 ≥ ref 128 | 112000 px | 112000 px | CONFIRMED |

The colour deviations (`0x7B` for `0x80`, `0x42` for `0x40`) are 565
quantisation: five bits of red and blue, six of green. They are within tolerance
and signal nothing.

Fog and the back-face mode do not appear: the first is not yet exercised by a
scene that makes it measurable, the second is deliberately disabled on the card —
see below.

## The two figures that amount to a proof

The trial triangle has vertices at (320,40), (600,440) and (40,440). Its
analytical area is ½ × 560 × 400 = **112000 pixels**, and that is exactly the count
recorded in the alpha test. The card therefore fills the geometric surface with
neither overrun nor shortfall, and its edge fill rule counts each pixel only once.

The triangle being symmetric about x = 320, a scissor window limited to the right
half must keep 56000 of them. Recorded: 55960, that is 40 pixels fewer — the
boundary column, counted once. Glide's window is therefore indeed **inclusive on
the left, exclusive on the right**, which is what `backend.h` assumed.

## The depth buffer: the fault the witness caught

It is the only point where memory was wrong, and it was worth the detour.

The natural reasoning runs as follows. The vertex carries `oow = 1/w`; a near
object has a small `w`, hence a **large** `1/w`; in a w buffer, the near one must
therefore win with `GR_CMP_GREATER`. That is what had been written.

The screen stayed **entirely black**, in both drawing orders. It is that "in both
orders" which allowed the conclusion: a sorting defect would have given the wrong
triangle, not the absence of a triangle.

The reasoning forgets a step. Glide does not store `1/w` in the buffer: it stores
an encoded w value that **grows with distance**, and `grBufferClear` clears it to
`GR_WDEPTHVALUE_FARTHEST` = `0xFFFF`. Nothing being able to exceed that maximum,
`GR_CMP_GREATER` rejects the whole scene — including the first triangle, which has
nothing in front of it.

The correct comparison is `GR_CMP_LESS`, as in a z buffer.

The symptom was worth recording: a black screen is first diagnosed as a defect of
geometry, of the window or of a matrix, and one inspects a perfectly correct
transformation chain for a long time before suspecting a depth buffer that is
itself working perfectly.

## What is deliberately not translated

**Back faces.** `apply_cull` always disables `grCullMode`. The chain already culls
by itself (`dkr_cull_accept`), because the reference rasteriser must discard *the
same* triangles as the card — without which E09-S02's comparison would measure a
difference of convention rather than a difference of rendering. Programming the
card as well would cull twice, under two conventions that do not necessarily
coincide, and the risk is emptying everything.

**Textures.** `texture_upload` returns zero. Allocation in TMU memory is E05-S02,
translation of the RDP combiner is E05-S03. In the meantime the four combiner
modes fall back on the vertex colour: selecting an absent texture would give white,
that is a wrong screen *in a way that looks like a combiner defect*. A plainly
untextured rendering diagnoses better.

## The witness got it wrong itself once

The first version counted the painted pixels after the frame-rate loop, which
clears to a gradient: the 307200 pixels were "painted" and the verdict was positive
without establishing anything. It now draws one last frame on a black background
before reading.

The lesson goes beyond Glide: **a counter whose background value is not
distinguishable from the result measures nothing**, and it is all the more
dangerous for displaying a success.
