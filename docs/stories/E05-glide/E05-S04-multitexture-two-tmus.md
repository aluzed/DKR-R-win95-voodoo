# E05-S04 — Multitexturing on two TMUs

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E05-S02, E05-S03 |
| **Blocks** | E08-S01 |

## Context

Three of the combiner configurations DKR uses read **two texels**
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`). On a card with a
single TMU, they impose two render passes. On a Voodoo 2, which has two, they are done
in a single pass: `grTexCombine` chains TMU 1's output into TMU 0.

Three configurations out of thirty-three seems anecdotal. It is not: what has to be
looked at is the **screen area** they cover, not their number. Two-texel combinations
typically serve for blending terrain textures and for surface effects — that is, for
large expanses of pixels. E04-S06 supplies that figure, and it is what decides this
ticket's priority.

This ticket is also what makes the fallback to one TMU acceptable: the rendering must
stay correct on a Voodoo 1 or a Banshee, in two passes, even though the recommended
target has two.

## Objective

To realise the two-texel configurations in a single pass on two TMUs, with a correct
multipass fallback on one TMU.

## Scope

**In:** chaining the TMUs, placing the textures, the fallback.

**Out:** the general translation of the combiner (E05-S03).

## Work

1. Measure the screen area covered by the two-texel configurations, from E04-S06's
   inventory. That figure is the only judge of this ticket's priority.
2. Implement the chaining: TMU 1 samples, its output becomes an input of TMU 0, whose
   output feeds the colour combiner.
3. Extend E05-S02's allocator: a texture intended for TMU 1 must be resident there.
   Two spaces, then, and a placement policy that avoids needlessly duplicating a
   texture on both units — memory is the scarce resource.
4. Implement the one-TMU fallback in two passes, and check that it produces the same
   visual result as the single two-TMU pass. Compare by image difference, not by eye.
5. Select the path at run time according to the number of TMUs detected in E05-S01. No
   hard-coded capability.
6. Measure the gain: time per frame on a representative scene, one pass against two.
7. Check the consistency of the texture coordinates between the two units. Each TMU has
   its own set of coordinates in the Glide vertex, and an error here produces an offset
   between the two layers — visible, and easy to confuse with a combiner problem.

## Acceptance criteria

- [~] The **weight** of the two-texel configurations is recorded: 34 table entries out
      of 214, that is 15 %, and the heaviest of the whole game
      (`G_CC_BLENDTEX_PRIM`, 32 entries) is one of them. That is not anecdotal. The
      real **screen area**, on the other hand, requires a measurement at run time,
      hence the ROM.
- [x] The chaining works in one pass on two TMUs, verified by reading back: `DECAL`,
      `OTHER` and `ADD` each produce the expected image. The enumeration values are
      **measured** — `OTHER` is 3 and `ADD` is 4, shifted by one from what had been
      written from memory.
- [x] The one-TMU fallback produces a **strictly identical** image: 0 pixels different
      out of 307,200, verified by difference and not by eye.
- [x] The allocator manages both spaces: `dkr_texture_desc` carries the TMU aimed at,
      and `bind_texture` binds on the unit where the texture resides — binding a TMU 1
      address on TMU 0 causes no error, TMU 0 sampling whatever is lying at that
      address in its own memory. Verified: each unit occupies exactly 8192 bytes after
      a load.
      Duplication happens only in the fallback, where it is the price to pay — and
      that is one more reason for the fallback not to be the default.
- [x] The path is chosen at run time by `dkr_glide_backend_tmu_count`, which reads
      E05-S01's detection — and the test override. No hard-coded capability.
- [~] Measured: 1552 ms in one pass against 1662 ms in two, over a hundred frames,
      that is 7 % of overhead. **That figure calls for a reservation**: at 64 frames
      per second the swap is synchronised on the scan and the card waits, so one more
      pass slips into dead time. Seven per cent is the cost of the fallback on a scene
      that does not saturate fill, not the cost of multipass in general. Measuring it
      on a representative scene requires the ROM.
- [x] The two units' coordinates are consistent, verified on two complementary patterns
      added together: 0 pixels uncovered out of 307,200, and each half does come from a
      different unit. That second check is not redundant — the witness's first version
      counted zero black pixels on an entirely white screen, and passed without
      establishing anything.

## Risks

The one-TMU fallback is easy to write and easy never to test, for want of single-TMU
hardware to hand. It must be testable by configuration — a setting that forces the
multipass path even on a two-TMU card — failing which it will only be checked after a
user reports it.

## References

- `../../Diddy-Kong-Racing/docs/research/combiner-inventory.md` — 3 two-texel
  configurations
- E05-S02 — allocator, to be extended to the two spaces
- E05-S03 — mapping table
