# ADR 0001 - Toolchain

- **Status**: accepted
- **Date**: 2026-08-12
- **Ticket**: [E00-S02](../stories/E00-scoping/E00-S02-spike-pe-win95-toolchain.md)

## Context

The compiler choice governs the language available, and the language available
governs how much code must be rewritten. It is the project's most structuring
decision: if no modern compiler produces a viable binary under Windows 95, then
`ultramodern` and `librecomp` — written in C++20 — become a complete rewrite.

It is settled by experiment. Three witnesses of increasing complexity were built
by three candidates, then **run on the test machine**:

| Witness | What it proves |
|---|---|
| **T1** | PE format, subsystem, basic imports — without a CRT |
| **T2** | the CRT: heap, `fopen`/`fread`, `printf`, floating-point maths |
| **T3a** | `std::thread`, `std::mutex`, `condition_variable`, RTTI, exceptions |
| **T3b** | the same execution model, but on the Win32 primitives |

T3a and T3b are the heart of the matter: T3a is the shape the code to be ported
has, T3b the shape it could take.

## Results

Every binary is compiled `-march=pentium2 -mfpmath=387 -mno-sse`, and the
disassembly confirms **no SSE instruction at all** — startup code and standard
library included.

| Candidate | C++ | T1 | T2 | T3a | T3b | T3b size | Imported DLLs |
|---|---|---|---|---|---|---:|---|
| **Open Watcom 2.0** | partial C++98 | ✅ | ✅ | *not applicable* | ✅ | 51,200 B | KERNEL32, USER32 |
| **mingw GCC 13, posix** | **C++20** | ✅ | ✅ | ❌ | ✅ *(with the bridge)* | 501,625 B | KERNEL32, USER32, MSVCRT |
| **mingw GCC 13, win32** | **C++20** | ✅ | ✅ | ❌ | ❌ | 353,108 B | KERNEL32, USER32, MSVCRT |

Open Watcom has no `<thread>`: T3a does not exist for it, and that is a result,
not a gap in the protocol.

### The failures, and the exact symbol at fault

Windows 95 resolves **every** import at load time. A missing symbol is therefore
fatal even if the function is never called — which the machine says itself (its
system messages are in French; translated here):

> "The file T3BG.EXE is linked to a missing export KERNEL32.DLL:GetThreadId."

T3b never calls `GetThreadId`: it uses `CreateThread` directly. It is
`libstdc++`, pulled in by exceptions and RTTI, that imports it. **One therefore
cannot escape the problem by avoiding `std::thread`.**

Missing symbols recorded before correction:

| Binary | Missing |
|---|---|
| mingw-win32 T3b | `GetThreadId`, `TryEnterCriticalSection` |
| mingw-win32 T3a | + `InitializeConditionVariable`, `SleepConditionVariableCS`, `WakeConditionVariable`, `WakeAllConditionVariable`, `_fstat64` |
| mingw-posix T3a and T3b | `AddVectoredExceptionHandler`, `RemoveVectoredExceptionHandler`, `GetTickCount64`, `IsDebuggerPresent`, `SetProcessAffinityMask`, `TryEnterCriticalSection` |

### The compatibility bridge, written and put to the test

The `posix` model's six gaps are superficial — none is a feature, all are
conveniences. `tools/win95/win95compat/` supplies them in 150 lines, and **T3b
then goes from "does not start" to 1000/1000 on the real machine**, RTTI and
exceptions included.

Two difficulties had to be resolved, and are worth recording:

**Supplying the function is not enough.** `winpthreads` is compiled with
`__declspec(dllimport)`: its calls aim at the `__imp__X@n` pointer, not at the
`_X@n` symbol. The bridge must therefore define those pointers too, stdcall
decoration included, and be linked under `--whole-archive` — without which
`libkernel32.a`, placed last by the compiler's specs, wins.

**A "lawful" stub is not a harmless stub.** The first version had
`TryEnterCriticalSection` return `FALSE` — an answer the contract permits, and
one checked as safe since `try_lock` appears nowhere in the runtime. **It froze
the whole machine**: `winpthreads` loops on that function to take its locks, and
the busy wait starves Windows 95's scheduler to the point of stopping the
taskbar's clock. The bridge therefore implements **all five** critical-section
functions, which gives it ownership of `CRITICAL_SECTION`'s 24 bytes, and relies
on `lock cmpxchg` — a 486 instruction — where Windows 95 does not export
`InterlockedCompareExchange`.

### What the bridge does not repair

**`std::thread` does not work, even with the bridge.** T3a now loads without an
error, starts, then terminates without writing its result: it fails in the thread
part. The blockage is therefore not merely a question of missing symbols —
`winpthreads`'s POSIX emulation does not hold up on Windows 95.

