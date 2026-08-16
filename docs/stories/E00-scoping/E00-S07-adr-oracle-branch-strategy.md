# E00-S07 — ADR: repository strategy and reference oracle

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | REVIEW |
| **Priority** | P1 |
| **Estimate** | S |
| **Depends on** | — |
| **Blocks** | E07-S01, E09-S02, E09-S03 |

## State as of 2026-08-12 — settled

ADR written:
[`docs/adr/0004-repository-strategy.md`](../../adr/0004-repository-strategy.md).

| Point | Decision |
|---|---|
| The modern path | **kept in the same tree**, as an oracle, until E09-S02's validation |
| The switch | `DKR_RUNTIME_BUILD_RT64`, default `OFF` — **verified sufficient** for RT64 and ImGui, not yet for SDL2 (4 references) |
| Upstream | **frozen** at commit `e5d1bbb` of `ThatGuyMcd/DKR-R`; manual pick-up of correctness and pipeline fixes alone |
| Patch / fork | the `patches/manifest.json` pipeline **confirmed without exception**; the boundary written; the first fork candidate named (`librecomp`'s mod system) |
| Tests | **all 18 suites kept** — 12 portable to both targets, 6 reserved for the host |

Two findings carried the decision, both from the survey of the preprocessor
directives:

- the "modern path / portable core" separation **already exists** and is enforced by
  the build — 0 RT64 or ImGui references outside a guard;
- the 18 test suites are **under `BUILD_TESTING` and not under RT64**, and none
  depends on SDL, on ImGui or on RT64. Twelve compile for the target without
  writing anything, which gives E09-S03 a free base.

A consequence to carry through: **E07-S01** does not remove the modern policies, it
excludes them from the Win95 target. The ticket must be reworded.

## Context

This repository is a fork of DKR-R. The Win95 port will remove features
(widescreen, interpolation, texture packs, the ImGui overlay), replace whole
dependencies (SDL2, RT64) and lower the language standard. Those changes are
incompatible with upstream: there is no way back.

Two distinct things must therefore be decided, once and for all:

1. **The fate of the modern port in this repository.** Removing it simplifies the
   code enormously. Keeping it costs maintenance — but supplies the only executable
   reference that tells us whether the Glide rendering is *right*. Without it, the
   only reference is a third-party N64 emulator or the console, and image-by-image
   comparison becomes far heavier.
2. **Tracking upstream.** Take DKR-R's fixes, or freeze.

The neighbouring native port (`/var/www/Diddy-Kong-Racing`) met exactly this
question and settled it in its ADR 0003: a dedicated fork, matching abandoned as a
delivery constraint, but **the reference build kept as a test oracle**. The
justification there is explicit — breaking the oracle is losing the only means of
knowing whether the port is correct.

## Objective

To write `docs/adr/0004-repository-strategy.md`: what is kept, what is removed, what
stays buildable, and how the oracle is used.

## Scope

**In:** the decision, and the build structure it imposes.

**Out:** implementing the scope reduction (E07).

## Work

1. Decide on keeping the RT64 / SDL2 path. Three honest options:
   - **remove** — minimal code, no executable oracle any more;
   - **keep in the same tree** behind a CMake switch, both backends implementing
     `ultramodern::renderer::RendererContext` — that interface already exists and
     carries three potential implementations;
   - **keep on a separate branch** — a clean tree, but guaranteed drift and a more
     laborious comparison.

   The second option is the only one that makes E09-S02 genuinely practicable,
   because it allows *the same display-list trace* to be replayed in both backends
   from a single development binary.
2. Define the build switch and its default. The Win95 target must obviously never
   attempt to compile RT64: `DKR_RUNTIME_BUILD_RT64` already exists and is `OFF` by
   default — check that it suffices and that no code in `src/game/` references RT64
   unconditionally.
3. Decide on tracking upstream: a frozen version of DKR-R as the base, and a
   procedure for taking fixes selectively, or a complete freeze.
4. Rule on the patch pipeline. The repository's rule is firm
   (`docs/ARCHITECTURE.md`): never modify a dependency worktree directly. The port
   will demand heavy patches on `ultramodern` and `librecomp` — confirm that
   `patches/manifest.json` stays the only route, and fix the boundary beyond which a
   dependency is forked rather than patched.
5. Decide the fate of the existing tests. The 18 suites in `runtime-recomp/tests/`
   bear on modern policies; some become moot with the "Accurate" profile alone,
   others stay valid (save codec, audio equaliser, save manager). Sort them.

## Acceptance criteria

- [ ] `docs/adr/0004-repository-strategy.md` settles the five points.
- [ ] The decision on the oracle is justified by its concrete use in E09-S02.
- [ ] The build switch and its default are named, and the check that it suffices to
      exclude RT64 is done.
- [ ] The patch / fork boundary for the dependencies is written.
- [ ] The sorting of the existing test suites is done, suite by suite.

## Risks

Keeping two backends doubles the cost of every change to the rendering interface.
That cost is real and must be accepted knowingly, not endured: if the ADR retains
them, it must say until when.

## References

- `docs/ARCHITECTURE.md` — protected boundaries, patch pipeline
- `runtime-recomp/CMakeLists.txt:36-37` — `DKR_RUNTIME_BUILD_RT64`
- `runtime-recomp/src/game/null_renderer.hpp` — a possible third implementation
- `../../Diddy-Kong-Racing/docs/adr/0003-strategie-fork.md` — the same arbitration,
  already settled on the native port's side
