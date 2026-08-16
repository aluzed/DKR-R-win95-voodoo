# What stops DKR-R from running under Windows 95

An inventory from
[E00-S01](../stories/E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md).
Date: 2026-08-12.

## Summary

| Component | Verdict | Why | Ticket |
|---|---|---|---|
| N64Recomp output (`RecompiledFuncs`) | **keep** | portable integer C; already compiles and runs in 32-bit without SSE | — |
| Audio microcode (`aspMain.cpp`) | **keep** *(it compiles)* | automatic scalar fallback — but 25× too slow, see [E00-S04](rsp-audio-budget.md) | E03-S03 |
| `std::atomic` | **keep** | no system call at all: the compiler emits `lock cmpxchg` / `cmpxchg8b` inline | — |
| The runtime's direct Win32 calls | **keep** | all 20 APIs called exist under Win95 OSR2 | — |
| `std::mutex` / `condition_variable` / `thread` | **replace** | pull in 6 absent APIs, among them Vista's condition variables | E02-S01 |
| `std::filesystem` | **replace** | pulls in 13 absent APIs; 400 call sites | E01-S03 |
| `librecomp`'s RDRAM constants | **patch** | `allocation_size` is **0** in 32-bit; `mem_size` exceeds the measured ceiling | E00-S06 |
| The `...W` Unicode APIs | **replace** with `...A` | exported but **stubs**: return 0 and `ERROR_CALL_NOT_IMPLEMENTED` | E01-S03 |
| RT64 | **keep, switched off** | already behind `DKR_RUNTIME_BUILD_RT64`, `OFF` by default | E04-S01 |
| Dear ImGui | **remove** | 97 % in a single file, already excluded when RT64 is off | E07-S02 |
| SDL2 | **replace** | 94 % in three files; no viable Win95 port | E07-S03 |

**The prognosis is better than the ticket feared.** The project's core — the
recompiled code — passes unmodified. What falls is the *hosting* layer: threads,
file system, window. And most of the blockers come down to **a handful of
functions**, not to a rewrite.

## Method

The ticket required the missing APIs to be checked "against a real export source,
not from memory". That is what was done, and one step further:

1. The six system DLLs were **extracted from the test machine itself**
   (`C:\WINDOWS\SYSTEM`) and their export tables analysed:

   | DLL | Size | Date | Exports |
   |---|---:|---|---:|
   | `KERNEL32.DLL` | 422,400 | 1996-08-24 | 682 |
   | `MSVCRT.DLL` | 280,576 | 1997-11-03 | 756 |
   | `USER32.DLL` | 44,544 | 1996-08-24 | 580 |
   | `GDI32.DLL` | 131,072 | 1996-08-24 | 330 |
   | `ADVAPI32.DLL` | 43,008 | 1996-08-24 | 224 |
   | `WINMM.DLL` | 49,152 | 1996-08-24 | 182 |

   That is **2,754 reference symbols**, proper to *this* installation.

2. Rather than guess what the code calls, **probes were compiled** for i686 with
   GCC 13 and their **PE import table** compared against that reference. That is
   the right granularity: Windows 95 resolves *every* import at load time, so a
   missing symbol is fatal even if the function is never called.

3. The probes were then **run on the real machine**, to check that the prediction
   comes true.

### Reproducing

```sh
tools/win95/check-win95-imports.sh --refresh   # extracts the reference from the VM's DLLs
tools/win95/probes/build-probes.sh             # matrix, win32 threading model
tools/win95/probes/build-probes.sh posix       # matrix, winpthreads model
tools/win95/check-win95-imports.sh build/DKR-R.EXE   # checking one binary
```

The checker returns a non-zero exit code if a symbol is missing: it is usable as
it stands as a build guard rail
([E01-S04](../stories/E01-build/E01-S04-pe-import-guard-rail.md)).

## 1. The runtime's direct Win32 calls — **no blockers**

Crossing the identifiers `ultramodern`, `librecomp` and `runtime-recomp/src/game/`
call against the 2,783 APIs decorated with a DLL import in mingw's headers gives
**20 system APIs actually called**:

```
CloseHandle  CreateProcessW  FillRect  FreeLibrary  GetCommandLineW
GetCurrentProcess  GetCurrentThread  GetCurrentThreadId  GetLastError
GetLogicalDrives  GetModuleHandleW  GetProcAddress  LoadLibraryExW
MoveFileExW  SetThreadPriority  SetUnhandledExceptionFilter  Sleep
VirtualAlloc  VirtualFree  VirtualProtect
```

**All twenty are exported by Win95 OSR2.** The code the project writes poses no API
problem at all. That is an important result: it locates the work elsewhere.

