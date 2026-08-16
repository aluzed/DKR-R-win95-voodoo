# Chaining the two TMUs

Measured on 14 August 2026 on the test machine (Voodoo 2, 2 TMUs), by
`tools/win95/witnesses/multitex_probe.c`.

## `grTexCombine`'s values, measured — and wrong from memory again

It is the **third time** in this port that a Glide enumeration value written from
memory turns out to be wrong, and the third time only measurement says so. The two
previous ones: the w buffer's comparison direction, then the whole `BLENDI` family
of the colour combiner.

A sweep over two textures made to be told apart — pure red on TMU 1, pure blue on
TMU 0:

| Value | Read | What it is |
|---|---|---|
| 0 | `000000` | zero |
| **1** | `0000FF` | **`DECAL`** — TMU 0 alone |
| 2 | `FFFFFF` | (saturated) |
| **3** | `FF0000` | **`OTHER`** — TMU 1 alone |
| **4** | `FF00FF` | **`ADD`** — both |
| 9 | `000000` | multiplication, or zero |

`OTHER` is 3 and `ADD` is 4: shifted by one notch from what had been written. The
witness itself paid for it — its consistency check asked for `OTHER` believing it
asked for `ADD`, and therefore saw only one layer.

Textures that resembled each other would have made this sweep mute. It is the same
principle as E05-S02's four-colour checkerboard: **a trial must tell faults apart,
not merely pass.**

## A trap in the witness, worth writing down

The first version left the render state at `DKR_COMBINE_SHADE`. The colour
combiner then ignores the TMUs' output, and all twelve functions rendered white —
a uniform screen that said nothing.

Worse: the coordinate-consistency check **passed there**, by counting zero black
pixels on an entirely white screen. That is exactly the defect noted when reading
the frame buffer back: *a counter whose background value is indistinguishable from
the result measures nothing, and it is all the more dangerous for displaying a
success.* The check now also verifies that the two halves **differ**.

## Coordinate consistency

Each TMU has its own coordinate set in the Glide vertex. A mistake there shifts the
two layers relative to each other, which **looks like a combiner defect** and
diagnoses very badly.

Verified on two complementary patterns — left half filled on one, right half on the
other — added together by `ADD`:

    black pixels: 0 out of 307200
    left quarter 0xFF0000, right quarter 0x0000FF

Zero uncovered pixels, and each half really comes from a different unit.

## The single-TMU fallback

The ticket names the risk: "easy to write and easy never to test, for want of
single-TMU hardware to hand". Hence `dkr_glide_backend_force_single_tmu`, which
forces the multipass path on a card that has two. Without that flag, the fallback
would only be checked after a user report — hence on somebody else's machine, and
with no trace.

Result: **0 differing pixels out of 307200**. The fallback produces a strictly
identical image.

## The gain, and the reservation to be set down

    100 frames in one pass   : 1552 ms
    100 frames in two passes : 1662 ms
    fallback overhead        : 7 %

**Seven per cent is not the cost of multipass.** It is its cost *on a scene that
does not saturate the fill rate*. At 64 frames per second, the buffer swap is
synchronised to the scan, and the card waits: one more pass slips into that dead
time unseen.

On a scene genuinely fill-limited — which the game will be — doubling the painted
area will cost considerably more. The honest figure is therefore: the fallback is
free as long as one is not saturated, and measuring it on a representative scene
requires the ROM.

That is also why chaining stays the default path, and the fallback a degradation:
the fallback additionally duplicates the second layer's texture onto TMU 0, and
texture memory is the scarce resource.

## The allocator

The two units have their own memory — two spaces, not one. `dkr_texture_desc`
therefore carries the intended TMU, and `bind_texture` binds on the unit where the
texture resides.

Binding a TMU 1 address on TMU 0 causes no error: TMU 0 simply samples whatever is
lying at that address in its own memory, and the scenery carries another's pattern.

Verified: after uploading one texture on each unit, each occupies exactly 8192
bytes.

## The order of the `grTexCombine` calls

Glide wants the highest TMU first: it is the one that begins the chain.
Programming TMU 0 before TMU 1 leaves it chained onto a unit not yet configured.
The effect is not an error but an image built from the previous state — hence
correct as long as nothing changes, and wrong at the first state change, which is
the worst moment to notice it.
