# The Windows 95 target's C++ subset

Delivered by [E01-S02](stories/E01-build/E01-S02-enforced-cpp-subset.md).
Checked by `tools/win95/check-cpp-subset.py`, as a pre-build step.

## The initial question was badly put

The ticket asked "which C++ dialect to forbid ourselves", assuming the target
would impose C++98. **That is false.** GCC 13 targets `i686-w64-mingw32` and
implements all of C++20; concepts, `<ranges>`, `<span>`, `consteval` and
`operator<=>` cost nothing at run time and block nothing
([ADR 0001](adr/0001-toolchain.md)).

**There is therefore no standard to restrict. C++20 is permitted in full.**

What blocks is not the language but **library facilities**, because they import
functions Windows 95 does not export — and under Windows 95 a missing import stops
the process from starting, even if the function is never called.

## What is forbidden, and why

Every entry is measured, not presumed: the number is that of the symbols absent
from Windows 95 which a mere include is enough to bring into the import table
([E00-S01](research/win95-blockers.md), reproducible with
`tools/win95/probes/build-probes.sh`).

| Header | Absent symbols | Replacement |
|---|---:|---|
| `<thread>` | 6 | `CreateThread` through E02-S01's layer |
| `<mutex>` | 6 | `CRITICAL_SECTION` through E02-S01's layer |
| `<condition_variable>` | 6 | Win32 events through E02-S01's layer |
| `<shared_mutex>` | 6 | the same |
| `<future>` | 6 | the same |
| `<latch>`, `<barrier>`, `<semaphore>` | 6 | the same |
| `<filesystem>` | **13** | a file layer in `...A` (E02-S05) |

The threads group's six symbols are always the same:
`AddVectoredExceptionHandler`, `RemoveVectoredExceptionHandler`,
`GetTickCount64`, `IsDebuggerPresent`, `SetProcessAffinityMask` and
`TryEnterCriticalSection`. They are **supplied** by `platform/win95/` — but
`std::thread` does not work for all that: E00-S02 measured that a binary using it
loads and then fails in the thread part. Supplying the symbols settles the
loading, not the execution.

`<filesystem>` adds seven more gaps, among them volume enumeration and hard links,
all in `...W`.

### What is not forbidden, contrary to the ticket's expectation

| Header | Status | Proof |
|---|---|---|
| `<format>` | **permitted** | 0 absent symbols |
| `<ranges>`, `<span>`, `<bit>`, `<concepts>` | **permitted** | 0 absent symbols, purely compile-time |
| `<atomic>` | **permitted** | 0 absent symbols — GCC emits `lock cmpxchg` inline |
| `<chrono>` | **permitted** | 2 symbols, supplied by `platform/win95/` |

`<atomic>` deserves emphasis: Windows 95 does not export
`InterlockedCompareExchange`, and one might conclude from that that `std::atomic`
is dead. It is not — the compiler does not call the API, it emits the 486
instruction directly. Verified at run time on the emulated Pentium II,
`compare_exchange` and 64 bits included.

## Exceptions and RTTI: kept

**Decision: exceptions and RTTI stay enabled.**

E00-S02's T3b witness exercises both under Windows 95 — a virtual hierarchy, a
`typeid`, a `throw`/`catch` — and prints `RTTI     : ok` and
`exception: exception caught`.

Disabling them would reduce the binary's size, which counts for little against
[ADR 0003](adr/0003-memory-budget.md)'s 14 MiB of headroom, and would cost dearly:
`librecomp` and `ultramodern` depend on them, and removing them would force their
error handling to be rewritten — exactly the work this project sets out to avoid.

A detail worth knowing: it is **RTTI and exceptions** that make `libstdc++` import
`GetThreadId`. That is what explains why the T3b witness, which uses nothing but
`CreateThread`, at first failed to load. One therefore cannot escape the
compatibility layer by avoiding `std::thread`.

## Compilation status of the dependencies

Measured with
[E01-S01](stories/E01-build/E01-S01-cmake-i686-toolchain-without-sse.md)'s
toolchain. It is the only honest measure of progress here.

| Dependency | Files | Errors | Remaining |
|---|---:|---:|---|
| **`ultramodern`** | 15 | **0** | — |
| **`librecomp`** | 26 | **6** | see below |

### `ultramodern` compiles — one patch, two hunks

`patches/n64-modern-runtime/0014-build-for-windows-95-targets.patch`:

- **`std::quick_exit`** does not exist. mingw-w64 only declares it under `_UCRT`;
  the target links the legacy msvcrt, where it exists under no name at all.
  Replaced by `std::_Exit`, which the `__APPLE__` branch just above already
  chooses. The difference — the `at_quick_exit` handlers are not run — is nil
  here, none being registered.
- **`SetThreadDescription`** is from Windows 10. It does nothing but name a thread
  for a debugger, and there is none. The call is removed, not emulated.

The patch was verified on both targets: **0 errors on Windows 95, 0 errors on the
64-bit Linux host**. A patch that breaks upstream breaks the oracle.

E00-S01's prediction — "`ultramodern` gets patched, it does not get rewritten" —
is therefore confirmed by measurement: two hunks.

### `librecomp`: 6 errors, all accounted for

| Error | Files | Nature | Assigned to |
|---|---:|---|---|
| `static_assert(sizeof(std::size_t) == 8)` in `mods.hpp:53` | 4 | **a real 64-bit assumption** | E01-S02, to be patched |
| `rabbitizer.hpp` not found | 2 | dependency not fetched | E01-S05 |

**`mods.hpp`'s assertion** packs three values into a 64-bit `size_t` in order to
hash a mod hook definition. In 32-bit, the packing is impossible as it stands — it
would need a mixing function rather than a shift. It is in the **mod system**,
which [E00-S01](research/win95-blockers.md) already names as the first candidate
for a fork, and which the Windows 95 target does not need.

> **Correction.** `docs/research/win95-blockers.md` asserted that no 64-bit
> assumption remained in the code. That was false: the search used `sizeof(size_t)`
> without accepting the `std::` prefix. A corrected sweep over `ultramodern`,
> `librecomp`, `N64Recomp/include` and `runtime-recomp/src` finds **only one**,
> this one. The document is corrected.

**`rabbitizer.hpp`** is N64Recomp's MIPS instruction decoder, used by `mods.cpp`
and `recomp.cpp` for hot instruction rewriting. It is a dependency to fetch, not
an incompatibility.

### Two shims that are not patches

These two do not touch the dependencies' code, because they do not fix the
dependencies' code:

- **`platform/win95/include-shim/Windows.h`.** `ultramodern` writes
  `#include <Windows.h>` with a capital, which is Microsoft's usage and works on a
  case-insensitive file system. mingw-w64 supplies only `windows.h`, and Linux
  does not confuse the two. That is a property of the machine one compiles on, not
  of Windows 95.
- **`miniz_export.h`** is generated by miniz's CMake. Its absence is a matter of
  build configuration, to be settled by wiring miniz into the target.

## Checking

```sh
tools/win95/check-cpp-subset.py extern/n64-modern-runtime/ultramodern
tools/win95/check-cpp-subset.py --self-test
```

The checker runs as a pre-build step of the Windows 95 target and fails on a
forbidden header. It is put to the test by injection, like the two other guard
rails: a broken checker and a satisfied one keep quiet in the same way.