That is this spike's most useful result, because it turns a hypothesis into a
certainty: **the threading layer must be rewritten on the Win32 primitives**
([E02-S01](../stories/E02-system/E02-S01-threading-and-synchronisation-layer.md)).
Until now that was only a plausible plan; it is now a measured constraint.

## Decision

**mingw-w64 GCC 13, target `i686-w64-mingw32`, `posix` threading model, linked
with `win95compat`.**

### Why not Open Watcom, which passes everything

Watcom is the best candidate on every criterion but one, and that single
criterion decides:

- its binaries are **51 KB against 501**;
- it imports **only KERNEL32 and USER32**, with no gaps, with no bridge;
- its CRT is statically linked — no question of redistribution;
- it targets Windows 95 natively, without a detour.

But it offers only **partial C++98**. `ultramodern` and `librecomp` are written
in C++20, and [E00-S01](../research/win95-blockers.md) showed that porting them
comes down to **six files** of `ultramodern` and the replacement of
`std::filesystem`. With Watcom, it is no longer six files to patch but two whole
libraries to rewrite — the scenario the ticket explicitly named as the risk to
avoid, "several weeks that appear in no ticket".

The extra cost of mingw is a binary ten times larger and a 150-line bridge. The
memory budget ([ADR 0003](0003-memory-budget.md)) has 14 MiB of headroom: the
size is not a problem.

**Watcom remains the documented fallback.** If porting `ultramodern` goes wrong
to the point of becoming a rewrite, the argument that rules Watcom out falls —
and it will then have to be reconsidered rather than persisted against. That is
the only condition that would reopen this decision.

### Why the `posix` model rather than `win32`

The two models are short of a comparable number of symbols, but not of the same
nature. The `win32` model requires **Vista's condition variables**, whose
reproduction on Windows 95 events is a delicate exercise in which wake-ups get
lost. The `posix` model requires `IsDebuggerPresent`, `GetTickCount64`,
`SetProcessAffinityMask` and two vectored exception handlers — all things one
writes in a few lines, which was done and verified.

### CRT distribution: static linking

`-static -static-libgcc -static-libstdc++`, without exception.

Three reasons, two of them measured:

1. **`libgcc_s_dw2-1.dll` does not exist under Windows 95.** A binary linked
   dynamically against libgcc does not load — observed during this spike, on a
   witness compiled without `-static` by inadvertence.
2. **`MSVCRT.DLL` is present on the test machine** (dated 3 November 1997, 756
   exports) but **not in first-generation Windows 95**. It arrives with OSR2 or
   Internet Explorer. Depending on it would amount to making the game depend on a
   version of IE.
3. Static linking removes any question of redistribution.

The cost is the size: 501 KB for T3b. Immaterial against the budget.

## Consequences

- **`ultramodern` and `librecomp` are patchable**, not to be rewritten. The major
  risk the ticket identified did not materialise, and that is this spike's chief
  gain.
- **[E02-S01](../stories/E02-system/E02-S01-threading-and-synchronisation-layer.md)
  becomes mandatory rather than optional**: `std::thread` does not work on the
  target. The threading layer must rest on `CreateThread`, `CRITICAL_SECTION` and
  events — which T3b validates.
- **`tools/win95/win95compat/` is this layer's starting point.** It is written,
  linked and tried on the machine; E02-S01 extends it rather than starting from
  nothing.
- **[E01-S01](../stories/E01-build/E01-S01-cmake-i686-toolchain-without-sse.md)**
  inherits the exact flags:
  `-march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2 -static
  -static-libgcc -static-libstdc++`, plus
  `-Wl,--whole-archive -lwin95compat -Wl,--no-whole-archive`.
- **[E01-S04](../stories/E01-build/E01-S04-pe-import-guard-rail.md)** becomes
  indispensable rather than comfortable: a single forgotten symbol makes the
  binary unloadable, with no warning at link time.
- The "no SSE instruction" check must bear on the **linked binary**, not on the
  project's objects: the SSE would come from the standard library.

## Reproducing

```sh
tools/win95/witnesses/build-witnesses.sh          # the three candidates, full matrix
tools/win95/check-win95-imports.sh <binary>       # symbols absent from Windows 95
scripts/Push-To-Win95-VM.sh build/win95-witnesses/*.exe
```

Open Watcom installs without privileges: the Linux installer the project
publishes is a zip archive, which extracts into `~/.local/dkr-win95/opt/watcom`.

## References

- [`docs/research/win95-blockers.md`](../research/win95-blockers.md) — inventory of the gaps, counts per file
- `tools/win95/witnesses/` — the four witnesses and their bench
- `tools/win95/win95compat/win95compat.c` — the bridge
- [ADR 0003](0003-memory-budget.md) — memory budget, which makes the binaries' size immaterial
