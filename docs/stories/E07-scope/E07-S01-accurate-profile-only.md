# E07-S01 — "Accurate" profile only

| | |
|---|---|
| **Epic** | E07 — Scope reduction |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E00-S07 |
| **Blocks** | E06-S04, E06-S05, E08-S04 |

## Context

DKR-R offers two profiles. **Accurate** reproduces the console: 4:3, 30 frames per
second, the original field of view, draw distance, level of detail and audio mixing.
**Modern** adds widescreen, interpolation towards high refresh rates, a widened field
of view, an extended scenery distance and anisotropic filtering.

On a machine that will struggle to hold 30 frames per second at 640 × 480, the Modern
mode makes no sense. It costs, on the other hand, a great deal of complexity:
`presentation_identity.cpp` is 39 KB and exists only to attach stable semantic
identities to moving objects so that RT64 can interpolate between two frames. All that
machinery — identity sidecar, hooks at objects' birth and death, `TaskIdentityScope`,
workload matching — disappears with the Modern mode.

It is the largest simplification available in this project, and it lightens the code,
the memory and the CPU all at once.

## Objective

To keep only the Accurate profile, and to remove the machinery that existed only for
the Modern mode.

## Scope

**In:** removing the Modern mode and its dependencies.

**Out:** ImGui, the texture packs and the telemetry (E07-S02).

## Work

1. Establish the exact list of what disappears, from `presentation_policy.hpp` and the
   boundary described in `docs/ARCHITECTURE.md`: widescreen, interpolation, field of
   view, scenery distance, vehicle level of detail, anisotropic filtering, modern
   camera, gyroscope.
2. Remove the presentation identities. `presentation_identity.cpp` (39 KB) and
   `presentation_identity.hpp` (17 KB) go, as do the patch pipeline's hooks that feed
   them — scene load, object birth and release, the `render_object` boundary. Those
   hooks are in the recompilation policy, not in the project's code: removing them also
   lightens the generated code.
3. Remove the interpolation: `interpolation_state_policy.hpp`, and the RT64 patches
   that serve it (`0001-allow-skip-buffering-interpolation-targets`,
   `0002-count-interpolated-presentations`). Those patches concern only the modern
   target — check before removing them whether the oracle depends on them (E00-S07).
4. Remove `widescreen_policy.hpp`, `modern_camera_policy.hpp`,
   `motion_steering_policy.hpp`.
5. Simplify `renderer_snapshot` and the graphics task queue's depth, in accordance with
   E00-S06's decision: without interpolation, there is no longer any need to match two
   frames.
6. Sort out the corresponding test suites: `interpolation_state_policy_tests`,
   `presentation_identity_tests`, `widescreen_policy_tests`,
   `modern_camera_policy_tests`, `motion_steering_policy_tests`. They go with the code
   they cover.
7. Measure the gain: lines of code, binary size, memory, and CPU time per frame. The
   last figure is the most interesting — the identity hooks ran for every object
   rendered.
8. Check that the Accurate profile's behaviour is strictly unchanged. It is the
   project's regression reference (`docs/ARCHITECTURE.md`) and nothing must move.

## Acceptance criteria

- [ ] The Modern mode and all its dependencies are removed.
- [ ] The presentation identities and their recompilation hooks are removed.
- [ ] The RT64 patches specific to interpolation are removed, or kept with a
      justification if the oracle depends on them.
- [ ] The test suites that have become moot are removed.
- [ ] The gain is measured: code, binary, memory, CPU time per frame.
- [ ] The Accurate profile's behaviour is unchanged, verified by a before / after
      comparison.
- [ ] The documentation no longer mentions the Modern mode.

## Risks

Some of these policies may be more entangled in the game's code than they appear: the
identity hooks are in the recompilation policy, and removing them changes the generated
code. Check after regeneration that the game behaves identically, rather than assuming
a removal is neutral.

## References

- `docs/ARCHITECTURE.md` — boundary between the profiles, Accurate as the regression
  reference
- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — semantic identity machinery
- `runtime-recomp/src/game/presentation_identity.{hpp,cpp}`,
  `interpolation_state_policy.hpp`, `presentation_policy.hpp`
- `runtime-recomp/dkr.us.v77.recomp-policy.json` — hooks to remove
