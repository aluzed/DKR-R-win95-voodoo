# E05-S06 — Fog

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | S |
| **Depends on** | E04-S06, E05-S01 |
| **Blocks** | — |

## The fog was reaching the card, and it painted the screen black — 17 September 2026

Recorded here because it is this ticket's subject and it was found somewhere else:
the menus were black on the Voodoo, and the cause was fog.

The chain is short and every link was already written down separately. Forty-three
of a menu's seventy-three draws reach the card with fog programmed. `G_SETFOGCOLOR`
is never decoded, so the fog colour is **zero**. `apply_fog` asks Glide for
`GR_FOG_WITH_ITERATED_ALPHA`, and this game's vertex alpha carries **opacity** - 255
on an opaque draw. Black fog at full strength over a fragment that passed every
test.

    CAP0420 on the card       painted    colours
      fog programmed          29.87 %      4590
      fog off                 99.18 %      5013

The second line is the same capture's render from a binary built before the `G_FOG`
gate landed, colour count included. The software oracle is the control: its image is
identical to the MD5 either way, because its own fog is inert - `k = clamp(z)` with
`z = -oow`, never positive.

**So fog is now off unless `DKR_FOG=1` asks for it**, which is what
`glide_renderer.cpp` has said in a comment since the switch existed, and what
E00-S03's sibling investigation concluded when it closed fog as derived and
deliberately not implemented. The decoder still records what the list *asked* for,
in `fog_asked`, so `emitted_fogged` keeps reporting 43 of 73 rather than collapsing
to zero and ceasing to be a diagnostic.

**What this ticket inherits.** Whoever implements fog properly needs two things this
port does not yet have, and both are named in the derivation above: a decoded
`G_SETFOGCOLOR`, and a coefficient that is not the vertex alpha. Turning the switch
on without them reproduces the black screen exactly.

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
- [ ] Comparison with a camera moving away — still not done, but **its stated
      blocker is lifted**. "The port does not render one legibly yet" stopped being
      true on 17 September: a race is reached, captured as `CAP2600`, and rendered
      on the card to 211 divergent pixels of 307,200 against the oracle. What is
      still missing is a *sequence* - this is one capture, and the ticket is right
      that a wrong curve does not show on a still image.
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
