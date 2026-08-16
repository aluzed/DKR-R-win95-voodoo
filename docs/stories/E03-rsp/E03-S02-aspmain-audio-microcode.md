# E03-S02 — `aspMain` audio microcode: execution and budget

| | |
|---|---|
| **Epic** | E03 — RSP on x86 without SSE |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E03-S01, E02-S02 |
| **Blocks** | E06-S03, E03-S03 |

## Context

`aspMain` is Rare's audio microcode, recompiled instruction by instruction into 73 KB
of C++. It produces the game's audio buffers from the commands the engine issues.

Its recompilation configuration (`runtime-recomp/rsp/aspMain.us.v77.toml`) is
instructive about its nature: the microcode is loaded at IMEM address `0x04001080`
rather than at `0x1000` like later revisions, and sixteen indirect branch targets had
to be declared by hand because the ABI dispatcher reads them from a command table,
out of static analysis's reach. This is not standard microcode, and it cannot be
replaced by a generic implementation.

Once E03-S01 is delivered, `aspMain` must run correctly on the target. What remains
is to check that it produces the right sound, and at what price.

## Objective

To make `aspMain` produce correct audio buffers under Windows 95, and to measure its
exact share of the CPU budget.

## Scope

**In:** running the recompiled microcode, validating it, measuring it.

**Out:** the vector emulation (E03-S01) and the sound output (E06-S03).

## Work

1. Run `dkrAspMain` on the target with E03-S01's implementation, on real audio tasks
   produced by the game.
2. Compare the buffers produced against the modern target's, bit for bit. Any
   difference goes back to E03-S01, not to a mixing setting.
3. Check the scheduling: the audio task is submitted by the game just as the graphics
   task is, and its completion must be signalled to the requesting thread. Several
   existing patches bear on that completion mechanism
   (`0011-acknowledge-sp-delivery-before-dp`, `0012-wait-for-emulated-sp-handler`):
   check that they stay correct with E02-S01's layer.
4. Measure the execution time per audio task on the target, at the median and at the
   99th percentile. The high percentile counts more than the median: it is the one
   that causes the sound dropouts.
5. Convert it into a percentage of the per-frame budget and enter it in E08-S01's
   overall budget.
6. Identify the microcode's hot zones and check that they match the distribution
   E00-S04 forecast. A gap signals a false hypothesis in the spike, which is better
   corrected than carried around.
7. Rule on whether E03-S03 is triggered: if the budget is exceeded and optimisation
   does not suffice, the high-level mixer becomes necessary.

## Acceptance criteria

- [ ] `dkrAspMain` runs on the target and produces buffers bit-for-bit identical to
      the modern target's.
- [ ] Task completion is signalled correctly, shutdown paths included.
- [ ] The time per task is measured at the median and at the 99th percentile.
- [ ] The share of the per-frame budget is quantified and entered in the overall
      budget.
- [ ] The hot zones are identified and compared against E00-S04's forecast.
- [ ] The decision whether or not to trigger E03-S03 is taken on that figure.

## Risks

Audio is merciless about deadlines: a late rendered frame produces a stutter the eye
forgives, a late audio buffer produces a crackle the ear does not. The audio budget
must therefore be held at the high percentile, not on average.

## References

- `runtime-recomp/RecompiledRSP/aspMain.cpp`
- `runtime-recomp/rsp/aspMain.us.v77.toml`
- `patches/n64-modern-runtime/0011-acknowledge-sp-delivery-before-dp.patch`,
  `0012-wait-for-emulated-sp-handler.patch`
