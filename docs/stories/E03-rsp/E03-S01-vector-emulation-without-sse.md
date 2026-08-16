# E03-S01 — Emulating the RSP's vector unit without SSE

| | |
|---|---|
| **Epic** | E03 — RSP on x86 without SSE |
| **Status** | TODO |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E00-S04, E01-S05 |
| **Blocks** | E03-S02 |

## State as of 2026-08-11 — largely obsolete

[E00-S04](../E00-scoping/E00-S04-spike-rsp-cost-without-sse.md) measured what this
ticket was to make possible, and the result takes away most of its reason to exist:

- **the scalar fallback already exists** in `librecomp` and is selected
  automatically in 32 bits — steps 1 to 3 are moot;
- **an MMX optimisation would not save the path**: four lanes instead of eight would
  bring us to ~15 % of the RSP's throughput, five times too slow. Steps 4 to 7 would
  be wasted effort.

What survives: the scalar path is
[E03-S03](E03-S03-high-level-mixer-fallback.md)'s **oracle**, and it already works.
This ticket ought to be closed, or reduced to validating that oracle.

## Context

`RecompiledRSP/aspMain.cpp` includes `librecomp/rsp_vu_impl.hpp`, which emulates the
RSP's vector unit. That unit processes eight signed 16-bit integers per instruction,
with a 48-bit accumulator and saturations — a natural match for SSE2, whose registers
are 128 bits, that is, exactly eight 16-bit lanes.

The Pentium II does not have SSE2. It has MMX: 64-bit registers, that is, four 16-bit
lanes. Each RSP vector operation will therefore require two MMX operations, plus
handling the carries and the saturations on both halves.

Two peculiarities of MMX weigh on the design:

- its registers are **shared with the x87 stack**. Any transition between MMX code
  and floating-point code requires an `emms`, whose cost is significant. The split
  must therefore group the vector work rather than interleave it;
- the RSP's 48-bit accumulator has no MMX equivalent, and reproducing it exactly is
  the delicate part of the exercise.

E00-S04 measured what each strategy costs. This ticket implements the one that was
retained.

## Objective

To supply an implementation of the RSP's vector unit that compiles without SSE,
produces bit-for-bit identical results to the reference, and holds E00-S04's budget.

## Scope

**In:** the vector emulation and its validation.

**Out:** the audio microcode itself (E03-S02) and the audio output (E06-S03).

## Work

1. Write the **portable scalar** implementation first, without MMX. It will be slow,
   and that is precisely its interest: it is simple, obviously correct, and it serves
   as the oracle for the optimised version. It is also the fallback if MMX turns out
   to be a problem.
2. Validate that scalar version against the reference SSE2 version: replay captured
   audio tasks and compare the output buffers bit for bit. A difference, even of one
   quantisation unit, is a defect.
3. Write the per-operation tests: for each vector instruction used by `aspMain`
   (surveyed and counted in E00-S04), a test covering the nominal cases, the positive
   and negative saturations, and the accumulator overflows. It is at the boundaries
   that these implementations go wrong.
4. Optimise in MMX the dominant operations surveyed in E00-S04 — not all of them. The
   rare operations stay scalar: optimising them costs time and risk for an
   unmeasurable gain.
5. Handle the MMX / x87 transitions: place the `emms` at the right boundaries, check
   that no floating-point code runs with an MMX state active. It is a classic source
   of silent corruption of floating-point results.
6. Revalidate the MMX version against the scalar version, bit for bit, on the same
   set of captured tasks.
7. Measure the real gain and confront it with E00-S04's budget.
8. Deliver the whole as a `patches/n64-modern-runtime/` patch, never by modifying the
   worktree directly.

## Acceptance criteria

- [ ] The scalar implementation produces output bit-for-bit identical to the SSE2
      reference, on at least ten distinct captured audio tasks.
- [ ] Every vector operation used by `aspMain` has a test covering nominal,
      saturations and accumulator overflow.
- [ ] The MMX implementation produces output bit-for-bit identical to the scalar
      version.
- [ ] The MMX / x87 transitions are handled, and a test checks it by deliberately
      interleaving floating-point code.
- [ ] The cost is measured and confronted with E00-S04's budget.
- [ ] The delivery is a patch under `patches/`, and the modern target goes on using
      the SSE2 path with no regression.
- [ ] The choice of which operations are optimised is justified by the measured
      distribution.

## Risks

Writing SIMD by hand is the ground most propitious to silent errors: a saturation
shifted by one produces a slightly wrong sound, which nobody notices for a long time.
The discipline of bit-for-bit comparison against a simple scalar implementation is
non-negotiable.

## References

- `runtime-recomp/RecompiledRSP/aspMain.cpp:1-2`
- `librecomp/include/librecomp/rsp_vu_impl.hpp` (in the prepared worktree)
- E00-S04 — distribution of the operations and budget
