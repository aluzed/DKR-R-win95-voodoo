# E05-S06 — Fog

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | S |
| **Depends on** | E04-S06, E05-S01 |
| **Blocks** | — |

## Context

DKR uses fog visibly: it masks the draw-distance limit, and it contributes to the
atmosphere of several levels. Fog that is absent or badly calibrated does not
translate into an image that is "a bit different" but into objects appearing abruptly
in the distance.

The N64 computes fog per vertex, in the RSP, and the RDP applies it through the
combiner. Glide proceeds otherwise: it has a dedicated fog unit, driven by a **table
of 64 entries** (`grFogTable`) indexed by depth, plus a fog colour. A mode also allows
the factor to be supplied directly per vertex, which matches the N64's model better.

That second route is probably the right one: the fog factor is already computed per
vertex in the pipeline (E04-S03), and supplying it directly avoids having to
reconstruct a table equivalent to the game's curve.

## Objective

To reproduce DKR's fog on Glide, with a transition visually identical to the
reference.

## Scope

**In:** computing the fog factor and applying it.

**Out:** the draw distance itself, which belongs to the game.

## Work

1. Survey how the microcode computes the fog factor per vertex, and how the combiner
   applies it. The decomp is the reference source here.
2. Settle between the two Glide routes: factor per vertex, or table indexed by depth.
   The first matches the N64's model better; measure whether it has a significant
   per-vertex cost before concluding.
3. If the table is retained, build it from the game's real curve, and measure the
   approximation error its 64 entries introduce.
4. Implement the fog colour, which varies by level: it is supplied by the game's
   state, not fixed at build time.
5. Check the interaction with blending and the alpha test (E05-S05): a translucent
   surface in fog is a case where the orders of application differ between the RDP and
   Glide.
6. Compare visually against the reference on the levels where the fog is most marked,
   and with a camera moving progressively away — it is the progression of the
   transition that reveals a wrong curve, not a fixed capture.
7. Measure the cost, which ought to be negligible since the unit is hardware.

## Acceptance criteria

- [x] Surveyed from the decomp: `gSPFogPosition(min, max)` loads a multiplier
      `128000/(max-min)` and an offset `(500-min)*256/(max-min)`, from which the RSP
      derives a per-vertex factor stored in **the vertex's alpha**. The blender applies
      it through `G_RM_FOG_SHADE_A`.
      The state decoder was corrected along the way: it left the fog bit hard-wired to
      zero, whereas it is the game's most frequent render mode. It is deduced from the
      blender, and the value 3 there means `G_BL_CLR_FOG` in position `m1a` but
      `G_BL_0` in `m1b` — a check exercises that trap.
- [x] The per-vertex factor is retained, and the measurement justifies it: the
      gradient is regular from `DE1C00` to `18DB00` by way of `7B7D00`, and the
      direction is the right one — alpha 255 means full fog, like the N64's factor
      which grows with distance. Getting the direction wrong would give inverted fog.
- [x] Moot: the table is not retained. It would bring nothing but an approximation of a
      curve we already possess exactly, per vertex.
- [~] The colour is taken from the render state and not fixed at build time;
      `grFogColorValue` receives it at every state change. The **variation from one
      level to another** comes from `set_fog` and `rain_fog`, surveyed in the decomp,
      but is not exercised for want of the ROM.
- [x] Checked, and it reveals a coupling the ticket did not announce: **the vertex's
      alpha serves fog and transparency simultaneously**. Both work, but they are not
      independent — one cannot be set without disturbing the other, and the game uses
      78 translucent modes for 74 fog modes.
      A consequence for E05-S03: the way out consisting of carrying the second
      constant colour in the vertex alpha conflicts with fog. It is not general.
- [ ] Comparison with a camera moving away — **blocked by the ROM**. The ticket is
      right to insist: a wrong curve does not show on a still image. The gradient
      measured here is a transition in space, not in time.
- [~] Measured, but the measurement does not settle it: 1538 ms against 1662 ms over a
      hundred frames, that is two different multiples of the scan period. **The
      measurement is quantised by the buffer swap** and cannot resolve a cost below one
      period. What can be asserted: the fog does not push us across more than one
      period, which bounds its cost from above.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

A wrong fog curve is hard to spot on a single capture and leaps to the eye in motion.
Step 6's check must therefore be a sequence, not an image.

## References

- E04-S06 — fog state in the RDP inventory
- E04-S03 — per-vertex factor, computed upstream
- [3dfx Glide sources](https://sourceforge.net/projects/glide/) — `grFogTable`,
  `grFogMode`
