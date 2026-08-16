# E05-S05 — Alpha, depth buffer and blending

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E04-S06, E05-S01 |
| **Blocks** | E09-S02 |

## Context

The RDP has a configurable blender, a depth test, and render modes that combine the
two in sometimes unexpected ways. Glide offers `grAlphaBlendFunction`,
`grAlphaTestFunction`, `grDepthBufferFunction` and `grDepthMask` — close in spirit,
with differences that count:

- **Depth.** Glide's depth buffer is 16 bits, and the card offers a choice between a Z
  buffer and a W buffer. W offers a far better distribution of precision in depth,
  which counts in a racing game where the track stretches far ahead. The N64 had its
  own handling, with its own compressed format; the correspondence is not direct and
  has to be chosen.
- **The alpha test.** DKR uses it massively for vegetation and cut-out billboards.
  Glide offers a classic alpha test, and `grChromakeyMode` as an alternative — to be
  evaluated, the second possibly being cheaper depending on the configuration.
- **Blending.** The RDP's blender is more expressive than Glide's, in particular
  because it can bring depth into play. Some configurations will have no exact
  equivalent.

## Objective

To realise the depth, alpha-test and blending modes DKR uses, with the precision
artefacts under control.

## Scope

**In:** depth test and write, alpha test, blending, render order of translucent
surfaces.

**Out:** the combiner (E05-S03) and fog (E05-S06).

## Work

1. Survey, from E04-S06's inventory, the render modes the game really uses, with their
   frequency.
2. Settle between a Z buffer and a W buffer, on a measurement of the precision
   artefacts: look for depth fighting on distant coplanar surfaces, a frequent case on
   a racing track. Record the comparison.
3. Set the depth range consistently with the value E04-S03 produces. It is a point of
   agreement between two tickets, and a disagreement there produces a globally wrong
   depth sort — hence a very visible one.
4. Implement the alpha test, and compare with `grChromakeyMode` on cost and on result.
   Check the rendering of vegetation, which depends on it directly.
5. Implement the blending modes, flagging those that have no exact equivalent and
   measuring their deviation from the reference rasteriser (E04-S08).
6. Deal with the render order of translucent surfaces. The N64 drew in display-list
   order, and the game depends on it: reproduce that order rather than sort. Check that
   no batching optimisation has reordered the translucent primitives — it is a classic
   trap of any state grouping.
7. Check the cases known to reveal these defects: vehicle shadows, water, particle
   effects, reflections.
8. Measure the cost of the depth test and of blending within the fill budget.

## Acceptance criteria

- [~] The modes are surveyed in the neighbouring port's source, by frequency:
      `XLU_SURF` 78, `OPA_SURF` 47, `TEX_EDGE` 25, `DECAL` 21, `INTER` 12, plus 74
      `FOG_SHADE_A` which belong to E05-S06. Opaque, translucent and alpha test are
      implemented and measured; **the depth bias of the `DECAL` and `INTER` modes is
      not**, its useful value being set against a real scene.
- [x] Measured and recorded — **and the result contradicts the ticket's hypothesis**.
      Both buffers resolve 2 ‰ at every distance in the range and both give way
      entirely at 0.2 ‰: the wall is in the same place. W is kept for a reason that is
      not precision — it consumes `oow` as it is, where Z would require rescaling every
      vertex.
      See `docs/research/win95-depth-and-blending.md`.
- [x] The range is consistent: from 0.50025 at 20 units to 0.99983 at 15000, and over
      that whole range the near masks the far.
- [~] The alpha test is measured in both directions on the card — 0 pixels below the
      threshold, 112,000 above, that is exactly the triangle's analytic area. **The
      comparison with `grChromakeyMode` is not done**, and rendering the vegetation
      requires the ROM.
- [~] The only structural deviation is identified: the `AA_` prefix, a pixel-coverage
      antialiasing built into the RDP's blender, which Glide 2 offers only through
      `grAADrawTriangle` at an unrelated cost. Those modes are rendered without
      antialiasing — a visible degradation at the edges, **and not a colour error**:
      it does not accumulate and does not propagate. The deviation is not quantified,
      for want of a representative scene.
- [x] Exercised rather than asserted: three triangles at the same depth, emitted in a
      known order, depth disabled so that nothing sorts in our stead. The last emitted
      wins. A negative control checks that all three were indeed drawn, without which a
      renderer drawing only the last would pass.
- [ ] Shadows, water, particles and reflections — **blocked by the absent ROM**.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

Depth fighting in 16 bits is the main risk, and it does not manifest on a test scene:
it manifests far away, on a long track, in motion. It has to be actively sought, in the
conditions where it appears, rather than waited for.

## References

- E04-S06 — inventory of the render modes
- E04-S08 — comparison oracle
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — current handling of the shadows
