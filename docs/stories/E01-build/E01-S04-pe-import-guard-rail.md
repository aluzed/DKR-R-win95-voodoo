# E01-S04 — Guard rail: checking the PE's imports

| | |
|---|---|
| **Epic** | E01 — 32-bit Windows 95 build chain |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | S |
| **Depends on** | E01-S03 |
| **Blocks** | E09-S05 |

## State as of 2026-08-12 — delivered

| Deliverable | File |
|---|---|
| Versioned export baseline | `tools/win95/exports/*.txt` — **3,201 symbols, 14 DLLs** |
| Provenance | `tools/win95/exports/PROVENANCE.md` |
| Exceptions | `tools/win95/exports/exceptions.json` |
| Tool | `tools/win95/check_imports.py` |
| Wiring | blocking post-build step, `cmake/win95-target.cmake` |

**Three categories of DLL, and the distinction is the tool's core:**

| Category | Treatment | Verified |
|---|---|---|
| System | every symbol set against the baseline | ✅ blocking |
| Driver (`glide2x`, `glide3x`) | reported, not verifiable here | ✅ tested with a fabricated import library |
| Unknown | **error** | ✅ tested with `winspool.drv` |

The third category is the one that counts: without it, a new dependency would go
unnoticed.

**The report names the offending object.** The PE's import table does not keep that
information — it is lost at link time. `--objects` reconstitutes it by re-reading the
objects with `nm`. On the trial:

```
MISSING KERNEL32.DLL:InitializeConditionVariable  <- import_canary.c.obj
```

**The check is tried by injection, at two levels**, like the instruction-set one:
`--self-test` compiles a binary importing `GetTickCount64` and checks that it is
refused; `-DDKR_WIN95_SELFTEST_IMPORT=ON` adds a translation unit to the witness and
**the build fails**.

An instructive detail: the canary's first version imported `GetTickCount64` and the
build passed — because `win95compat` supplies it, and the linker resolved the import
to the bridge rather than to KERNEL32. The trial failed to fail, which was in itself
the demonstration that the bridge intercepts correctly. The canary now imports
`InitializeConditionVariable`, which the bridge does not cover.

**A single baseline.** E00-S01's tool kept its reference in
`$DKR_WIN95_PREFIX/win95-exports.txt`, outside the repository.
`tools/win95/check-win95-imports.sh` has become a wrapper around the new tool: two
baselines that diverge would be worse than one imperfect one.

**Two findings recorded along the way:**

- **`MSVCRT.DLL` is not original** — dated 3 November 1997 when everything else
  carries 24 August 1996. It arrives with an update, and a first-generation Windows
  95 does not have it. That is what justifies linking the CRT statically (ADR 0001).
- **DirectInput is absent.** The installation carries DirectX 2 — `DDRAW`, `DSOUND`,
  `D3DIM`, `D3DRM` — but no `DINPUT.DLL`. Gamepad reading
  ([E06-S02](../E06-platform/E06-S02-keyboard-and-gamepad-input.md)) must go through
  `WINMM`'s `joyGetPosEx`, or the package must ship a DirectX update.

**Still to do:** the ticket's point 7 — the same check at the packaging stage —
awaits [E09-S05](../E09-qa/E09-S05-packaging-distribution.md), which does not exist
yet. The tool is ready to be called there as it stands.

## Earlier state — E00-S01's tool

[E00-S01](../E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md) needed
this check for its own measurements, and therefore wrote it:

- `tools/win95/pe_symbols.py` — a PE32's export and import tables, with no
  dependency;
- `tools/win95/check-win95-imports.sh` — compares a binary's imports against the
  reference, and **returns a non-zero exit code** if any are missing.

The reference is not a hand-written list: `--refresh` extracts it from the six DLLs
in the test machine's `C:\WINDOWS\SYSTEM` (2,754 symbols). The script also reports
separately KERNEL32's `...W` imports, which pass the check but are inert stubs under
9x.

Still to do: call it from CMake as a post-build step, and decide whether a gap breaks
the build or merely warns.

## Context

Under Windows 95, a missing import is a load error: the process does not start at
all. The symptom is therefore binary and late — one only discovers it by running the
binary on the target machine, which, at the pace of a round trip to an emulator or a
real machine, costs several minutes each time.

That class of error is entirely checkable cold: the PE's import table is static, and
so is the list of Windows 95's exports. It is exactly the kind of check that must run
at every build rather than in the head of whoever reviews the code.

The same reasoning holds for the instruction set, already covered by E01-S01: this
ticket adds the second guard rail, the one on symbols.

## Objective

To refuse at build time any binary that could not load under Windows 95.

## Scope

**In:** the checking tool, its reference baseline, its integration.

**Out:** the compatibility layer itself (E01-S03).

## Work

1. Build the reference baseline: the exports of `kernel32`, `user32`, `gdi32`,
   `advapi32`, `winmm`, `ddraw`, `dinput` and `dsound` as they exist under Windows 95
   OSR2.5. Extract them from the test machine's DLLs — a list copied from
   documentation is a wrong list.
2. Put that baseline under `tools/win95/exports/`, with the provenance and the exact
   version of the system it comes from.
3. Write `tools/win95/check_imports.py`: read the PE's import table, set it against
   the baseline, fail with the list of offending symbols and each one's DLL.
4. Deal with the case of non-system DLLs. `glide2x.dll` or `glide3x.dll` are supplied
   by the card's driver, not by the OS: the tool must distinguish "system DLL,
   verifiable symbols" from "supplied DLL, presence to be checked at launch", and not
   silently ignore the second category.
5. Wire the tool in as a post-build step of the Win95 target, as a blocking failure.
6. Provide for a list of explicit exceptions, every entry carrying a written
   justification — without which the list becomes the place where the tool is
   silenced.
7. Add the same check at the packaging stage (E09-S05), on the binary actually
   distributed.

## Acceptance criteria

- [ ] The export baseline is extracted from a real Windows 95 and its provenance is
      documented.
- [ ] `check_imports.py` detects a forbidden import introduced deliberately —
      tested, not assumed.
- [ ] The check is blocking at post-build and at packaging.
- [ ] DLLs supplied by a driver are treated separately, not ignored.
- [ ] Every exception carries a written justification.
- [ ] The failure report names the symbol, its DLL, and the object that imports it —
      without the last, the diagnosis is left to be done by hand.

## Risks

An incomplete export baseline produces false positives, and repeated false positives
lead to the tool being disabled. Better a baseline restricted to a few DLLs, exact
and respected, than a wide and approximate one.

## References

- E01-S01 — instruction-set verifier, the same principle
- E01-S03 — the compatibility layer that supplies the replacements
- `scripts/scan_for_game_assets.py` — a precedent for a blocking check at packaging