### But the five `...W` variants are decoys

`CreateProcessW`, `GetCommandLineW`, `GetModuleHandleW`, `LoadLibraryExW` and
`MoveFileExW` do appear in the export table. They do nothing.

The disassembly shows it unambiguously. Every `...W` entry is packed into ~150
bytes around RVA `0x034fb9`, and each fits in three instructions:

```asm
34fef:  33 c0           xor    eax,eax      ; return value = 0 (failure)
34ff1:  b1 03           mov    cl,0x3       ; stub index
34ff3:  e9 21 c3 fc ff  jmp    0x1319       ; common tail
```

And the common tail:

```asm
1319:   51              push   ecx
131a:   68 78 00 00 00  push   0x78         ; 120 = ERROR_CALL_NOT_IMPLEMENTED
131f:   e8 be c7 00 00  call   0xdae2       ; SetLastError
```

A revealing detail: `LoadLibraryExW` and `MoveFileExW` **share the same address**
(`0x034fef`). Two functions with radically different behaviour point at the same
code — because neither has any.

**Verdict:** switch to the `...A` variants. It is mechanical, but it must be done
consciously: the link succeeds, the load succeeds, and the failure occurs at run
time, silently.

## 2. The C++ standard library — the real seam

The ticket assumed that C++20 constructs "rule out the compilers capable of
targeting Win95". **That is false, and it is good news.** GCC 13 targets i686 PE32
and implements all of C++20; concepts, `<ranges>`, `<span>`, `consteval` and
`operator<=>` cost nothing at run time.

The problem is not the language, it is the **library**: which facilities pull in
APIs Win95 does not have. Seven probes, compiled identically
(`-march=pentium2 -mno-sse -static`), give the answer:

| Probe | Imports | Blockers | Detail |
|---|---:|---:|---|
| `printf` alone | 53 | **0** | reference |
| `std::atomic` | 53 | **0** | including `compare_exchange` and 64 bits |
| `std::chrono` | 68 | 2 | `GetThreadId`, `TryEnterCriticalSection` |
| `std::mutex` | 86 | 6 | + the 4 condition variables |
| `std::condition_variable` | 87 | 6 | the same |
| `std::thread` | 107 | 7 | + `_fstat64` |
| `std::filesystem` | 131 | **13** | + volumes, hard links, `stat64` |

The threads group's six blockers:

```
InitializeConditionVariable   Vista (2006)
SleepConditionVariableCS      Vista
WakeConditionVariable         Vista
WakeAllConditionVariable      Vista
GetThreadId                   XP
TryEnterCriticalSection       98 / NT 4
```

The seven `std::filesystem` adds:

```
FindFirstVolumeW  FindNextVolumeW  FindVolumeClose  CreateHardLinkW
GetFileSizeEx     _wstat64         _fstat64
```

### `std::atomic` passes, and it is verified on the machine

Counter-intuitive: Win95 exports only three `Interlocked` functions (`Increment`,
`Decrement`, `Exchange`) — neither `CompareExchange` nor `ExchangeAdd`. One might
conclude from that that `std::atomic` is dead.

It is not: GCC does not call those APIs, it emits `lock cmpxchg` (486+) and
`cmpxchg8b` (Pentium+) directly. The `std::atomic<int>` +
`compare_exchange_strong` + `std::atomic<long long>` probe **runs correctly on the
emulated Pentium II** and prints `2 1`, the expected value.

That is the kind of conclusion one does not draw from a compatibility table.

### The trap the probes could not see: the empty exports

> **Added on 2026-08-12, by
> [E02-S01](../stories/E02-system/E02-S01-threading-and-synchronisation-layer.md).**

The import-table method answers one question — "does this symbol exist?" — and
**the limit announced at the end of this document has materialised**: it says
nothing about what happens when the symbol exists and the function does nothing.

`moodycamel::LightweightSemaphore` — the blocking primitive on which *all* of
`ultramodern`'s scheduler rests — calls **`CreateSemaphoreW`**. The symbol is
exported by the machine's `KERNEL32.DLL`. Its code:

```asm
0x03500a:  33 c0              xor  eax,eax     ; return 0
           b1 04              mov  cl,0x4      ; stub index
           e9 06 c3 fc ff     jmp  0x1319      ; common tail
0x001319:  51                 push ecx
           68 78 00 00 00     push 0x78        ; ERROR_CALL_NOT_IMPLEMENTED
           e8 be c7 00 00     call SetLastError
```

It shares its address with `CreateEventW`. `CreateSemaphoreA`, conversely, is real
code.

