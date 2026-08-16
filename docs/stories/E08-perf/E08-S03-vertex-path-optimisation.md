# E08-S03 — Optimising the vertex path

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | L |
| **Depends on** | E08-S01, E04-S03, E04-S05 |
| **Blocks** | — |

## Context

Vertex transformation is work the N64 entrusted to the RSP and that here falls to the
host processor. It is regular computation, in volume, on contiguous data — exactly the
profile that lends itself to optimisation.

Three levers, in the usual order of profitability:

1. **Not computing.** Early rejection by bounding volume (E04-S05) is the cheapest
   gain: a rejected object is an object none of whose vertices is transformed. It is
   almost always the most profitable lever, and it is often neglected in favour of the
   next.
2. **Computing better.** The choice between x87 and fixed point, settled in E04-S03,
   may be re-examined in the light of the real profile. The Pentium II's x87 has a
   notable latency and a constrained register stack; MMX offers 16-bit integers in
   parallel, which suits fixed point.
3. **Computing less often.** A level's static geometry is transformed every frame
   whereas only the view matrix changes; there may be invariants to exploit, provided
   one checks that they really hold.

A point of vigilance: MMX shares its registers with the x87 stack (see E03-S01), and
mixing the two in the vertex pipeline imposes expensive transitions. If MMX is
retained, it must cover a whole block, not a few isolated operations.

## Objective

To bring the vertex path back within its budget allocation, measurement in hand.

## Scope

**In:** transformation, clipping, preparing the vertices for Glide.

**Out:** the recompiled code (E08-S02) and the Glide backend on the card's side.

## Work

1. Establish the vertex path's detailed profile from E08-S01: the share of
   transformation, of clipping, of rejection, of preparation.
2. Work on rejection first. Measure how many vertices are transformed for nothing —
   that is, belonging to geometry that ends up invisible. If that figure is high, all
   the rest of the ticket is secondary.
3. Optimise the transformation loop: data layout in memory, unrolling, prefetching if
   the architecture allows it. On a Pentium II, data layout often weighs more than the
   number of instructions.
4. Evaluate MMX for fixed point, covering a whole block of the pipeline to avoid
   transitions with x87. Measure before writing a lot of code.
5. Eliminate the copies. The vertex must be produced directly in Glide's format
   (E04-S01); check that no intermediate conversion remains, and that the vertex buffer
   is reused rather than reallocated.
6. Explore drawing through vertex arrays rather than triangle by triangle, if the
   retained version of Glide allows it: it reduces the number of calls, whose unit cost
   is not negligible.
7. Measure the gain at each step and enter it in the budget. An optimisation whose gain
   is not measured is a complication.
8. Check for visual regressions after each change, by image comparison (E09-S02). An
   optimisation of geometric computation that moves a position by a pixel must show.

## Acceptance criteria

- [ ] The vertex path's detailed profile is established.
- [ ] The number of vertices transformed needlessly is measured, and rejection worked on
      first.
- [ ] Every optimisation is measured separately.
- [ ] MMX is used only if the measurement justifies it, and on whole blocks.
- [ ] No copy and no reallocation per frame in the vertex path.
- [ ] Drawing through vertex arrays is evaluated.
- [ ] The vertex path fits within its budget allocation.
- [ ] No visual regression after optimisation, verified by image comparison.

## Risks

Optimising geometric code easily introduces precision deviations. A change in the order
of floating-point operations, a different rounding in fixed point, and the geometry
starts to wobble. Checking by image comparison after each step is not an excessive
precaution: it is what allows a regression to be attributed to the optimisation that
caused it, rather than to the whole.

## References

- E04-S03 — transformation, floating-point / fixed-point choice
- E04-S05 — clipping and rejection
- E03-S01 — MMX and transitions with x87
- E08-S01 — profile and budget
