# E00-S03 — Spike: CPU cost of the recompiled code in 32-bit without SSE

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E00-S02 |
| **Blocks** | E00-S05, E08-S01, E08-S02 |

## State as of 2026-08-11

**Both factors** of the budget are measured — the 64 → 32 bit move without SSE, and
the normalisation towards the real target machine:
[`docs/research/cpu-budget.md`](../../research/cpu-budget.md).

| Quantity | Measurement |
|---|---|
| Execution time 64 → 32 bit, median factor | **2.16×** (range 1.54× to 3.00×) |
| **Normalisation 32-bit host → Pentium II 400 MHz** | **17.7×** (range 15.6× to 21.7×) |
| **Overall factor, development machine → target** | **≈ 38×** |
| x86 instructions per MIPS instruction | 2.74 in 64-bit, **3.89** in 32-bit |
| `.text` code size | **+24.8 %** |
| SSE instructions in the 32-bit binary | **none**, verified in the disassembly |

The chain was made operational under Linux along the way: a MIPS toolchain without
root privileges, a **matching** decomp ELF (SHA-1 identical to the player's ROM),
`scripts/generate_recomp_toml.py`, and **587,625 lines of C generated** by
N64Recomp. A replayable bench: `tools/cpu-budget/run.sh`.

One outright blockage found and lifted: `recomp.h` required a 128-bit integer,
absent in 32-bit, which stopped the compilation before the first function. It served
only `DMULT`/`DMULTU`, which **DKR never calls** (zero occurrences across 3,823
functions). Fixed by a portable implementation proved equivalent over 20,000,200
comparisons — patch `patches/n64recomp/0002-…`.

**The normalisation towards the target is done** (step 5). The bench was ported to
Win32 and run on
[E09-S01](../E09-qa/E09-S01-emulated-test-environment.md)'s emulated Pentium II,
with the same generated code, the same ELF and the same inputs as on the host: only
the machine changes. 23 common functions, a narrow spread (15.6× to 21.7×), which
indicates a sound measurement.

Two lessons in the margin: the host/target frequency ratio being about 9 for a
measured factor of 17.7, the Pentium II is about **twice as inefficient per cycle**
on this code — plausible for recompilation, made of long dependency chains. And the
measurement binary, linked against **mingw-w64's complete CRT**, starts without
difficulty under Windows 95: that is a direct result for
[E00-S02](E00-S02-spike-pe-win95-toolchain.md), which held that question to be the
principal unknown.

**The go/no-go is still not pronounced**, but what is missing has changed in nature:
it is no longer a factor, it is a **denominator**. The bench measures isolated leaf
functions; it gives the relative cost from one machine to another, not the absolute
cost of a game frame. What is now needed is step 2 (a deterministic play sequence),
which requires
[E02-S06](../E02-system/E02-S06-game-bring-up.md), and
[E00-S04](E00-S04-spike-rsp-cost-without-sse.md)'s audio figure.

Usable straight away: the **38×** factor transposes onto the target any measurement
made on the development machine.

## Context

This is the ticket that decides whether this project is feasible.

Static recompilation translates every MIPS instruction into C. On a 64-bit host
that is comfortable: the VR4300's 32 64-bit registers fit naturally into the host's
registers. On a Pentium II, every one of the game's registers becomes a pair of
32-bit words, and every 64-bit operation a sequence of several x86 instructions. The
multiplying factor is not known and it cannot be guessed.

To that is added a load the console did not put on the CPU: the audio microcode,
run on the N64 by a dedicated 62.5 MHz vector DSP, runs here on the host processor
(E00-S04 measures it separately).

The order of magnitude to beat: the VR4300 runs at 93.75 MHz and the game aims at 30
frames per second. A 400 MHz Pentium II offers about four times the raw instruction
throughput. The whole question is how much of that margin the translation overhead
consumes.

## Objective

To measure the slowdown factor of the recompiled code compiled in 32-bit without
SSE, and to deduce from it the minimum CPU frequency required. To deliver a
numbered **go / no-go**, not an impression.

## Scope

**In:** measurement of the game's CPU code alone, outside rendering and outside
audio.

**Out:** optimisation (that is E08-S02); the whole graphics part.

## Work

1. Build the existing runtime in two variants on the same modern host:
   - reference: x86-64, current options;
   - target: `-m32 -march=pentium2 -mfpmath=387 -mno-sse`, the toolchain E00-S02
     retained.
2. Instrument a **deterministic and reproducible** portion of the game: for
   instance the startup up to the title screen, then a fixed race played by a
   replayed input trace. Without reproducibility, the measurement compares nothing.
3. Measure, with the `DiagnosticRenderer` (`null_renderer.cpp`) to take the
   rendering out of the equation:
   - the game thread's total CPU time per frame, median and 99th percentile;
   - the distribution per hot area (physics, AI, collisions, matrices).
4. Deduce the target / reference factor, then, by normalising by the measurement
   host's frequency, the minimum frequency of a Pentium II that holds 33.3 ms per
   frame while leaving a margin for rendering and audio.
5. Cross-check against an independent measurement: replay the same trace on a real
   or heavily throttled 32-bit machine if the workshop has one. An extrapolation
   from a modern core always overestimates old machines — the instructions per
   cycle, the cache sizes and the branch prediction bear no relation.
6. Write `docs/research/cpu-budget.md` with the method, the raw figures, and the
   conclusion.

## Acceptance criteria

- [ ] The 64 → 32 bit slowdown factor without SSE is measured on at least two
      distinct play sequences.
- [ ] The per-frame budget is broken down: the game's CPU, the audio microcode (the
      figure taken from E00-S04), vertex transformation, remaining margin.
- [ ] The minimum CPU frequency is stated in MHz, with the margin hypothesis made
      explicit.
- [ ] A **go / no-go** conclusion is written in black and white, with the threshold
      that would trigger it the other way.
- [ ] If the verdict is no-go on a Pentium II, the document states what would change
      the picture: a raised hardware floor (Pentium III), or a switch to the
      neighbouring decomp's native port.

## Risks

The result may condemn the recomp approach on this class of machine. That is an
acceptable outcome of this ticket, and it is even its reason for being: discovering
it now costs a week, discovering it after E04 and E05 costs three months.

The fallback exists and is documented: `/var/www/Diddy-Kong-Racing` carries a
"Voodoo95" backlog of a native port from the decomp, which does not have this
translation overhead since it compiles the original C — at the price of far heavier
work on everything else.

## References

- `runtime-recomp/src/game/null_renderer.cpp` — the diagnostic renderer, ideal for
  isolating the CPU cost
- `docs/ARCHITECTURE.md` — execution path
- E00-S04 — cost of the audio microcode, the budget's second term
