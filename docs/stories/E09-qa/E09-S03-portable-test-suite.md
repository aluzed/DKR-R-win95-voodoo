# E09-S03 — Portable test suite

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | IN_PROGRESS |
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

- [x] The 18 existing suites are sorted, each decision being justified -- they are
      seventy now. Sorted on 2 October 2026 by following each suite's includes into
      `src/game` and comparing with what `DKRR.EXE` compiles: 30 test code the game
      contains and need nothing else (`tools/tests/portable-suites.txt`), 2 share
      code but need more (three ROM revisions; the netplay library), 38 test the
      modern build only. `docs/TESTING.md`.
- [~] The portable-logic tests pass on both targets -- **all 30 on the host**
      (`tools/tests/run-portable-tests.sh`, 25 s). 23 are built for the target from
      the same list (`PT*.EXE`) and run by `tools/tests/Run-Target-Tests-VM.sh`;
      seven are not, each for a reason written beside the list in
      `cmake/win95-target.cmake` -- five of them because of the divergence below.
- [~] The platform tests E02 asks for are written and pass on the target -- written,
      and passed by hand in August and September (E02-S01, E02-S03, E02-S05). Run by
      the new runner on 3 October 2026: **`THREADS`, `CLOCKT` and `FILEIOT` pass;
      `SAVEMGR` did not finish** -- it wrote nothing and was still running when the
      runner's fifteen minutes ran out, so `SAVECDC` and the portable suites after it
      were not reached. It passed on 13 August launched the same way; what changed is
      not yet known. Open.
- [x] Running them on the emulated machine is automated:
      `tools/tests/Run-Target-Tests-VM.sh` copies the platform tests and the portable
      suites to `D:\PT`, runs them through a batch file, and reports each exit code;
      a suite that hangs shows as the run not finishing -- which is how `SAVEMGR`'s
      hang was found.
- [x] The cross-target comparison tests detect a rounding divergence -- **a real
      one, not introduced**: built for the target, five suites' compile-time
      assertions fail, thirteen of them, because with `-mfpmath=387` GCC 13 gives
      C++ standard excess precision and a `float` constant expression is
      evaluated in `long double` (`0.920000017 == 0.92` is false). One is in the
      code under test: `split_gutter_authored(4.0F / 3.0F)` returns 2.4e-6 on the
      target and 0 on the host. `docs/TESTING.md`.
- [~] The tests are blocking in the build scripts -- the platform tests and the
      thirty portable suites block `scripts/Package-Win95.sh`; the target run
      needs the test machine and is not in it.
- [x] `docs/TESTING.md` documents running them on both targets.

## Risks

A test suite that runs only on the modern host gives deceptive confidence: it validates
code compiled by another compiler, for another architecture, with another floating-point
arithmetic. Step 5's automation is what distinguishes a useful suite from a decorative
one.

## References

- `runtime-recomp/tests/` — 18 existing suites
- `Build-Linux.sh:32` — CTest blocking before packaging
- E01-S01 — `-mfpmath=387` and its rounding deviations
