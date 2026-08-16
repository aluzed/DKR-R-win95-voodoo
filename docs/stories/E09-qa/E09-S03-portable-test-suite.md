# E09-S03 — Portable test suite

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E00-S07, E01-S01, E07-S03 |
| **Blocks** | E09-S05 |

## Context

The project has 18 test suites run by CTest, and the build scripts require them before
any packaging (`Build-Linux.sh:32`). It is a good discipline, and one to keep despite
three difficulties:

- some of those suites bear on modern policies that disappear (E07-S01, E07-S02);
- the Win95 target is cross-compiled: its tests do not run on the build host without
  going through the emulated machine;
- CTest and the test framework used may not be available in the retained C++ subset
  (E01-S02).

The right answer is to separate three families: what tests **portable logic** and runs
everywhere, what tests **platform-specific code** and must run on the target, and what
**compares** the two targets.

## Objective

To keep a useful automatic verification on both targets, run before any packaging.

## Scope

**In:** organising the tests, running them on the target, integrating them into the build
scripts.

**Out:** the visual comparison (E09-S02).

## Work

1. Sort the 18 existing suites according to E00-S07's decision: kept, removed, to be
   adapted. The save codec and audio equaliser suites are to be kept; those of the modern
   policies go.
2. Classify the suites kept into three families: portable logic, platform-specific,
   cross-target comparison.
3. Run the portable-logic tests on both targets. If they depend on a test framework
   unavailable in restricted C++, provide a minimal replacement rather than give them up.
4. Write the platform tests E02 asks for: threads and synchronisation (E02-S01), clock
   and overflow (E02-S03), saves (E02-S05).
5. Automate running them on the emulated machine: launch the suite, collect the results,
   report them back on the host. Without automation, these tests will not be run
   regularly, and tests one does not run are worth nothing.
6. Add the cross-target comparison tests: does the same input produce the same result on
   the modern host and on the target? That is what will catch the floating-point rounding
   divergences introduced by `-mfpmath=387` (E01-S01).
7. Integrate them into the build scripts, as a blocking failure, on the model of
   `Build-Linux.sh`.
8. Document how to run them in `docs/TESTING.md`.

## Acceptance criteria

- [ ] The 18 existing suites are sorted, each decision being justified.
- [ ] The portable-logic tests pass on both targets.
- [ ] The platform tests E02 asks for are written and pass on the target.
- [ ] Running them on the emulated machine is automated.
- [ ] The cross-target comparison tests detect a rounding divergence introduced
      deliberately.
- [ ] The tests are blocking in the build scripts.
- [ ] `docs/TESTING.md` documents running them on both targets.

## Risks

A test suite that runs only on the modern host gives deceptive confidence: it validates
code compiled by another compiler, for another architecture, with another floating-point
arithmetic. Step 5's automation is what distinguishes a useful suite from a decorative
one.

## References

- `runtime-recomp/tests/` — 18 existing suites
- `Build-Linux.sh:32` — CTest blocking before packaging
- E01-S01 — `-mfpmath=387` and its rounding deviations
