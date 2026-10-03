# E04-S04 — Billboarding

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | REVIEW |
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

## Where it stands (3 October 2026)

`platform/render/f3ddkr.c` implements it (`cmd_vertex`, "Billboarding"), and
the menus and races have exercised it since August. What the ticket asked to be
established had not been written down; it is below.

**The trigger.** `G_MW_BILLBOARD`, MoveWord index `0x02`, bit 0 of `w1`
(`gDkrEnableBillboard` / `gDkrDisableBillboard` in the decomp's
`include/f3ddkr.h`; `docs/research/f3ddkr-commands.md`). While it is set, a
loaded vertex's clip coordinates have vertex 0's added to them, after the
transformation and before the division by w.

**Three sources, one disagreement.**

| | decomp header | RT64 decoder (`f3ddkr_rt64.cpp`) | this port (`f3ddkr.c`) |
|---|---|---|---|
| which loads get the anchor | every load while it is set | appended loads only | every vertex but index 0 |
| where an appended load goes | after the last flag-0 load | index 1 when billboarding | after the last flag-0 load |
| how | clip coordinates added | anchor added to the matrix's translation row | clip coordinates added |

Adding the anchor to the translation row is the same sum for a vertex with
w = 1, which every DKR vertex is. The rows differ only for a flag-0 load made
while billboarding is on, or an anchor load of more than one vertex. **Neither
happens in what the game sends**: replayed with a one-line probe in
`cmd_vertex` (a copy outside the tree, printing each load made with the flag
set), the 66 captures of the corpus hold 1,021 such loads, in 47 of them, and
every one is an appended load at index 1 with vertex 0 loaded -- the decomp's
recipe, one anchor then the sprite. Where the three sources disagree, the game
never goes.

**No other geometry moves.** The addition is guarded by the flag; the 19
captures with no billboard load replay to their reference counts and images
like the rest (`tools/render/check-corpus.sh`), and the card's frames agree
with this decoder's oracle (E05).

Not done: the 360° rotation and the comparison with the modern target (no
modern build on this machine, as for E04-S03 and E04-S05), a billboard on a
moving object tested as such, and the cost measured on its own -- four
additions per vertex, too small for the sampler to separate from
`dkr_transform_to_clip`.

## Acceptance criteria

- [x] The billboard mode's trigger is identified and documented -- above.
- [x] The behaviour is cross-checked between the current decoder and the decomp, any
      disagreement being recorded -- above, with the RT64 decoder as the third
      source; the disagreement is outside anything the corpus contains.
- [ ] The billboards stay oriented towards the camera through a complete 360°
      rotation, compared against the modern target.
- [ ] The case of a billboard attached to a moving object is handled and tested.
- [ ] The cost is measured on a dense scene and entered in E08-S01's budget.
- [x] No unconcerned geometry is oriented by mistake — verified on a scene with no
      billboard: 19 captures without one, replayed against their references.

## Risks

A wrong axis convention produces a result that is correct head-on and wrong from the
side. The test must therefore be a continuous rotation, and the comparison must be
made at several angles, not on a single capture.

## References

- `docs/F3DDKR.md` — billboards among the entities with a semantic identity
- `runtime-recomp/src/game/f3ddkr_rt64.cpp`
- `extern/dkr-decomp` — `include/f3ddkr.h`
