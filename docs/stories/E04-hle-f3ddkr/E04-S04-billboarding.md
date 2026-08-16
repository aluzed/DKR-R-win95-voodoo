# E04-S04 — Billboarding

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E04-S03 |
| **Blocks** | E09-S02 |

## Context

Billboarding — automatically orienting a quadrilateral to face the camera — is a
function Rare wired into its microcode, and it is one of F3DDKR's reasons to exist.
DKR uses it massively: trees, bushes, particles, scenery elements, and part of the
effects.

That makes it a visual item with a strong impact. A badly oriented billboard does not
produce a small inaccuracy: it produces a tree lying down, or invisible from a certain
angle. And since the game displays a great many of them, it is also a measurable
computation item.

`docs/F3DDKR.md` confirms that billboards are among the entities to which the modern
port attaches a semantic identity, which indicates that the current decoder handles
them explicitly — the reference behaviour is therefore legible in `f3ddkr_rt64.cpp`.

## Objective

To reproduce F3DDKR's microcode billboarding, with the same visual result as the
modern target.

## Scope

**In:** detecting the oriented primitives and computing their orientation.

**Out:** rendering them (E05) and the depth sort order (E04-S06).

## Work

1. Survey the exact behaviour in two independent sources: the current decoder
   (`f3ddkr_rt64.cpp`) and the microcode on the decomp's side (`extern/dkr-decomp`,
   `include/f3ddkr.h` documents the billboarding). A disagreement between the two is
   a piece of information not to be lost.
2. Identify the trigger: which state bit or which command puts the microcode in
   billboard mode. It is the easiest point to miss, and an over-broad detection would
   orient geometry that must not be.
3. Implement the orientation computation. The usual form consists of cancelling the
   model-view matrix's rotation, keeping only its translation and scale, but Rare's
   microcode may have its own convention — in particular about the axis preserved, a
   cylindrical billboard turning about the vertical not behaving like a spherical one.
4. Check the case of billboards nested in a matrix hierarchy: an oriented object
   attached to a moving object.
5. Compare visually against the modern target on reference scenes rich in billboards,
   turning the camera through 360° — it is the rotation that reveals the axis errors,
   not a fixed capture.
6. Measure the cost on a dense scene, and check that it stays proportionate to the
   number of billboards displayed.

## Acceptance criteria

- [ ] The billboard mode's trigger is identified and documented.
- [ ] The behaviour is cross-checked between the current decoder and the decomp, any
      disagreement being recorded.
- [ ] The billboards stay oriented towards the camera through a complete 360°
      rotation, compared against the modern target.
- [ ] The case of a billboard attached to a moving object is handled and tested.
- [ ] The cost is measured on a dense scene and entered in E08-S01's budget.
- [ ] No unconcerned geometry is oriented by mistake — verified on a scene with no
      billboard.

## Risks

A wrong axis convention produces a result that is correct head-on and wrong from the
side. The test must therefore be a continuous rotation, and the comparison must be
made at several angles, not on a single capture.

## References

- `docs/F3DDKR.md` — billboards among the entities with a semantic identity
- `runtime-recomp/src/game/f3ddkr_rt64.cpp`
- `extern/dkr-decomp` — `include/f3ddkr.h`
