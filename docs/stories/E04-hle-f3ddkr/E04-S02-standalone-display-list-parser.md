# E04-S02 — Standalone F3DDKR display-list parser

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E04-S01 |
| **Blocks** | E04-S03, E04-S05, E04-S06, E04-S08 |

## Context

`F3DDKRRT64Bridge` already knows how to decode Rare's microcode: its fourteen
handlers cover the whole of the command set DKR uses. It is valuable work, validated
by a port that runs, and it must on no account be redone from scratch.

But it is written against RT64: it registers its handlers in an `RT64::GBI`, receives
`RT64::DisplayList**`s, writes into an `RT64::State`. The work therefore consists of
extracting the decoding logic from it — which belongs to the microcode and has
nothing to do with RT64 — and resting it on E04-S01's interface.

An inherited point of care, not to be lost along the way: the current decoder
**validates every range** before using it — matrices, vertices, triangles, textures,
nested display lists — and rejects invalid data with a bounded error rather than
letting host memory be addressed (`docs/F3DDKR.md`). That discipline must survive the
extraction: it protects against a modified ROM as much as against a bug in the port.

## Objective

To deliver `platform/render/f3ddkr.{h,cpp}`: an F3DDKR display-list decoder that
depends only on E04-S01's interface.

## Scope

**In:** parsing the display list, dispatching the commands, validating the ranges,
managing nested display lists.

**Out:** transforming the vertices (E04-S03), clipping (E04-S05), the RDP state
(E04-S06), the textures (E04-S07).

## Work

1. Map the fourteen commands from `f3ddkr_rt64.cpp`: opcode, field layout, effect.
   Document the whole in `docs/research/f3ddkr-commands.md` — that map has a value of
   its own, independently of the port.
2. Extract the decoding logic into the new module, replacing each write into
   `RT64::State` by a call to E04-S01's interface or by a write into the decoder's
   internal state.
3. Take the range validation over in full. Every read from the RDRAM snapshot is
   bounded, and an invalid range produces a circumscribed error that interrupts the
   current display list without bringing the game down.
4. Deal with nested display lists: `DisplayListBranch`, `EndDisplayList` and
   `CountedDisplayList`. Bound the nesting depth and check that overrunning it is
   handled cleanly.
5. Deal with `DMAOffsets`, a peculiarity of Rare's microcode: the vertices and the
   matrices are addressed through offsets relative to bases supplied by the command.
   It is F3DDKR's central mechanism, and an error in a base there produces totally
   absurd geometry.
6. Write the trace mode: a mode that logs every decoded command, its parameters and
   the primitives emitted. Without that tool, any graphics diagnosis on the target
   machine is done blind.
7. Write the tests on display lists captured from real play: title screen, character
   selection, two levels of different natures. The test checks the sequence of
   primitives emitted, not merely the absence of a crash.
8. Provide for capturing those display lists from the modern target, and replaying
   them — it is the foundation of E09-S02's comparison harness.

## Acceptance criteria

- [x] `platform/render/f3ddkr.{h,c}` references no RT64 type — in C rather than in
      C++, the extraction needing none of the latter's facilities.
- [x] The thirteen opcodes and the presentation group are decoded and documented in
      `docs/research/f3ddkr-commands.md`. The fourteenth "handler" has no opcode:
      `PresentationGroup` is reached through `MoveWord` with a magic word, and it is
      an **extension of the port**, not of the microcode.
- [x] The range validation is taken over in full and tested by injecting deliberately
      corrupted display lists — which makes the suite possible **without a ROM**: a
      corrupted list can be written, a real one has to be captured. Exercised in both
      directions: removing either of the two disciplines makes three checks fail.
- [x] The nesting is bounded at 32, and overrunning it produces a circumscribed error
      — verified by a list that calls itself.
- [~] The trace mode logs the commands. **Not yet the primitives emitted**: the
      decoder emits none, for want of E04-S03 to project the vertices. Emitting
      vertices in object space would give a wrong image rather than an absent one,
      which is worse — one would believe the path complete.
- [ ] The tests replay at least four captured display lists — **blocked**, the
      capture requiring a game on the modern target, hence the ROM.
- [ ] The sequence is identical to the RT64 decoder's — **blocked** for the same
      reason, and because RT64 is absent from this repository.

## Risks

The extraction is a rewrite in disguise, and a rewrite loses subtle fixes if it
proceeds by rereading. Proceed by moving code rather than by reimplementing, and
validate by comparing sequences rather than by inspection.

## References

- `runtime-recomp/src/game/f3ddkr_rt64.cpp:1-...` — the current decoder
- `runtime-recomp/src/game/f3ddkr_rt64.hpp` — the list of handlers
- `docs/F3DDKR.md` — range validation, presentation groups
- `extern/dkr-decomp` — `include/f3ddkr.h` documents the microcode on the decomp's
  side