**Both sides of the semaphore break, and differently:**

| | Consequence of a null handle |
|---|---|
| `wait()` | `WaitForSingleObject(NULL, INFINITE)` fails instead of blocking. `ultramodern` **ignores the return value** of `running.wait()`: the thread carries on as if it had been woken. The game threads, which must run one at a time, then all run together — non-deterministic corruption, not a crash. |
| `signal()` | `while (!ReleaseSemaphore(NULL, ...));` — a loop that never terminates. The same scheduler starvation as the first version of `TryEnterCriticalSection`: the whole machine freezes. |

moodycamel's `assert(m_hSema)` is compiled out of the binary in Release.

**The scale of the phenomenon.** The pattern is recognisable mechanically, hence
countable. On the DLLs extracted from the machine:

| DLL | Stubs | Named exports | Share |
|---|---:|---:|---:|
| `ADVAPI32` | 176 | 224 | **79 %** |
| `KERNEL32` | 179 | 682 | 26 % |
| `USER32` | 162 | 580 | 28 % |
| `GDI32` | 62 | 330 | 19 % |
| `MSVCRT`, `WINMM`, `CRTDLL` | 0 | — | — |

`ADVAPI32` at 79 % is a warning for any ticket that would aim at it: under Windows
95 it is almost entirely decorative.

**The guard rail existed and could see nothing**, since the symbol *is* exported.
It is now doubled by a stub survey, produced by `tools/win95/find_stubs.py` and
stored in `tools/win95/exports/stubs/`. A binary that imports an empty entry fails
the check, with the offending object's name.

Two stubs were already imported by E01-S03's binaries, and the new check flushed
them out:

| Symbol | Required by | Verdict |
|---|---|---|
| `GetModuleHandleW` | `libmsvcrt` (`_vscprintf`, `_scprintf`, `wassert`) | **benign, and verified** — the disassembly of `_init_vscprintf` shows the failure is anticipated: a null return makes mingw branch onto its own `_emu_vscprintf` implementation. The stub selects the portable fallback. |
| `GetHandleInformation` | `libwinpthread` (`thread.o`, `sched.o`) | **not benign** — `pthread_join` tests the return value and takes its error path. **`std::thread::join()` therefore throws an exception under Windows 95.** Tolerated in `WITNESS.EXE` alone, whose role is to exercise the standard model. |

The second is a blocker in itself, and it strengthens E02-S01: `dkr_thread_join`
goes through `WaitForSingleObject` and depends on no stub.

### The threading model moves the problem without solving it

Two models exist for mingw. Both were installed and measured:

| Probe | `win32` model | `posix` model (winpthreads) |
|---|---:|---:|
| `printf`, `std::atomic` | 0 | 0 |
| `std::mutex`, `condition_variable` | 6 | 6 |
| `std::thread` | 7 | 6 |
| `std::chrono` | 2 | 6 |
| `std::filesystem` | 13 | 13 |

The same count, a different nature. The `posix` model eliminates Vista's condition
variables, but brings others in:

```
AddVectoredExceptionHandler  RemoveVectoredExceptionHandler   XP
GetTickCount64                                                Vista
IsDebuggerPresent                                             98 / NT 4
SetProcessAffinityMask                                        NT
TryEnterCriticalSection                                       98 / NT 4
```

**The `posix` model is preferable**, because its gaps are superficial:
`IsDebuggerPresent` returns false, `SetProcessAffinityMask` does nothing,
`GetTickCount64` wraps around `GetTickCount` while handling the 49-day wraparound,
the vectored exception handlers fall back on `SetUnhandledExceptionFilter` — which
does exist. Whereas reproducing Vista's condition variables on Win95 events is a
delicate exercise, in which wake-ups get lost.

**Verdict: write a small compatibility library** that supplies those six entries,
and place it before `libkernel32.a` in the resolution order. That is not a rewrite
of `ultramodern` — it is a page of code. `TryEnterCriticalSection` is the only
point that demands attention, and it is implemented on `InterlockedExchange`,
which is present.

### Where the usage is concentrated

Counted per file, so that E01-S02 can decide on a figure:

**`ultramodern`** — 15 `.cpp` files, **6 files** touch the blocking primitives:

| File | Usage |
|---|---|
| `src/events.cpp` | `std::thread`×6, `std::mutex`×1 |
| `src/threads.cpp` | `std::thread`×3 |
| `src/timer.cpp` | `std::thread`×2 |
| `src/renderer_context.cpp` | `std::mutex`×3 |
| `src/extensions.cpp` | `std::mutex`×1 |
| `include/ultramodern/ultramodern.hpp` | `std::thread`×1, `std::filesystem`×1 |

