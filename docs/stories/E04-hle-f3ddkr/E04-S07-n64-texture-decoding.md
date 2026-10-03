# E04-S07 — Decoding the N64 textures

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E04-S02, E04-S06 |
| **Blocks** | E05-S02, E05-S08 |

## Context

The N64 and the Voodoo do not store textures the same way, and the gap bears on three
points at once.

**The formats.** The N64 offers RGBA16, RGBA32, IA16, IA8, IA4, I8, I4, CI8 and CI4.
The Voodoo offers RGB565, ARGB1555, ARGB4444, intensity, intensity-alpha, and an
8-bit palettised format. The match is good for some, imperfect for others: the N64's
RGBA16 is an ARGB1555, a direct transposition; I4 and IA4 have no equivalent and
require an expansion, hence a doubling of the memory they occupy.

**The palettes.** CI4 and CI8 textures are indexed, and the N64 has a dedicated
palette memory. The Voodoo has **only one palette active at a time per TMU**: two
palettised textures with different palettes cannot be bound simultaneously. It is a
structuring constraint, and the safest solution is often to expand the indexed
textures into a direct format — at the price of texture memory, which is precisely the
scarce resource.

**The interleaving.** The N64 stores its textures in an order interleaved on odd
rows. Decoding must undo it.

Added to that is the card's format constraint: power-of-two dimensions, 256 × 256 at
most, bounded aspect ratio.

## Objective

To decode every texture DKR uses into formats the target card accepts, with a measured
cost and memory occupancy.

## Scope

**In:** decoding, format conversion, palettes, host-side cache.

**Out:** placement in texture memory (E05-S02) and filtering (E05-S08).

## Work

1. Inventory the texture formats and sizes DKR really uses, with their frequency. The
   neighbouring native port extracted **2,687 textures** from the ROM and already has
   that information.
2. Write the decoder for each N64 format into a target format, documenting any loss.
   RGBA32 in particular does not survive as it is: the Voodoo is a 16-bit card, and
   the conversion has to be chosen — dithering, or truncation.
3. Undo the N64 interleaving. Test it on textures of various sizes: it is a classic
   bug that only manifests at certain widths.
4. Settle how indexed textures are handled: a single hardware palette, or expansion
   into a direct format. Decide **on a measurement** of the resulting memory
   occupancy, confronted with E00-S05's per-TMU texture memory budget.
5. Deal with non-conforming dimensions: scaling to a power of two, or padding. Check
   that the texture coordinates are adjusted accordingly — that is the place where
   half-texel offsets get introduced, and they show at the edges.
6. Implement a host-side cache indexed by the texture's key, to avoid decoding again
   every frame. Size the cache on E00-S06's budget.
7. Measure the cost of decoding: per texture and per frame, in steady state and during
   a level load. Expensive decoding at load time is acceptable; mid-race, it is not.
8. Compare the decoded textures against the modern target's, taking the reduction in
   colour depth into account.

## Where it stands (3 October 2026)

Nothing here had been written down since August; this section was established
from the code, the tests and the 66-capture corpus.

**Seven format/size pairs decoded, not twelve.** `platform/render/texture.c`
converts RGBA16, RGBA32, I8, I4, IA16, IA8 and IA4 to ARGB1555. CI4, CI8 and
YUV are refused and counted: the indexed formats need the palette that
`LOADTLUT` places in the other half of texture memory, and the decoder does
not model it. The stories table had said "12 formats" since 28 August; it
was wrong. What the game sends, measured on the corpus by printing each
conversion's format (a probe in a copy outside the tree):

| Format | Conversions | Share |
|---|---|---|
| RGBA16 | 5,364 | 61% |
| RGBA32 | 2,565 | 29% |
| IA8 | 832 | 9.5% |
| anything else | 0 | |

8,761 conversions, and `replay` now prints the decoder's counters ("texture
decoder: ... unsupported="): **0 refused** in the 66 captures.

