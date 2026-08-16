# E04-S07 — Decoding the N64 textures

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | TODO |
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

## Acceptance criteria

- [ ] Every texture format DKR uses is decoded.
- [ ] The interleaving is correctly undone, tested at several widths.
- [ ] The handling of indexed textures is settled on a measurement of memory
      occupancy, confronted with the per-TMU budget.
- [ ] Non-conforming dimensions are handled with no visible texel offset.
- [ ] The cache avoids re-decoding in steady state, and its occupancy respects the
      budget.
- [ ] The decoding cost is measured, at load time and during play.
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