**That is small.** `ultramodern` gets patched, it does not get rewritten — 12
`std::thread` and 5 `std::mutex` in all. The conclusion is worth setting down in
black and white, because ticket E02-S01 was sized on the opposite hypothesis.

**`librecomp`** — 26 `.cpp` files, dominated by `std::filesystem` (101 uses):
`mods.hpp`×21, `mods.cpp`×20, `mod_manifest.cpp`×15, `files.cpp`×14,
`recomp.cpp`×13, `mod_config_api.cpp`×6, `pi.cpp`×6. Two thirds serve the mod
system, which the Win95 port does not need.

**`runtime-recomp/src`** — 299 uses of `std::filesystem`, but **139 (46 %)
disappear** when RT64 is switched off (see §5). 160 remain, concentrated in
`save_manager.cpp`×71 and `virtual_pak.cpp`×24 — those are necessary.

## 3. Instruction set — content

No use of SSE/AVX outside the paths already dealt with:

| File | Occurrences | Status |
|---|---:|---|
| `thirdparty/sse2neon/sse2neon.h` | 1,377 | ARM only, never compiled on x86 |
| `librecomp/include/librecomp/rsp_vu_impl.hpp` | 277 | under `ARCHITECTURE_SUPPORTS_SSE4_1`, automatic SISD fallback |
| `thirdparty/xxHash/xxhash.h` | 67 | run-time dispatch, scalar by default |
| `N64Recomp/lib/tomlplusplus` | 11 | detection only |

[E00-S03](cpu-budget.md) compiled the recompiled code for the Pentium II and
verified in the disassembly that it **emits no SSE instruction**. Nothing to do
here.

## 4. 64-bit assumptions — one serious blocker, and only one

> **Correction of 2026-08-12.** This paragraph asserted that no 64-bit assumption
> remained. That was false: the search used `sizeof(size_t)` without accepting the
> `std::` prefix, and therefore missed
> `static_assert(sizeof(std::size_t) == 8)` in
> `librecomp/include/librecomp/mods.hpp:53` — revealed by actually compiling
> `librecomp` for the target ([E01-S02](../CPP-SUBSET.md)). A corrected sweep over
> `ultramodern`, `librecomp`, `N64Recomp/include` and `runtime-recomp/src` finds
> **only one**, that one. It packs three values into a 64-bit `size_t` in order to
> hash a mod hook definition, and lies in the mod system — already the first
> candidate for a fork.

Searching for `static_assert(sizeof(void*) == 8)`, for pointer↔`uint64_t`
conversions and for other assumptions about `size_t` turns up **nothing more**. The
`0xFFFFFFFF80000000` constants scattered through `recomp.h` are *guest* addresses,
computed as `uint64_t` then used as indices into `rdram`: correct in 32-bit.

The problem is elsewhere — `librecomp/include/librecomp/addresses.hpp`:

```cpp
constexpr size_t mem_size        =  512ULL * 1024ULL * 1024ULL;
constexpr size_t allocation_size = 4096ULL * 1024ULL * 1024ULL;
```

In 32-bit, `size_t` is 4 bytes. GCC flags it, but only as a warning:

```
warning: conversion from 'long long unsigned int' to 'size_t' {aka 'unsigned int'}
changes value from '4294967296' to '0' [-Woverflow]
```

**`allocation_size` is 0.** `recomp.cpp:773` therefore calls
`VirtualAlloc(nullptr, 0, ...)`, which fails, and the game stops on "Failed to
allocate memory". It compiles, it links, and it dies at startup.

Verified on the real machine, which also answers the question the source code
cannot settle:

| Quantity | Measurement on the target |
|---|---:|
| `sizeof(size_t)` | 4 |
| `allocation_size` evaluated | **0** |
| `mem_size` evaluated | 536,870,912 |
| Physical / available RAM | 63 MiB / 47 MiB |
| Virtual address space | 2,044 MiB |
| Allocation granularity | 65,536 |
| **Maximum reservation** | **1,024 MiB** |
| **Maximum read/write commit** | **256 MiB** |
| `librecomp`'s scheme at 8 MiB | **OK** |

Two lessons for
[E00-S06](../stories/E00-scoping/E00-S06-adr-memory-budget.md):

- even corrected for the truncation, the 4 GiB reservation is impossible: the
  measured ceiling is 1 GiB;
- **`mem_size` at 512 MiB would fail too**: the read-write commit tops out at
  256 MiB on a 64 MiB machine.