**And in the whole ROM, no indexed texture.** The decomp has CI4 and CI8
load paths (`material_init` in `textures_sprites.c`, with `LOADTLUT`), so the
corpus alone could not settle it. The decomp's extraction of the US 1.0 ROM
(`assets/.vanilla/us.v77`, one JSON per asset with its `format`) does: of
2,307 texture assets, 1,539 RGBA16, 486 RGBA32, 272 IA8, 5 I4, 3 IA16, 2 I8,
1 IA4 -- **no CI4, no CI8, no YUV**. Every format the game's textures use is
one this decoder converts; the indexed paths are code the data never takes.

**The odd-row swap, at several widths.** `test_texture.c` checks it on an
8-wide RGBA16 image, a 12-wide I8 image read from 16-texel rows, and a 6-wide
RGBA32 image -- a quarter row for 8-bit texels, half for 32-bit, and at the
end of a row whose width is not a multiple of the swap, the texels whose
partner would lie outside the row stay where they are. That last rule is what
the code does and the test pins; the test does not prove the RDP does the
same. The corpus's textures come in 35 widths from 4 to 248, and 40% of the
conversions are not a power of two.

**Non-power-of-two sizes** are padded by repeating the pattern
(`f3ddkr.c`, "The padding"), and the coordinates are normalised over the
padded size. A texture that wraps shows its padding at the seam; the code
says so. No offset has been measured on such a texture.

**The cache.** The 98.9% the table quoted was the TMU allocator's counter on
26 August, withdrawn on 18 September (E05-S02): measured again, 98.5% then
99.7%, with a peak of 1,087 K of a 2,048 K TMU. In play, 0.6 conversions a
display list (`docs/research/frame-budget.md`).

**The decoding cost, on the target** (3 October 2026, `DKR_TRACE_RENDER_ZONES=1`
at `6bcf163`, from boot through the menus to a race driven for two minutes,
`scripts/Drive-To-Race.sh`; guest time). 2,670 display lists, **952
conversions, 70.8 ms in all: 74 µs a texture** on average, between 14 and 280
µs by size. They come in bursts where a scene loads -- the heaviest, 163
conversions in 60 lists, 11.1 ms, 0.19 ms a list -- and almost never in play:
**4 conversions in the last 810 lists**, all of them in the race, where the
other 303,705 bindings were served resident or reused. Against a 33.3 ms frame
that is nothing in play and a fraction of a frame at a load.

**Not done:** the comparison with the modern
target's textures (no modern build here); and the host-side footprint of
decoded textures against ADR 0003's 8 MiB reserve.

## Acceptance criteria

- [x] Every texture format DKR uses is decoded -- seven, from the ROM's 2,307
      texture assets (above).
- [x] The interleaving is correctly undone, tested at several widths -- three
      widths and three texel sizes (above).
- [x] The handling of indexed textures is settled on a measurement of memory
      occupancy, confronted with the per-TMU budget. *Settled without one*: the
      ROM holds no indexed texture, so there is nothing to expand or to give a
      palette; they stay refused and counted, which would show at once if a
      mod brought one.
- [ ] Non-conforming dimensions are handled with no visible texel offset.
- [ ] The cache avoids re-decoding in steady state, and its occupancy respects the
      budget.
- [x] The decoding cost is measured, at load time and during play -- 74 µs a
      texture, 11.1 ms over the heaviest load's 60 lists, 4 conversions in
      810 lists of racing (above).
- [ ] The decoded textures are compared against the modern reference, the difference
      being attributable to the reduction in colour depth alone.

## Risks

Expanding indexed textures into a direct format can make texture-memory occupancy
explode: a CI4 expanded into ARGB1555 takes four times as much. If the peak per level
exceeds the TMU's memory, we shall have to fall back on the hardware palette and manage
its changes — which entails grouping the draws by palette, hence constraining the
render order. That dependency must be assessed in E05-S02, not discovered at run time.

## References

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — a peak of 1.20 MB
  per level
- `../../Diddy-Kong-Racing/docs/stories/E02-assets/E02-S03-conversion-textures-palettes.md`
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — `SetTextureImage`, `LoadBlock`
- E00-S05 — texture memory per TMU
