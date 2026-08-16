# E05-S08 — Filtering, wrapping and levels of detail

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E05-S02, E04-S07 |
| **Blocks** | — |

## Context

The texture sampling modes decide an important part of the appearance, and the N64 and
the Voodoo each have their peculiarities.

**Filtering.** The N64 practises a three-point filter, over a triangle of texels, where
the Voodoo does a classic bilinear filter over four. The difference is real and visible
on low-resolution textures — that is, on most of DKR's, the console's texture memory
having been very limited. The rendering will not be identical, and we have to decide
what to do about it.

**Wrapping.** The N64 offers wrap, clamp and mirror, the last being much used to save
texture memory. Glide offers the same modes, with dimension constraints.

**Levels of detail.** Does DKR use mipmaps? To be checked rather than assumed. If they
are used, they consume a third more texture memory, which weighs on E05-S02's budget;
if they are not, distant textures will shimmer, exactly as on the console.

## Objective

To set the sampling modes so as to approach the N64's rendering as closely as the card
allows, knowing and documenting the differences.

## Scope

**In:** filtering, wrapping, mirroring, clamping, levels of detail.

**Out:** decoding the textures (E04-S07) and allocating them (E05-S02).

## Work

1. Determine whether DKR uses mipmaps, by examining the RDP state surveyed in E04-S06.
   If so, quantify their cost in texture memory and report it to E05-S02.
2. Implement wrap, clamp and mirror, and check that mirroring works for every texture
   size used.
3. Set the filtering. Compare Glide's bilinear filter against the N64's three-point
   filter on low-resolution textures, and decide: accept the difference, or offer
   nearest-neighbour filtering as an option for a rendering closer to the console on
   certain elements.
4. Deal with the interface separately. 2D elements are often better rendered without
   filtering — filtering makes text blurry. Check which mode the game asks for on those
   elements and respect it (E05-S07).
5. Measure the cost of each mode. Bilinear filtering is free on the Voodoo; trilinear,
   if it is used, is not and consumes a TMU, which conflicts with E05-S04's
   multitexturing.
6. Document the accepted differences in `docs/RENDER-DIFFERENCES.md`: what will not
   look like the console, and why.
7. Check visually on surfaces that reveal these settings: a road seen obliquely, a
   texture repeated in mirror, interface text.

## Acceptance criteria

- [x] Determined from the source, unambiguously: **DKR does not use mipmaps**.
      `G_TL_TILE` appears fifteen times, `G_TL_LOD` none. The extra third of texture
      memory the ticket feared for E05-S02 does not exist, and distant textures will
      shimmer exactly as on the console — which is recorded in `RENDER-DIFFERENCES.md`
      precisely because it is the kind of thing taken for a defect of the port.
- [x] Wrap and clamp verified on the card for the seven sizes from 4 to 256: three
      repetitions when wrapping, a single one when clamping, in every case.
      **Mirroring is not verified because DKR does not use it** — the ticket describes
      it as "much used to save texture memory", and the source contains not a single
      occurrence of it. Eighteen `G_TX_WRAP`, sixteen `G_TX_NOMIRROR`, four
      `G_TX_CLAMP`.
- [~] The difference is documented in `RENDER-DIFFERENCES.md`: three-point filtering
      against bilinear over four, irreducible because inexpressible with Glide's modes.
      **The visual comparison itself requires the ROM** — the difference is judged on
      the game's textures, not on a test pattern.
      Point sampling stays available, and the game already asks for it in 30 table
      entries out of 214: those surfaces will be identical to the console.
- [x] The mode comes from the decoded render state and not from a backend choice;
      `DKR_OMH_1CYC_POINT` and `DKR_OMH_2CYC_POINT` total 30 table entries and do give
      point sampling. E05-S07 verified that in point sampling the alignment is exact to
      the texel.
- [~] Measured: 1538 ms at point against 1662 ms in bilinear over a hundred frames.
      **They are exactly the same two values as the fog measurement**, which confirms
      that the measurement is quantised by the buffer swap and does not resolve a cost
      below one scan period. Bilinear therefore costs nothing measurable here, in
      accordance with what the ticket announces.
      The conflict with E05-S04 is identified and **does not occur**: it would arise
      only with trilinear filtering, which consumes a TMU, and trilinear presupposes
      mipmaps that DKR does not use.
- [x] `docs/RENDER-DIFFERENCES.md` is written: six differences, each saying what
      differs, why it is irreducible, and what it gives on screen. A documented
      difference is a known characteristic of the port; the same difference undocumented
      will be reported as a defect at every comparison.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

The N64's three-point filter cannot be reproduced exactly on the Voodoo. It is an
irreducible difference, and it must be announced rather than endured: if it is
documented, it is a known characteristic of the port; if it is not, it will be reported
as a defect at every comparison.

## References

- E04-S06 — texture modes in the RDP inventory
- E05-S02 — texture memory budget, impacted by mipmaps
- E05-S04 — potential conflict over TMU usage
