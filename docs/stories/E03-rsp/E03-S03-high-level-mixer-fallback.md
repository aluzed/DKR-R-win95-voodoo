# E03-S03 — Fallback: high-level audio mixer

| | |
|---|---|
| **Epic** | E03 — RSP on x86 without SSE |
| **Status** | TODO — **triggered** |
| **Priority** | **P0** |
| **Estimate** | XL |
| **Depends on** | E03-S02 |
| **Blocks** | — |

## Context

**This ticket is triggered.** Its condition is met unambiguously:
[E00-S04](../E00-scoping/E00-S04-spike-rsp-cost-without-sse.md) measured that the
target reaches only **3.9 % of the RSP's vector throughput**. Even assuming the audio
consumes only 5 % of the RSP on console, the recompiled microcode would cost 43 ms per
frame against a budget of 33.3 ms — and an MMX implementation, which would offer only
four lanes instead of eight, would change nothing.

Details and figures:
[`docs/research/rsp-audio-budget.md`](../../research/rsp-audio-budget.md).

In that case, we must stop running the microcode instruction by instruction and
interpret at a high level the audio commands it receives: read the command list the
engine produces, and carry out the requested operation directly — ADPCM decoding,
resampling, envelope, mixing — in x86 code written for the target machine.

That is the approach of the N64 emulators with HLE audio. It is markedly faster and
markedly less faithful: the output is no longer bit-for-bit identical, and some
effects peculiar to Rare's microcode may differ.

The neighbouring native port has the same trade-off in its epic E06, with the same
reasoning.

## Objective

To supply a high-level audio mixer that holds the CPU budget, with a difference in
sound that is measured and judged acceptable.

## Scope

**In:** interpreting the audio commands and mixing.

**Out:** the audio output (E06-S03). The mixer produces buffers; it does not play
them back.

## Work

1. Document DKR's audio ABI: the command list's format, the opcodes, the data
   structures. Two sources corroborate each other — the decomp (`extern/dkr-decomp`,
   which contains the audio engine's code) and the recompiled microcode itself, whose
   dispatcher is described by the sixteen branch targets in the TOML file.
2. Implement the commands one by one, in order of frequency of use. At each step,
   compare the output against the recompiled microcode's.
3. Deal first with ADPCM decoding and resampling: they are the dominant operations of
   any N64 audio mixer.
4. **Use the recompiled microcode as the oracle.** It compiles and runs on the target
   through the scalar path — too slowly for real time, but with bit-for-bit fidelity.
   It is the reference against which to measure the mixer's deviation, and it is
   available right now.
5. Measure the difference in sound objectively: root-mean-square error against the
   reference, on musical sequences and on effects. "It sounds the same" is not a
   criterion.
6. Measure the CPU gain and check that it brings the audio back within its budget.
7. Keep the microcode path available and selectable through the configuration
   (E06-S05): on a machine faster than the floor, fidelity must stay reachable.
8. Document, in `docs/AUDIO-HLE.md`, what differs from the reference and why.

## Acceptance criteria

- [ ] DKR's audio ABI is documented from the decomp and the microcode.
- [ ] The mixer produces audible and correct output for the music and for the
      effects.
- [ ] The deviation from the reference is measured objectively and recorded.
- [ ] The CPU gain is measured and brings the audio back within its budget.
- [ ] The microcode path stays selectable through the configuration.
- [ ] The known differences are documented.
- [ ] This ticket's triggering is justified by E03-S02's figure, and that figure is
      quoted here.

## Risks

This is one of the project's largest items of work, and it produces an accepted
regression in fidelity. It must not be undertaken out of comfort or in anticipation —
only on the strength of a measurement proving that the faithful path does not hold.

Conversely, if it is necessary and we put it off, all the downstream audio work is
built on a foundation that will not hold.

## References

- E03-S02 — triggering condition and budget
- `runtime-recomp/rsp/aspMain.us.v77.toml` — ABI dispatcher and command table
- `extern/dkr-decomp` — source code of the game's audio engine
- `../../Diddy-Kong-Racing/docs/stories/E06-audio/` — the same trade-off on the native
  port's side
