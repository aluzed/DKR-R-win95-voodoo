# Cost of the RSP audio microcode on the target machine

Measurements from
[E00-S04](../stories/E00-scoping/E00-S04-spike-rsp-cost-without-sse.md).
Date: 2026-08-11.

## Summary

| Quantity | Measurement |
|---|---|
| Scalar fallback in `librecomp` | **already exists**, selected automatically in 32-bit |
| Instruction set the SIMD path requires | **SSE4.1** — out of reach of any Pentium |
| Compiling the microcode in 32-bit without SSE | ✅ unmodified, with no SSE instruction |
| Cost of one vector operation, SIMD host | **2.22 ns** (weighted mean) |
| Cost of one vector operation, SISD target | **410 ns** (weighted mean) |
| Penalty of the scalar path alone | **10.4×** |
| The target's vector throughput | **2.44 M op/s** |
| The real RSP's vector throughput | ~62.5 M op/s |
| **What the target reaches of the RSP** | **3.9 %** |

**Conclusion: the recompiled audio microcode cannot hold real time on the
target.** [E03-S03](../stories/E03-rsp/E03-S03-high-level-mixer-fallback.md) — the
high-level audio mixer — stops being a contingency and becomes necessary.

## The scalar fallback already existed

The ticket's step 1 asked whether `librecomp/rsp_vu_impl.hpp` offers an
alternative to SIMD. The answer is in `rsp_vu.hpp`:

```c
#if defined(__x86_64__) || defined(_M_X64)
#define ARCHITECTURE_SUPPORTS_SSE4_1 1
#include <nmmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ARCHITECTURE_SUPPORTS_SSE4_1 1
#include <sse2neon.h>
#endif

namespace Accuracy { namespace RSP {
#if ARCHITECTURE_SUPPORTS_SSE4_1
    constexpr bool SISD = false;  constexpr bool SIMD = true;
#else
    constexpr bool SISD = true;   constexpr bool SIMD = false;
#endif
}}
```

On **32-bit** x86, neither architecture condition is true: the macro stays
undefined and `Accuracy::RSP::SISD` is `true`. The scalar path — a loop over the
eight 16-bit lanes — is therefore selected **automatically**, without a line to
write. `aspMain.cpp` compiles for the Pentium II without SSE at the first attempt,
and the disassembly contains no SSE instruction.

A detail that closes one of the ticket's questions: the vector path requires
**SSE4.1** (`_mm_shuffle_epi8`, `<nmmintrin.h>`), not merely SSE2. It was out of
reach of the entire Pentium range, not only the Pentium II. The question "MMX or
scalar" therefore never arose as a choice between two existing implementations:
there has only ever been the scalar one.

## Profile of the microcode

`aspMain.cpp` counts **1,061 microcode instructions**, of which **184 are vector**
(17 %). Distribution:

| Operation | Occurrences | | Operation | Occurrences |
|---|---:|---|---|---:|
| `vmadh` | 33 | | `vmadm` | 6 |
| `vmulf` | 26 | | `vmudm` | 5 |
| `vxor` | 24 | | `vaddc` | 5 |
| `vmadn` | 17 | | `vmudl` | 4 |
| `vmacf` | 14 | | `vge` | 4 |
| `vadd` | 13 | | `vcl` | 4 |
| `vmudn` | 10 | | `vmudh` | 3 |
| `vand` | 8 | | `vsub` | 2 |
| `vsar` | 6 | | | |

The eight most frequent operations cover 80 % of the total. That is a mixer's
profile: multiply-accumulate over 16-bit samples.

## Measurement

`tools/cpu-budget/bench_rspvu.cpp` calls those eight operations in the same
proportions, on registers filled with values on the order of audio samples — no
degenerate patterns, whose saturations would not be representative. The same source
compiles for the host (SIMD) and for the target (SISD) unchanged: only the
architecture decides.

| Operation | SIMD host | SISD target | Factor | Weight |
|---|---:|---:|---:|---:|
| `vadd` | 1.37 ns | 420.79 ns | 307× | 13 |
| `vxor` | 0.58 ns | 143.55 ns | 248× | 24 |
| `vmacf` | 3.33 ns | 641.64 ns | 193× | 14 |
| `vmulf` | 2.34 ns | 443.45 ns | 190× | 26 |
| `vmadn` | 3.27 ns | 584.15 ns | 179× | 17 |
| `vmadh` | 3.10 ns | 498.74 ns | 161× | 33 |
| `vand` | 0.91 ns | 145.38 ns | 160× | 8 |
| `vmudn` | 1.75 ns | 248.07 ns | 142× | 10 |
| **Weighted** | **2.22 ns** | **410.07 ns** | **185×** | |

The raw factor of 185× breaks down: **17.7×** comes from the machine (measured
independently by [E00-S03](cpu-budget.md)), the rest — **10.4×** — is the penalty
proper to the scalar path against the eight lanes SSE4.1 handles at once. The two
terms tally cleanly, which gives confidence in the measurement.

## What that means

The RSP runs at 62.5 MHz and issues up to one vector operation per cycle, that is
**~62.5 million per second**. The target sustains **2.44 million**: it reaches
**3.9 %** of the vector throughput of the chip it must replace.

Expressed as a frame budget, at 30 frames per second:

| Share of the RSP the audio consumes on console | Cost on the target | 33.3 ms budget |
|---|---:|---:|
| 5 % | 43 ms | **128 %** |
| 10 % | 85 ms | **256 %** |
| 20 % | 171 ms | **513 %** |

Even under the most favourable hypothesis, the audio alone exceeds a whole frame's
budget. DKR would have to use **less than 4 %** of the RSP for its audio, which is
not credible for a game with music, engines and effects at the same time.

An MMX implementation, which the ticket considered, does not change the
conclusion: MMX offers four 16-bit lanes against eight for SSE, and has neither the
byte permutations nor the 32-bit saturations the SIMD path uses. A gain of 3 to 4×
would bring the target to ~15 % of the RSP's throughput — still five times too
slow. **Writing MMX would not save this path**, and that is a saving of effort
worth recording now.

## Decision

[E03-S03](../stories/E03-rsp/E03-S03-high-level-mixer-fallback.md) — interpreting
the audio commands at a high level rather than executing the microcode — **moves
from contingency to critical path**. Its triggering condition, written in
[E03-S02](../stories/E03-rsp/E03-S02-aspmain-audio-microcode.md), is met with a
margin that leaves no doubt.

Consequences for the backlog:

- [E03-S01](../stories/E03-rsp/E03-S01-vector-emulation-without-sse.md) (vector
  reimplementation) loses its reason for being as far as audio is concerned. The
  scalar path exists and suffices to *execute* the microcode — outside real time,
  which stays useful as an **oracle** for validating E03-S03's mixer: it produces
  the exact output, slowly.
- E03's estimated effort moves from "optimise the vector emulation" to "write a
  mixer", which is a far heavier item — the ticket classes it XL.

## Limits

- The target is an **emulated** Pentium II. The machine factor of 17.7× comes from
  86Box's model
  ([E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md) will say by how
  much it is wrong). But the gap measured here — a factor of 26 on throughput — is
  too large for a modelling inaccuracy to overturn.
- The bench measures the operations in isolation, outside the real microcode. It
  does not account for the RSP's scalar instructions, which add to the cost. The
  measurement is therefore **optimistic**: the complete microcode would cost more.
- The share of the RSP DKR's audio really consumes is not measured; the table above
  varies it rather than assuming it.
