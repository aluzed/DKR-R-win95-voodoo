# E04-S05 — Clipping, viewport and scissor window

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E04-S03 |
| **Blocks** | E05-S01, E04-S08 |

## Context

3dfx cards do not clip geometry. They have a scissor window (`grClipWindow`) that
rejects fragments outside the zone, but a triangle with a vertex passing **behind the
camera** cannot be rejected at fragment level: its projection is mathematically
absurd, and it must be clipped before projection, in homogeneous space.

It therefore falls to the CPU to provide:

- the rejection of triangles entirely outside the view volume — cheap and profitable,
  since every rejected triangle is a triangle not transformed and not sent;
- the actual clipping of the triangles that cross the near plane, which produces new
  vertices and sometimes several triangles for one;
- back-face culling, which the card does not do either.

One favourable point: Glide tolerates screen coordinates moderately off-screen, and
its scissor unit does the rest. Only the near plane requires real clipping. That
distinction is the key to the cost: full clipping against the six planes would be very
expensive and is not necessary.

## Objective

To deliver a correct and cheap clipping stage, which hands the backend only
primitives the card can rasterise.

## Scope

**In:** rejection, near-plane clipping, back-face culling, viewport, scissor window.

**Out:** the Glide configuration of the scissor window (E05-S01).

## Work

1. Implement bounding-volume rejection, upstream of the transformation where
   possible. A whole object rejected before transformation saves all its vertices.
2. Implement near-plane clipping in homogeneous space, interpolating all of the
   vertex's attributes: colour, texture coordinates, fog. Forgetting an attribute
   produces an artefact visible only on the clipped triangles, hence rare and
   confusing.
3. Check experimentally the margin Glide tolerates off-screen, and align on it the
   "clip or let through" decision. That margin is measured; it is not deduced from
   the documentation.
4. Implement back-face culling according to the microcode's convention. Survey that
   convention in the decomp: the winding the N64 retains and the state that enables
   it.
5. Implement the viewport from the microcode's command: scale and translation,
   consistently with E04-S03's transformation.
6. Implement the scissor window and translate it into `grClipWindow`. DKR uses it in
   particular for multiplayer split-screen display — a case to be tested explicitly,
   including with four players.
7. Measure the share of the budget this stage consumes, and the number of triangles
   actually clipped on a typical scene. If that number is low, the stage does not need
   optimising; if it is high, it becomes a candidate for E08-S03.

## Acceptance criteria

- [x] The triangles crossing the near plane are clipped, with all attributes
      interpolated — position, colour **and** texture coordinates, each checked
      separately. Forgetting a single one would show only on the clipped triangles,
      hence rarely; the self-test confirms it by provoking it.
      The one-vertex-behind case does indeed produce **two** triangles: the remaining
      polygon is a quadrilateral, and not re-triangulating it would make half the
      surface disappear.
- [~] Glide's off-screen tolerance margin is measured and exploited — **Glide's
      margin is still not measured**: it requires reading the card's frame buffer
      (`grLfbLock`), a passthrough Voodoo's output appearing in no capture from the
      emulator.
      **But a different constraint is measured and exploited**: precision. A vertex
      created at the near plane projected to 16 million pixels, where the edge
      functions lose all meaning. The clipper now bounds the coordinates by a **guard
      band** at four half-screens, which took the gap between the host and the target
      from 2.59 % to 0.62 % of the pixels.
      The two margins answer distinct questions and the second does not wait on the
      first.
- [x] Back-face culling follows the microcode's convention: the winding depends on
      the **sign of the viewport's x scale**, surveyed in `f3ddkr_rt64.cpp`. A
      mirrored viewport inverts the apparent winding, and culling the wrong side would
      empty the screen.
- [ ] The viewport matches the modern target's — **blocked**, the comparison
      requiring captured scenes.
- [~] The split screen is correct with two, three and four players — the four layouts
      are computed and checked, including **the absence of overlap** between
      neighbouring quadrants. But that is rectangle geometry: the game has not
      exercised it yet.
- [ ] The share of the budget and the number of clipped triangles are measured —
      **blocked**: the number clipped depends on a real scene, and without it the
      share of the budget has no meaning.
- [~] No primitive reaches the backend out of domain — the off-screen rejection exists
      and discards a triangle only if **all three** vertices are on the same side.
      **No assertion in a development build**: there is not yet a complete path from
      the decoder to the backend on which to place it.

## Risks

Clipping is a classic source of subtle errors: a badly clipped triangle produces a
shard of geometry that crosses the screen, a very visible phenomenon and a hard one to
reproduce, because it depends on a precise camera angle. The tests must include a
camera that passes through the geometry, not only one that looks at it.

## References

- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — viewport and scissor commands
- E04-S03 — transformation, upstream
- E05-S01 — Glide configuration, downstream
