# E01-S01 — CMake toolchain file: i686, without SSE

| | |
|---|---|
| **Epic** | E01 — 32-bit Windows 95 build chain |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E00-S02 |
| **Blocks** | E01-S02, E01-S03, E01-S05, E02-S01 |

## State as of 2026-08-12 — delivered

```
./Build-Win95.sh
  -> build/win95/bin/WITNESS.EXE
     PE32 executable (console) Intel 80386, for MS Windows
     no instruction outside the Pentium II set
     no symbol absent from Windows 95
```

| Deliverable | File |
|---|---|
| Toolchain | `cmake/toolchain-win95.cmake` |
| CMake target | `cmake/win95-target.cmake`, option `DKR_RUNTIME_TARGET_WIN95` |
| Verifier | `tools/win95/check-instruction-set.sh` |
| Build script | `Build-Win95.sh` |

**The existing targets are unchanged, and it is proved**: configured before and
after the change, `build.ninja` is **identical byte for byte**. The CMake cache's
only difference is the `DKR_RUNTIME_TARGET_WIN95:BOOL=OFF` entry, which is inert.
The change to `CMakeLists.txt` comes down to a false `if()` followed by a
`return()`: everything else lives in `cmake/win95-target.cmake`.

**The verifier is tried by injection, not assumed.** Two levels: `--self-test`
compiles an SSE object and checks that it is refused; `-DDKR_WIN95_SELFTEST_SSE=ON`
adds an SSE translation unit to the witness, and the build **fails** at the
post-link check, naming the instructions (`movss`, `mulss`, `divss`…).
`Build-Win95.sh` runs the self-test before compiling anything.

It detects by the `xmm`/`ymm`/`zmm` **registers** rather than by a list of mnemonics
to keep up to date, completed by the post-Pentium II instructions that name none
(memory barriers, `prefetch*`, 3DNow!). The check bears on the **linked binary**,
hence on the CRT and the standard library — that is where SSE slips in, not in our
sources.

**Two discoveries along the way:**

1. **`_WIN32_WINNT=0x0400` does hide the recent APIs** — verified:
   `InitializeConditionVariable` disappears from the preprocessor's output. But in
   C, GCC 13 emits only a *warning* for an undeclared function: without
   `-Werror=implicit-function-declaration`, a Vista API would pass compilation only
   to fail at load time. The flag is in the toolchain.
2. **An SSE injection can be inert.** The probe's first version used `double` with
   `-msse`: GCC fell back on x87, since double precision requires SSE2. The trial
   therefore looked conclusive while testing nothing. Corrected to `float`.

**Not done, and accepted:** `WITNESS.EXE` has not been run on the test machine. Its
only source is E00-S02's T3b witness, which prints 1000/1000 there — only the
optimisation level differs (`-O3` instead of `-O2`). The acceptance criterion asked
for compilation and linking as a 32-bit PE, which is established.

`-mfpmath=387`'s risk — x87's 80-bit rounding against strict IEEE — stays whole and
unmeasured. It will show in E01-S05 and will be measured against the oracle in
E09-S02.

## Context

`runtime-recomp/CMakeLists.txt` assumes a modern host: CMake 3.24, C17 and C++20
required, MSVC or Clang/GCC, RT64 and SDL2. The Win95 target is another world, and
it must coexist with the existing one without breaking it — the comparison oracle
depends on it (E00-S07).

The easiest point to miss is the instruction set. A modern 32-bit compiler emits
SSE2 by default for floating-point arithmetic; the Pentium II has only MMX and x87.
The failure does not show at compile time: it shows at launch, as an invalid
instruction exception, possibly months later in a rarely reached function.

## Objective

To add a `win95` build target that produces a 32-bit binary of which it is
**proved** that no instruction exceeds the Pentium II, without altering the existing
Windows, Linux and macOS targets.

## Scope

**In:** the toolchain file, the CMake integration, the instruction-set check, a
build script.

**Out:** actually compiling the game's sources — they do not compile yet (E01-S02,
E01-S03, E01-S05).

## Work

1. Write `cmake/toolchain-win95.cmake` with the compiler E00-S02's ADR retained,
   `CMAKE_SYSTEM_NAME Windows`, `CMAKE_SYSTEM_PROCESSOR i686`, and the option set:
   `-m32 -march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2` (to be
   transposed according to the toolchain).
2. Define `_WIN32_WINNT`, `WINVER` and `NTDDI_VERSION` at the highest Windows 95
   value the headers accept, so that the use of too recent an API fails **at compile
   time** rather than at launch.
3. Introduce a `DKR_RUNTIME_TARGET_WIN95` option in
   `runtime-recomp/CMakeLists.txt` which forces `DKR_RUNTIME_BUILD_RT64=OFF`,
   short-circuits SDL2 and ImGui, and selects the target's sources.
4. Write the instruction-set verifier: a script that disassembles every object
   produced and fails if an instruction outside the Pentium II set appears. It must
   cover the CRT's startup code and the standard library, not only the project's
   sources — that is where SSE slips in.
5. Wire that verifier in as a mandatory post-build step of the target, not as an
   optional tool.
6. Write `Build-Win95.sh` on the model of `Build-Linux.sh`, with the same
   prerequisite checks at the head of the script.
7. Check that `Build-Linux.sh` and `Build-Windows.cmd` produce unchanged binaries —
   by comparing CMake's configuration output before and after.

## Acceptance criteria

- [ ] `cmake -S runtime-recomp --toolchain cmake/toolchain-win95.cmake` configures
      without an error.
- [ ] A minimal witness file compiles and links as a 32-bit PE.
- [ ] The instruction-set verifier runs automatically after the link and fails on an
      object deliberately containing SSE — tested by injection, not assumed.
- [ ] The verifier covers the CRT's and the standard library's code.
- [ ] The use of an API later than Windows 95 fails at compile time.
- [ ] The existing Windows, Linux and macOS targets are unchanged, proved by
      comparing the CMake configuration.
- [ ] `Build-Win95.sh` exists and clearly reports its missing prerequisites.

## Risks

`-mfpmath=387` changes floating-point results: x87 computes in 80 bits internally
and rounds on output, where SSE computes strictly in 32 or 64 bits. The recompiled
game code runs the VR4300's floating-point arithmetic, which is IEEE 754 single and
double precision. Rounding discrepancies are therefore expected, and they may alter
the physics. To be watched from E01-S05 onwards, and to be measured against the
oracle in E09-S02: it is a likely candidate for the first behavioural divergence
observed.

## References

- `runtime-recomp/CMakeLists.txt:26-31, 36-37`
- `Build-Linux.sh` — model build script with prerequisite checking
- E00-S02's ADR — the toolchain retained
