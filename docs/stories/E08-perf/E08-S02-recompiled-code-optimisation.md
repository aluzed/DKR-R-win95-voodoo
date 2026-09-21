# E08-S02 — Optimising the recompiled code

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | L |
| **Depends on** | E08-S01, E01-S05 |
| **Blocks** | — |

## Context

The recompiled code is the budget's largest item, and it is also the hardest to
optimise: it is generated, one does not edit it, and it translates the VR4300's
instructions faithfully including when that fidelity is expensive.

The levers available, from the least risky to the most:

1. **The compiler's options.** The generated code is very repetitive; the choice of
   optimisation level, of inlining strategy and of scheduling for the Pentium II can
   give a notable gain for no risk.
2. **Code layout.** A binary of several tens of megabytes on a machine whose
   instruction cache is counted in kilobytes: grouping the hot functions improves
   locality, and that is often where the largest gain is found on this class of
   machine.
3. **The recompilation policy.** `dkr.us.v77.recomp-policy.json` drives the generation.
   Some of N64Recomp's options change the code produced; examine them.
4. **Replacing functions.** The hook pipeline allows a game function to be replaced by
   a native implementation. For the rare very hot and purely computational functions,
   it is the most powerful lever — and the one that most endangers fidelity, since it
   substitutes hand-written code for translated code.

The order matters: the first three levers do not change the game's behaviour, the
fourth does.

5. **The guest register's width.** Added 21 September 2026, after the fact, because
   none of the four above covers it. N64Recomp declares `typedef uint64_t gpr`
   unconditionally, so every emitted instruction carries a 64-bit register on a
   32-bit machine. DKR uses that width in thirteen operations across three
   functions. Narrowing it removes 30.5% of the emitted x86 instructions and 31.5%
   of the instructions that reference memory — larger than any of the four levers
   above, and it is not a compiler setting but a change to the recompiler's type
   model. The measurement, the census that found the three functions, and their
   hand-written wide paths are in `docs/research/cpu-budget.md` and reproduced by
   `scripts/Measure-Narrow-Gpr.sh`.

   What remains is plumbing rather than semantics: a shadowed header for the 32-bit
   target, the three functions entered in the recomp policy's `stubs` list with
   their wide implementations supplied, and a `recomp_context` that does not have to
   agree with the 64-bit modern target. Until that is done there is no frame time
   for it, only an instruction count.

## Objective

To reduce the recompiled code's cost, strictly preserving the game's behaviour.

## Scope

**In:** compilation options, layout, recompilation policy, targeted function
replacement.

**Out:** the graphics path (E08-S03) and memory (E08-S04).

## Work

1. Identify the hot functions from E08-S01's export, on a real play session and not on
   a synthetic loop.
2. Explore the compiler's options, measuring each variant. On this architecture,
   optimising for size may beat optimising for speed, because the cache is the limiting
   factor — that is counter-intuitive and it is measured.
3. Work on code layout: group the hot functions. If the retained toolchain allows it,
   profile-guided optimisation is the most direct way of getting there.
4. Examine the recompilation policy's options and measure their effect.
5. For the hottest functions, evaluate native replacement. The decomp
   (`extern/dkr-decomp`) supplies the original C, which makes the exercise far less
   risky than a rewrite: one compiles the original source rather than imitating its
   behaviour. Beware all the same — the neighbouring native port discovered that this
   C, under `#ifdef NON_MATCHING`, **had never been compiled by any target** and
   carried five outright defects. It is to be checked, not trusted.
6. For each replacement, prove the equivalence by comparing output over a large sample
   of inputs, against the recompiled version.
7. Measure the cumulative gain and report it to E08-S01's budget.
8. Check the game for regressions after each change: a complete play session, not just
   the startup.

## Acceptance criteria

- [ ] The hot functions are identified on a real play session.
- [ ] Every compilation option is measured, not assumed.
- [ ] The effect of code layout is measured separately.
- [ ] Any native replacement is proved equivalent by comparing output over a large
      sample.
- [ ] The cumulative gain is measured and reported to the budget.
- [ ] The game behaves identically, verified by a complete session.
- [ ] No native replacement is made without a prior measurement proving the function is
      hot.
- [x] The three functions that need a 64-bit general register are identified by a
      census of the emitted opcodes, and each is proved equivalent at both widths —
      `tools/cpu-budget/wide_register_paths.h`, `tools/cpu-budget/narrow_gpr_test.c`.
- [ ] The narrowed register is built and linked, and its gain measured as a frame
      time rather than an instruction count.

## Risks

Replacing functions is the most tempting and the most dangerous lever: it substitutes
hand-written code for faithfully translated code, and a difference in behaviour may
manifest only in a rare game situation. Use it only on functions the profile proves
matter, and never without proof of equivalence.

## Risks, added for the fifth lever

Narrowing the register is uniform and therefore cannot be applied to part of the
game: every function gets it. Three need a wide path and have one; the argument that
there is no fourth rests on a census of the opcodes the recompiler prints in its own
comments, which is only as good as that printing. `-Wshift-count-overflow` finds four
of the thirteen sites by itself and is worth keeping on for that reason, but it will
not find the other nine — `atan2s` in particular compiles clean and then returns the
wrong angle only when a coordinate difference exceeds 2^21, which is the failure that
survives a play-test.

## References

- `docs/research/cpu-budget.md` — the width measurement and the census
- `scripts/Measure-Narrow-Gpr.sh` — reproduces both builds and the comparison
- `runtime-recomp/dkr.us.v77.recomp-policy.json`
- `extern/dkr-decomp` — original C source
- `../../Diddy-Kong-Racing/docs/stories/E01-build/E01-S04-porter-hasm-en-c.md` —
  five defects found in the decomp's `NON_MATCHING` C
- E08-S01 — profile and budget
