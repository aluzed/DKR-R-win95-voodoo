# ADR 0004 - Repository strategy and reference oracle

- **Status**: accepted
- **Date**: 2026-08-12
- **Ticket**: [E00-S07](../stories/E00-scoping/E00-S07-adr-oracle-branch-strategy.md)

## Context

This repository is a fork of
[`ThatGuyMcd/DKR-R`](https://github.com/ThatGuyMcd/DKR-R), detached at commit
`e5d1bbb`. The Windows 95 / 3dfx port removes features, replaces whole
dependencies and aims at a machine upstream never envisaged. It had to be decided,
once, what stays buildable and how we will know that the Glide rendering is
*right*.

Two measurements changed the nature of the question, and they must be set down
before the decision.

**The repository is already structured for two targets.**
`runtime-recomp/CMakeLists.txt` carries the `DKR_RUNTIME_BUILD_RT64` option, `OFF`
by default, which propagates the `DKR_RUNTIME_HAS_RT64` symbol.
[E00-S01](../research/win95-blockers.md) verified what that switch really covers,
by following the nesting of the preprocessor directives rather than by searching
for patterns:

| Component | References outside the guard |
|---|---:|
| RT64 | **0** — `f3ddkr_rt64.hpp` contains nothing but forward declarations |
| Dear ImGui | **0** — `runtime_platform.cpp`'s 26 lines are guarded, `imgui.h` included |
| SDL2 | **4**, in two files |
| Texture packs, CRT overlay, Rice import | **0** — whole files excluded |

The separation between "modern path" and "portable core" is therefore not to be
invented: it exists, it is enforced by the build, and it leaves four references to
deal with.

**The tests are portable.** The 18 suites in `runtime-recomp/tests/` each test one
policy in a header, under `if(BUILD_TESTING)` and not under
`DKR_RUNTIME_BUILD_RT64`. None depends on SDL, on ImGui or on RT64; a single one —
`save_manager` — touches `std::filesystem`.

## Decision

### 1. The modern path is kept in the same tree, as an oracle

Of the ticket's three options, the second is retained: **keep RT64 and SDL2 behind
the existing CMake switch**, in the same tree.

The justification is not comfort, it is
[E09-S02](../stories/E09-qa/E09-S02-visual-comparison-harness.md). Comparing the
Glide rendering against a reference requires replaying **the same display-list
trace** in both backends. In a single tree that is one development binary and two
calls; across two branches it is two binaries, two source states to keep in step,
and a comparison one stops making after three weeks because it costs too much.

Removing the modern path would have simplified the code — but would have left, as
the only reference for correctness, a third-party N64 emulator. The project would
thereby lose its ability to tell "Glide renders badly" from "the F3DDKR decoder
decodes badly".

**The cost is accepted explicitly**: every change to the rendering interface will
have to be carried into both backends. That cost stays low as long as
[E04-S01](../stories/E04-hle-f3ddkr/E04-S01-render-backend-interface.md)'s split
holds — an interface receiving primitives already transformed.

**Until when.** The modern path is kept at least until the Glide rendering is
validated by E09-S02 over the whole set of reference scenes. After that the
question will be reopened, and not before: it is precisely during development that
the oracle serves.

### 2. The build switch

`DKR_RUNTIME_BUILD_RT64`, **default `OFF`**, propagated as
`DKR_RUNTIME_HAS_RT64`. It is confirmed sufficient for RT64 and ImGui, on the
strength of the survey above. The Windows 95 target never turns it on.

It is **not yet sufficient for SDL2**. Four references to bring under the guard,
as part of [E07-S03](../stories/E07-scope/E07-S03-sdl2-decoupling.md):

| Location | Nature |
|---|---|
| `runtime_input.cpp:571-572` | `SDL_GameController*` in `input::poll`'s public signature |
| `runtime_enhancements.cpp:10` | unconditional `#include <SDL.h>` |
| `runtime_enhancements.cpp:461,468` | `SDL_Window*`, `SDL_GetWindowSize` |

**A rule that follows from this ADR**: the core — everything that compiles with
`DKR_RUNTIME_BUILD_RT64=OFF` — references neither RT64, nor ImGui, nor SDL2.
[E01-S04](../stories/E01-build/E01-S04-pe-import-guard-rail.md)'s guard rail
checks it at the output, on the produced binary's import table.

### 3. Tracking upstream: frozen, with selective pick-up on report

Upstream (`ThatGuyMcd/DKR-R`) remains a modern 64-bit target; this fork descends
towards a 1995 machine. The two trajectories diverge, and merging mechanically
would make no sense.

**Decision: the base is frozen at commit `e5d1bbb`**, with no automatic tracking.
An upstream fix is taken only if it belongs to one of the two categories below,
and it is then taken by hand, as a documented `cherry-pick`:

- **a correctness fix in the game** — undefined behaviour, wrong computation,
  desynchronisation. Those fixes hold for every target, and on a 32-bit target
  without SSE the UB manifests differently: ignoring them would cost hours of
  debugging wrongly attributed to the port;
- **a fix in the recompilation pipeline** — N64Recomp, hook policy, generation. It
  touches the oracle itself.

Everything else — modern features, interface, texture packs — is ignored by
construction.

No upstream `remote` is configured, deliberately: taking a fix must be a conscious
act, not a mechanical `git pull`.

### 4. The boundary between patching and forking a dependency

[`docs/ARCHITECTURE.md`](../ARCHITECTURE.md)'s rule is **confirmed without
exception**: the worktrees `extern/rt64`, `extern/n64-modern-runtime`,
`extern/n64-modern-runtime/N64Recomp`, `RecompiledFuncs` and `RecompiledPatches`
are never modified directly. Every modification goes through
`patches/manifest.json`, with its checksum.

The port will demand heavy patches on `ultramodern` and `librecomp`. The boundary
beyond which one forks rather than patches:

> A dependency is forked when a patch can no longer be expressed as a **local and
> legible modification** of the upstream file — that is, when it rewrites a whole
> translation unit, or when its intent can no longer be described in a paragraph.

Concretely, and on the strength of E00-S01:

- **`ultramodern` stays patched.** Six files affected, 12 `std::thread` and
  5 `std::mutex`. That is within the outline.
- **`librecomp` stays patched** for the RDRAM constants (ADR 0003 / E00-S06) and
  the replacement of `std::filesystem`, which are localised substitutions.
- **The first candidate for a fork is `librecomp`'s mod system**, which
  concentrates two thirds of the 101 uses of `std::filesystem` for a feature the
  Win95 target does not need. If neutralising it by patch exceeds a hundred lines,
  we fork.

The `0002-portable-128-bit-multiply-for-32-bit-targets` patch illustrates the
right form: an `#elif` branch the modern target never compiles.

### 5. Sorting the test suites: all 18 are kept

None is removed. The sorting bears on the **target each one runs on**, which only
makes sense because point 1 keeps the modern path.

**Core — portable, run on both targets (12)**

| Suite | Remark |
|---|---|
| `dkr_save_codec` | pure logic, save codec |
| `save_manager` | **blocked on Win95** until E01-S03 (`std::filesystem`) |
| `presentation_identity` | pure logic |
| `presentation_policy` | pure logic |
| `vi_presentation_policy` | pure logic — its mentions of RT64 are comments |
| `audio_equalizer` | pure logic |
| `audio_mix_policy` | pure logic |
| `magic_code_policy` | pure logic |
| `quick_restart_policy` | pure logic |
| `intro_tail_policy` | pure logic |
| `character_select_animation_policy` | pure logic |
| `character_select_music_policy` | pure logic |

These twelve are **a net gain**: they compile for i686 without SSE and constitute
the first test suite runnable on the target
([E09-S03](../stories/E09-qa/E09-S03-portable-test-suite.md)) without writing
anything.

**Modern path — kept, run on the host only (6)**

| Suite | Why it does not apply to Win95 |
|---|---|
| `widescreen_policy` | 21:9 makes no sense at 640×480 |
| `modern_camera_policy` | modern camera, outside the "Accurate" profile (E07-S01) |
| `interpolation_state_policy` | frame interpolation specific to RT64 |
| `renderer_snapshot` | exposes an RDRAM image **to RT64**, by construction |
| `motion_steering_policy` | gyroscopic steering, no gyro controller in 1995 |
| `rice_texture_pack_policy` | texture packs, out of scope (E07-S02) |

They stay valid and must keep passing: they protect the oracle, and a broken
oracle is worth nothing.

## Consequences

- **E07-S01** ("Accurate" profile only) does not remove the modern policies, it
  excludes them from the Win95 target. The ticket's wording must be adjusted.
- **E09-S03** (portable test suite) inherits twelve suites already written.
- **E09-S02** becomes feasible as designed: one tree, two backends, one trace.
- The maintenance cost of the two backends is accepted, bounded in time by
  E09-S02's validation.
- The risk taken on: if E04-S01's split degrades, the cost of the two backends
  grows unnoticed. That is to be watched when the rendering interface changes.

## References

- [`docs/research/win95-blockers.md`](../research/win95-blockers.md) — the survey of the guards
- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — protected boundaries
- `runtime-recomp/CMakeLists.txt:37` — `DKR_RUNTIME_BUILD_RT64`
- `runtime-recomp/CMakeLists.txt:178,291` — propagation as `DKR_RUNTIME_HAS_RT64`
- `../../Diddy-Kong-Racing/docs/adr/0003-strategie-fork.md` — the same arbitration on the native port's side
