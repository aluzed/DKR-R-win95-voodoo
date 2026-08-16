# E00-S04 — Spike: cost of the recompiled RSP without SSE

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E00-S02 |
| **Blocks** | E00-S05, E03-S01, E03-S03 |

## State as of 2026-08-11 — measured, conclusion settled

Full results:
[`docs/research/rsp-audio-budget.md`](../../research/rsp-audio-budget.md).

| Quantity | Measurement |
|---|---|
| Scalar fallback in `librecomp` | **already exists**, selected automatically in 32-bit |
| The SIMD path's instruction set | **SSE4.1** — out of reach of any Pentium |
| Compiling the microcode in 32-bit without SSE | ✅ unmodified |
| Vector operation, SIMD host → SISD target | 2.22 ns → **410 ns** (185×) |
| Of which the scalar path's own penalty | **10.4×** (the rest is the machine, 17.7×) |
| The target's vector throughput | 2.44 M op/s against ~62.5 M/s for the RSP |
| **What the target reaches of the RSP** | **3.9 %** |

**The recompiled audio microcode cannot hold real time.** Even assuming the audio
consumes only 5 % of the RSP on console, it would cost 43 ms per frame on the target
— for a budget of 33.3 ms. At 20 %, five times the budget.

**Three of the ticket's questions are closed:**

1. The scalar fallback did not need writing — `rsp_vu.hpp` selects it as soon as the
   architecture is neither x86-64 nor arm64.
2. The SIMD path requires **SSE4.1**, not SSE2: it was out of reach of the entire
   Pentium range, and not of the Pentium II alone.
3. **Writing MMX would not save this path.** Four lanes instead of eight, with
   neither byte permutation nor 32-bit saturation: a gain of 3 to 4× would bring it
   to ~15 % of the RSP's throughput, still five times too slow. That is an effort not
   to embark on.

**Decision: [E03-S03](../E03-rsp/E03-S03-high-level-mixer-fallback.md) moves from
contingency to critical path.** The scalar path keeps a use — it runs the microcode
faithfully, outside real time, which makes it the natural **oracle** for validating
the high-level mixer.

## Context

DKR makes two uses of the RSP, and the project treats them very differently:

- the **F3DDKR graphics microcode** is not recompiled: it is interpreted at a high
  level by `f3ddkr_rt64.cpp`, which reads the display list and translates it. Good
  news — that is the cheap path;
- the **`aspMain` audio microcode** really is recompiled, instruction by
  instruction, into `runtime-recomp/RecompiledRSP/aspMain.cpp` (73 KB of generated
  C++) and runs on the host CPU.

The RSP is a vector processor: its instructions handle eight 16-bit integers at
once. `librecomp/rsp_vu_impl.hpp` very probably emulates them in SSE2 — absent from
the Pentium II, which has only MMX.

MMX is not a direct replacement: its registers are 64 bits, that is four 16-bit
lanes. Every RSP vector operation will need two of them. And MMX shares its registers
with the x87 stack, which imposes an `emms` at every transition to floating-point
code — the real cost therefore depends as much on the interleaving as on the
instructions themselves.

## Objective

To put a number on the cost of the recompiled audio microcode on the target, and to
decide whether the "recompiled microcode" path fits in the budget or whether a
high-level audio mixer should be preferred to it.

## Scope

**In:** `aspMain`, `rsp_vu_impl.hpp`, and the comparative cost of the three vector
emulation strategies.

**Out:** writing the chosen implementation (that is E03-S01) and the audio output
(E06-S03).

## Work

1. Read `librecomp/include/librecomp/rsp_vu_impl.hpp` and record precisely which
   instruction set it requires, and whether a portable scalar fallback already
   exists.
2. Count, in `RecompiledRSP/aspMain.cpp`, the calls to vector operations per type.
   The distribution counts for more than the total: a few operations always dominate
   an audio microcode (multiply-accumulate, saturations, permutations).
3. Write an isolated bench that runs `dkrAspMain` on an audio task captured from a
   real play session, in a loop, and measures the time per task.
4. Measure that bench in three configurations:
   - SSE2, 64-bit — the current reference;
   - portable scalar, 32-bit without SSE — the worst case;
   - MMX, 32-bit — the likely target, at least for the dominant operations recorded
     at step 2.
5. Relate the result to the real budget: DKR produces audio at a fixed rate; convert
   the time per task into a percentage of a 33.3 ms frame on a 400 MHz Pentium II,
   reusing E00-S03's normalisation factor.
6. Rule on the fallback: if the microcode exceeds its budget even in MMX, E03-S03
   (high-level mixer) moves from contingency to critical path.

## Acceptance criteria

- [ ] The instruction set `rsp_vu_impl.hpp` requires is established by reading the
      code, and the existence or absence of a scalar fallback is settled.
- [ ] The distribution of `aspMain`'s vector operations is counted.
- [ ] The bench measures the three configurations on a real audio task.
- [ ] The cost is expressed as a percentage of one frame's budget on the target.
- [ ] The document concludes: recompiled microcode kept, or high-level mixer — with
      the figure that motivates the conclusion.
- [ ] The result is carried into E00-S03's overall budget.

## Risks

The audio microcode is generated code and cannot be touched directly
(`docs/ARCHITECTURE.md`): every correction goes through the patch pipeline. If the
vector emulation has to change, it changes in `librecomp` through
`patches/n64-modern-runtime/`, not in the generated file.

## References

- `runtime-recomp/RecompiledRSP/aspMain.cpp` — 73 KB of recompiled audio microcode
- `runtime-recomp/rsp/aspMain.us.v77.toml` — the RSP recompilation's configuration
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — the graphics microcode, by contrast, is HLE
- `docs/F3DDKR.md`
