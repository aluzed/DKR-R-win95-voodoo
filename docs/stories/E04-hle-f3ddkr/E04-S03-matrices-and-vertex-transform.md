# E04-S03 — Matrix stack and vertex transformation

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E04-S02 |
| **Blocks** | E04-S04, E04-S05, E08-S03 |

## Context

On the N64, vertex transformation is done by the RSP. Here, it falls to the host CPU
— as on any 3dfx card, which transforms nothing. It is the port's heaviest graphics
computation item, and it falls entirely within the budget measured by E00-S03.

Three peculiarities of Rare's microcode complicate the exercise:

- the N64's matrices are in **16.16 fixed point**, stored as two separate halves —
  high parts then low parts. It is not an ordinary matrix format, and the conversion
  must be exact;
- F3DDKR addresses matrices and vertices through **relative offsets** supplied by the
  `DMAOffsets` command;
- Rare's vertices are in a compact format peculiar to the microcode, which
  `f3ddkr_rt64.cpp` translates today into a dedicated memory window
  (`0x7FE000..0x7FFFFF` of the snapshot, according to
  `docs/RENDER_SNAPSHOT_ARCHITECTURE.md`).

## Objective

To transform the vertices from object space to the screen coordinates the backend
expects, with sufficient precision and within budget.

## Scope

**In:** matrix stack, transformation, projection, perspective division, vertex
lighting.

**Out:** clipping (E04-S05) and billboarding (E04-S04), which fit into this pipeline
but are dealt with separately.

## Work

1. Implement the matrix stack: push, pop, load, multiply, with the depth DKR really
   uses — to be surveyed rather than generously assumed.
2. Implement the 16.16 fixed-point → floating-point conversion, respecting the layout
   in two halves. Test it by comparison against matrices captured from the modern
   target.
3. Settle the internal representation: x87 floating point, or fixed point. The
   Pentium II's x87 is decent and using it simplifies the code a great deal; fixed
   point may be faster on certain operations. Decide **on a measurement**, not on a
   preference, and record the figure.
4. Implement vertex transformation: model-view-projection matrix, perspective
   division, scaling to screen coordinates, computation of the depth value.
5. Produce directly the vertex format the backend expects (E04-S01): Glide wants
   screen coordinates, the reciprocal of the depth and the reciprocal of the
   homogeneous coordinate. Avoid any intermediate copy.
6. Implement per-vertex lighting as the microcode does it: DKR uses vertex colours
   and simple lighting. Survey the exact behaviour in the decomp rather than assume a
   standard model.
7. Measure the cost per vertex and per frame on the target, and enter it in E08-S01's
   budget. That figure will determine whether E08-S03 (MMX optimisation of the vertex
   path) is necessary.
8. Check the precision: compare the screen positions obtained against the modern
   target's on captured scenes, and set a justified tolerance.

## Acceptance criteria

- [x] The matrix stack reproduces the microcode's behaviour, with a depth
      **surveyed and not assumed**: `f3ddkr_rt64.cpp` bounds the index at 2 in
      `Matrix` as in `MoveWord`, hence three slots. Providing sixteen out of caution
      would cost memory on a machine that has none, and would mask a badly decoded
      command aiming at a slot that does not exist.
- [~] The 16.16 conversion in two halves is exact, tested on values **computed by
      hand** — integer alone, fraction alone, negative with a fraction, and just
      under unity. The case that breaks a naive conversion is covered: the fraction is
      not signed. **No captured matrices**: they require the ROM.
- [~] The floating-point / fixed-point choice is justified by a recorded measurement —
      **the x87 is measured, fixed point is not**. Writing a variant in haste would
      measure its own clumsiness rather than the technique, and the decision depends
      on the number of vertices per frame, which only the game gives.
      See [`docs/research/win95-vertex-cost.md`](../../research/win95-vertex-cost.md).
- [x] The transformed vertices are produced directly in the backend's format, with no
      copy — `dkr_render_vertex` is written in place, `oow` and `ooz` included.
- [ ] The screen positions match the modern target's — **blocked**, the comparison
      requiring captured scenes, hence the ROM.
- [x] The cost per vertex and per frame is measured on the target: **0.682 µs per
      vertex**, that is **24,322 vertices** in a 16.6 ms frame.
- [ ] Per-vertex lighting — **not done**. DKR carries vertex colours, and the decoder
      passes them on as they are; knowing whether the microcode applies anything else
      to them requires surveying the behaviour in the decomp, which is not in this
      repository.

## Risks

A precision gap shows immediately on screen, as wobbling geometry or as cracks
between adjacent polygons. The N64 worked in fixed point with precise rounding rules;
reproducing its behaviour in floating point introduces deviations that, in isolation,
are invisible, and that, accumulated over a chain of matrices, are no longer so. Step
8's tolerance must be measured on a whole level's geometry, not on a test cube.

## References

- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — `Matrix`, `Vertex`, `DMAOffsets`
  handlers
- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — vertex translation window
- `extern/dkr-decomp` — `include/f3ddkr.h`, the microcode's matrix stack
