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

## Risks

Replacing functions is the most tempting and the most dangerous lever: it substitutes
hand-written code for faithfully translated code, and a difference in behaviour may
manifest only in a rare game situation. Use it only on functions the profile proves
matter, and never without proof of equivalence.

## References

- `runtime-recomp/dkr.us.v77.recomp-policy.json`
- `extern/dkr-decomp` — original C source
- `../../Diddy-Kong-Racing/docs/stories/E01-build/E01-S04-porter-hasm-en-c.md` —
  five defects found in the decomp's `NON_MATCHING` C
- E08-S01 — profile and budget
