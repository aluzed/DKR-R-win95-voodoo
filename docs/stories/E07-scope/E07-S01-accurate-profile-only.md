# E07-S01 — "Accurate" profile only

| | |
|---|---|
| **Epic** | E07 — Scope reduction |
| **Status** | REVIEW |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E00-S07 |
| **Blocks** | E06-S04, E06-S05, E08-S04 |

## Where it stands (3 October 2026) — removed from the Windows 95 build

**The Modern profile was already unreachable on this target.**
`presentation_profile()` starts at Accurate (`runtime_enhancements.cpp`), and
the only caller of `set_presentation_profile` is `runtime_ui.cpp`, the ImGui
overlay E07-S02 left out of the build. What remained was its machinery,
compiled and called for nobody:

- `presentation_identity.cpp`, 65 KB of source, linked into `DKRR.EXE`;
- fifteen hooks the recompilation policy places in the generated code --
  scene load, frame start, camera matrices, waves, object birth, death and
  rendering, task submission -- recording identities only the Modern profile
  reads.

**Removed from the Windows 95 build, not from the repository.** The modern
target still offers both profiles, and the recompilation policy is a property
of the ROM shared by both: removing the hooks from it would change the
generated code of the modern build too, which is this ticket's regression
reference. So the policy stays, and the Windows 95 build replaces
`presentation_identity.cpp` with `runtime_absent_presentation.cpp`, the same
arrangement as `runtime_absent_hooks.cpp` for netplay and custom tracks: each
hook supplied, doing what the Accurate profile needs of it and nothing else.
Read hook by hook, the full versions write no guest memory and no register;
two of them call into code the Accurate profile does use -- the HUD's authored
frame and the post-race frame gate -- and the replacement keeps exactly those
calls.

**What stays, and why.** `widescreen_policy.hpp`, `modern_camera_policy.hpp`
and `motion_steering_policy.hpp` are still included by `runtime_stubs.cpp`,
`runtime_enhancements.cpp` and `runtime_input.cpp`: with the Accurate profile
they return the authored values, and they are the same calls the modern build
makes in that profile. Removing them would mean editing shared sources to
reach the same answer. The RT64 patches serve the modern target only; the
Windows 95 build fetches no RT64. No test suite became moot: the portable
suites covering these policies (`PRESID`, `WIDE`, `CAMERA`, `MOTION`) test
header code `DKRR.EXE` still compiles.

**The gain, measured.** `DKRR.EXE` loses 76,160 bytes of code (`.text`
7,322,980 to 7,246,820) and 96,395 bytes on disk (9,582,471 to 9,486,076); 46
`presentation::` symbols down to 5. In a race the hooks were each under 0.1% of
the sampler's samples (`docs/stories/E08-perf/E08-S01-frame-budget-instrumentation.md`),
and the frame says the same: interleaved on the test machine, before, after,
before, after, each run to a race driven two minutes (`scripts/Drive-To-Race.sh`,
the renderer's last `[gfx] frame:` report, in the race):

| | before | after |
|---|---:|---:|
| first pair | 38,083 us | 37,692 us |
| second pair | 38,012 us | 37,643 us |

**0.38 ms a frame, 1%**, the same in both pairs. Memory: the 896 bytes of
`.bss` are measured; the identity maps' heap reservations (a thousand matrix
entries, a few hundred markers per buffer) are not, Windows 95 having no
per-process counter.

**The Accurate profile unchanged.** The four runs reach the race by the same
route and their logs hold the same kinds of line in the same numbers but for
one audio-cost and one idle report (a run three seconds shorter or longer);
no texture refused, no rejected list. The replacement hooks write nothing the
originals did not, so the display lists the game builds are the same.

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

Read for the Windows 95 build; the modern target keeps both profiles.

- [x] The Modern mode and all its dependencies are removed -- unreachable
      before, and its machinery no longer linked; the policy headers that
      answer "authored" in the Accurate profile stay, with the reason above.
- [x] The presentation identities and their recompilation hooks are removed --
      `presentation_identity.cpp` out of the build; the hooks supplied empty,
      the policy shared with the modern target left as it is.
- [x] The RT64 patches specific to interpolation are removed, or kept with a
      justification if the oracle depends on them -- the Windows 95 build
      fetches no RT64; the patches serve the modern target.
- [x] The test suites that have become moot are removed -- none had: those
      covering these policies test header code `DKRR.EXE` still compiles.
- [x] The gain is measured: code, binary, memory, CPU time per frame -- 76 KB
      of code, 94 KB of file, 0.38 ms a frame; the heap part of the memory is
      not measurable here.
- [x] The Accurate profile's behaviour is unchanged, verified by a before / after
      comparison -- four interleaved runs to a race.
- [x] The documentation no longer mentions the Modern mode -- the package's
      `README.TXT` never did; `docs/CONFIGURATION.md` names the variables
      that only the modern build reads as such.

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
