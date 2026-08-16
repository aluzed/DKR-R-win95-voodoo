# E00-S02 — Spike: produce an executable that starts under Windows 95

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E00-S01 |
| **Blocks** | E01-S01, E01-S02, E01-S03 |

## State as of 2026-08-12 — settled, and the major risk is ruled out

ADR: [`docs/adr/0001-toolchain.md`](../../adr/0001-toolchain.md).

**Decision: mingw-w64 GCC 13, `i686-w64-mingw32`, `posix` threading model, CRT
linked statically, with the `tools/win95/win95compat/` bridge.**

| Candidate | C++ | T1 | T2 | T3a | T3b | T3b size |
|---|---|---|---|---|---|---:|
| Open Watcom 2.0 | partial C++98 | ✅ | ✅ | *not applicable* | ✅ | 51,200 B |
| mingw GCC 13, posix | **C++20** | ✅ | ✅ | ❌ | ✅ *(with the bridge)* | 501,625 B |
| mingw GCC 13, win32 | **C++20** | ✅ | ✅ | ❌ | ❌ | 353,108 B |

No binary emits an SSE instruction, the standard library included.

**The ticket's major risk did not materialise**: `ultramodern` and `librecomp` stay
patchable. Watcom nonetheless passes every witness, with binaries ten times smaller
and no bridge — but its C++98 would force both libraries to be rewritten. It remains
the documented fallback if the port goes wrong.

**Three results that change what follows:**

1. **`std::thread` does not work on the target, even once every symbol is
   supplied.** T3a loads, starts, then fails in the thread part. **E02-S01 becomes
   mandatory**, and that is no longer a hypothesis.
2. **One cannot avoid the problem by avoiding `std::thread`**: T3b, which uses
   nothing but `CreateThread`, still fails to load — `libstdc++` imports
   `GetThreadId` for exceptions and RTTI. The machine says so itself: "linked to a
   missing export KERNEL32.DLL:GetThreadId".
3. **A lawful stub can freeze the machine.** `TryEnterCriticalSection` always
   returning `FALSE` — an answer the contract permits — blocked Windows 95 to the
   point of stopping its clock: `winpthreads` loops on it. The bridge implements all
   five critical-section functions.

The bridge is written, linked and **tried on the machine**: T3b goes from "does not
start" to 1000/1000, RTTI and exceptions included. It is E02-S01's starting point.

## Context

The compiler choice governs the language available, and the language available
governs how much code must be rewritten. It is the project's most structuring
decision, and it is settled by experiment, not by reading.

Three families of candidates, with a plain trade-off between the language's
modernity and the target's compatibility:

| Candidate | C++ available | Win95 compatibility |
|---|---|---|
| Visual C++ 6.0 / 2003 (7.1) | C++98 | native, Microsoft's last generation to target 95 |
| Open Watcom 2.0 | C99 / partial C++98 | native, still maintained |
| recent clang or GCC → `i686-w64-mingw32` | C++17 / C++20 | to be proved — mingw-w64's CRT startup calls APIs later than 95 |

The third route is the only one that preserves `ultramodern` and `librecomp` without
a rewrite; it is also the only one whose compatibility is not a given.

## Objective

To make a witness executable of increasing complexity start under Windows 95, and to
deduce from it which toolchain the project adopts.

## Scope

**In:** three witness executables, measured in an emulated Windows 95.

**Out:** compiling any of the game (that is E01-S05).

## Work

1. Set up the test machine (E09-S01 supplies the recipe; if it is not ready, a
   Windows 95 OSR2.5 under 86Box suffices for this spike).
2. For each candidate, produce three witnesses:
   - **T1** — `MessageBoxA` and exit. Proves the PE format, the subsystem and the
     basic imports.
   - **T2** — T1 plus the CRT: heap allocation, `fopen`/`fread`, `printf`,
     floating-point maths. Proves the dependence on the CRT and its distribution.
     Careful: `msvcrt.dll` is not present in first-generation Windows 95 — decide
     between static linking and redistribution.
   - **T3** — T2 plus two threads, a synchronisation object, and a C++ class with
     exceptions and RTTI. Proves the execution model, which is what `ultramodern`
     needs.
3. For the mingw-w64 candidate, force `-march=pentium2 -mtune=pentium3
   -mfpmath=387 -mno-sse` and verify in the disassembly that **no** SSE instruction
   remains, including in the startup code and in the standard library's functions.
4. Record for every witness that passes: the binary's size, the list of imported
   DLLs, the list of imported symbols, and the behaviour observed at startup under
   95.
5. For every witness that fails, record the exact symbol or instruction at fault. A
   documented failure is worth more than an unexplained success.

## Acceptance criteria

- [ ] The three witnesses are built by at least two candidates.
- [ ] At least one candidate runs T3 under emulated Windows 95, with a screenshot in
      support.
- [ ] The results table gives, per candidate: C++ available, T3's size, DLLs and
      symbols imported, execution status.
- [ ] The failures name the offending symbol or instruction.
- [ ] A toolchain-choice ADR is written (`docs/adr/0001-toolchain.md`), which also
      settles the CRT's distribution mode.
- [ ] The ADR states the choice's direct consequence for `ultramodern` and
      `librecomp`: patchable as they stand, or to be rewritten — relying on
      E00-S01's count of C++20 constructs.

## Risks

If no modern compiler produces a viable binary under 95, the project falls back on
C++98, and `ultramodern`/`librecomp` become a complete rewrite — several weeks that
appear in no ticket today. That is precisely why this spike comes before everything
else. The result must be raised as a project risk, not left in the ADR file.

## References

- `docs/BUILDING.md` — the current build chain (VS 2022, CMake, Ninja)
- `runtime-recomp/CMakeLists.txt:26-31`
- E00-S01 — inventory of the blockers