`librecomp`'s exact scheme, on the other hand — reserve, commit a window,
`VirtualProtect` — **works at 8 MiB**, that is precisely the RDRAM of an N64 with
the Expansion Pak. The fix is therefore to make those two constants
target-dependent, not to change the mechanism.

## 5. External dependencies

### RT64 — **keep, switched off**

Nothing to remove: the renderer is already optional.

```cmake
option(DKR_RUNTIME_BUILD_RT64 "Also configure and compile the pinned RT64 renderer" OFF)
```

And the `if(DKR_RUNTIME_BUILD_RT64)` block excludes, along with it,
`f3ddkr_rt64.cpp`, `rt64_renderer.cpp`, `runtime_crt_overlay.cpp`,
`runtime_rice_texture_import.cpp`, `runtime_texture_packs.cpp`, `runtime_ui.cpp`
and the ImGui/SDL bridge.

**A direct consequence: a good part of
[E07-S02](../stories/E07-scope/E07-S02-dropping-imgui-texture-packs-telemetry.md)
is already done by a switch that exists.** The ticket must be cut down.

RT64 requires D3D12, Vulkan or Metal; none exists under Windows 95, and a Voodoo
speaks nothing but Glide. There is no question of porting it — only of not
switching it on. That is also what protects the modern target: it keeps switching
it on, knowing nothing of the Win95 work.

### Dear ImGui — **remove**, and it is already done

1,089 references, of which **1,054 in `runtime_ui.cpp` alone** (97 %), already
excluded when RT64 is off. The rest — 26 lines in `runtime_platform.cpp`, which
copy the controller's state into `ImGuiIO` — is **entirely under
`#if DKR_RUNTIME_HAS_RT64`**, the `imgui.h` include included.

Verified by following the nesting of the preprocessor directives line by line, and
not by a pattern search: **0 ImGui references outside a guard**. There is nothing to
decouple.

### SDL2 — **replace**, but the scope is tiny

343 references in 10 files, of which `runtime_platform.cpp` (177),
`runtime_input.cpp` (81) and `runtime_ui.cpp` (66). The same survey of the guards
shows that **only four references** escape `DKR_RUNTIME_HAS_RT64`:

| Location | Nature |
|---|---|
| `runtime_input.cpp:571-572` | `SDL_GameController*` in `input::poll`'s **public signature** |
| `runtime_enhancements.cpp:10` | unconditional `#include <SDL.h>` |
| `runtime_enhancements.cpp:461,468` | `SDL_Window*` and `SDL_GetWindowSize` |

SDL2 no longer has a Windows 95 port, and the project uses only its window,
keyboard, controller and audio — that is, exactly what E06 plans to rewrite in bare
Win32. But the decoupling to do comes down to **two files**, because the bulk of
the work has already been done by the RT64 switch.

## 6. What survives unmodified

This is the part the ticket asked to be named explicitly, because it is the
project's retained value:

- **All of N64Recomp's output** (`RecompiledFuncs`, 3,823 functions): integer C,
  with no system dependency. It compiles for the Pentium II and runs — that is
  what [E00-S03](cpu-budget.md) measured.
- **The recompiled audio microcode**: compiles in 32-bit without SSE, without a
  line to change, thanks to `rsp_vu.hpp`'s scalar fallback. It is 25 times too slow
  for real time ([E00-S04](rsp-audio-budget.md)), but it stays exact — it is
  E03-S03's oracle.
- **`std::atomic`**, 64 bits included — verified on the machine.
- **The 20 Win32 APIs the project calls.**
- **`std::chrono`**, bar two functions, themselves trivial to supply.
- **RT64 and ImGui**, which need not be touched: it is enough not to compile them.

## Limits

- The inventory bears on the **sources**, not on a complete compilation of
  `ultramodern` and `librecomp` for i686 — that is E00-S02. Other imports may
  appear at the real link; the import-table method will reveal them at the first
  attempt.
- The export reference is **this** installation's (Win95 OSR2 plus a `MSVCRT.DLL`
  from November 1997). A real machine of the same age may differ slightly —
  [E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md) will say.
- The per-file counting rests on regular expressions, not on the syntax tree: it
  gives a reliable order of magnitude, not an exact count.
- The probes measure what the standard library **imports**, which is the right
  criterion under Win95 since loading fails on a missing symbol. They say nothing
  about what would happen if one supplied those symbols and the semantics differed
  — that is the risk proper to the compatibility library, to be covered by tests.

  > **This limit has materialised**, and in a form the paragraph above anticipated
  > only halfway: the danger did not come from a symbol *we* supplied with
  > different semantics, but from a symbol **Windows 95 itself** supplies empty. See
  > "the trap the probes could not see", §2. The import check is corrected
  > accordingly.
