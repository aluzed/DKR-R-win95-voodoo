# E00-S01 — Inventory of the dependencies incompatible with Windows 95

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | — |
| **Blocks** | E00-S02, E01-S02, E02-S01, E07-S03 |

## State as of 2026-08-12 — the inventory is done

Full results: [`docs/research/win95-blockers.md`](../../research/win95-blockers.md).

| Finding | Measurement |
|---|---|
| Win32 APIs called directly by the project | 20, **all present** under Win95 OSR2 |
| `...W` variants used | 5, **all stubs** (`ERROR_CALL_NOT_IMPLEMENTED`, verified in the disassembly) |
| `std::atomic` (64 bits included) | **0 blockers**, run on the real machine |
| `std::mutex` / `condition_variable` / `thread` | **6 blockers** (Vista condition variables) |
| `std::filesystem` | **13 blockers** |
| `ultramodern` | **6 files** affected, 12 `std::thread`, 5 `std::mutex` |
| `librecomp`'s `allocation_size` in 32-bit | **0** — silent truncation, the game dies at startup |
| The target's real ceiling | 1 GiB reserved, **256 MiB committed** (`mem_size` asks for 512) |
| SSE outside the paths already dealt with | none |

**Three conclusions change the plan:**

1. **The premise "C++20 rules out the compilers capable of targeting Win95" is
   false.** GCC 13 targets i686 PE32 and implements all of C++20. The problem is the
   *library*, not the language — which moves E01-S02 from a question of dialect to a
   question of hosting layer.
2. **`ultramodern` gets patched, it does not get rewritten** (6 files). E02-S01 was
   sized on the opposite hypothesis and must be cut down.
3. **E07-S02 is already done, or nearly**: ImGui, the texture packs and
   `runtime_ui.cpp` are excluded by `DKR_RUNTIME_BUILD_RT64=OFF`, and the survey of
   the preprocessor guards finds **no ImGui reference outside a guard**. Only four
   SDL2 references in two files remain.

The risk this ticket announced — "concluding that everything is to be thrown away" —
did not materialise: the recompiled code, the audio microcode, `std::atomic` and the
20 APIs called all pass unmodified.

## Context

DKR-R is a port by static recompilation designed for modern 64-bit systems. The
current stack is, from top to bottom:

| Layer | Component | Nature |
|---|---|---|
| Window / input / audio | SDL2 | external dependency |
| Interface | Dear ImGui (`runtime_ui.cpp`, 194 KB) | external dependency |
| Rendering | RT64 (`extern/rt64`) | D3D12 / Vulkan / Metal |
| N64 scheduling | `ultramodern` | C++20 |
| N64 loading / API | `librecomp` | C++20 |
| The game's CPU | N64Recomp output (`RecompiledFuncs`) | generated C |
| Audio microcode | RSPRecomp output (`RecompiledRSP/aspMain.cpp`) | generated C++ |

None of these layers was conceived for a Win32 from 1995. Before planning anything,
one must know **precisely** what falls and why: that is what distinguishes a
mandatory replacement from a mere adjustment.

## Objective

To produce `docs/research/win95-blockers.md`: the exhaustive list, component by
component, of what prevents compilation or execution under Windows 95, with for
each entry a verdict — **replace**, **patch**, or **keep**.

## Scope

**In:** static analysis of the sources and headers of the four dependencies pinned
in `dependencies.lock.json`, plus `runtime-recomp/src/game/`.

**Out:** any performance measurement (that is E00-S03 and E00-S04) and any writing
of replacement code.

## Work

1. Prepare the dependencies once (`Prepare-DKR-Runtime.cmd` or
   `scripts/bootstrap_dependencies.py`) so as to have the worktrees to analyse.
2. **Win32 API calls.** Extract every symbol imported from
   `kernel32`/`user32`/`advapi32` by `ultramodern`, `librecomp` and
   `runtime-recomp/src/game/`. Set each against Windows 95's real export table. The
   expected gaps, to be confirmed rather than assumed:
   - `TryEnterCriticalSection` — NT 4 / 98 and beyond;
   - `InitializeCriticalSectionAndSpinCount` — 98 / NT 4 SP3;
   - `SignalObjectAndWait` — NT 4;
   - the `SRWLOCK` / `CONDITION_VARIABLE` condition variables — Vista;
   - `GetTickCount64`, `GetModuleHandleEx` — Vista / XP;
   - the whole `...W` Unicode family, which is a stub under 9x.
3. **The C++ standard library.** Record the C++20 constructs that rule out the
   compilers capable of targeting Win95: concepts, `<ranges>`, `<span>`, `<bit>`,
   `consteval`, `<format>`, designated initialisers, and above all `std::thread` /
   `std::condition_variable` / `std::atomic` with `std::latch` or `std::jthread`.
   Count the occurrences per file — it is that count which will say whether
   `ultramodern` gets patched or rewritten.
4. **Instruction set.** Spot any use of SSE/SSE2/AVX, explicit (intrinsics) or
   implicit. Look in particular at `librecomp/include/librecomp/rsp_vu_impl.hpp`,
   included by `runtime-recomp/RecompiledRSP/aspMain.cpp:2`: the RSP's vector unit is
   very probably emulated there in SSE2, absent from the Pentium II.
5. **64-bit assumptions.** Look for `static_assert(sizeof(void*) == 8)`, for
   pointer↔`uint64_t` conversions, for hard-coded reserved address spaces
   (`librecomp` typically reserves RDRAM through a massive `mmap`/`VirtualAlloc`).
6. **External dependencies.** For SDL2, ImGui and RT64, rule on replacement rather
   than porting, and justify it in one line each.
7. Put every conclusion into the verdict table with the ticket that will deal with
   it.

## Acceptance criteria

- [ ] `docs/research/win95-blockers.md` exists and covers the seven points above.
- [ ] Every entry carries a verdict **replace / patch / keep**, a one-line
      justification, and the ticket that takes it on.
- [ ] The missing Win32 APIs are verified against a real export source (the export
      table of a Win95 OSR2.5 `kernel32.dll`, or the Win32 documentation's "Minimum
      supported client" column), not from memory.
- [ ] The count of C++20 constructs is given **per file** for `ultramodern` and
      `librecomp`, so that E01-S02 can decide between patch and rewrite on a figure.
- [ ] The document names explicitly the components that survive unmodified — that is
      where the project's retained value lies.

## Risks

The trap is to conclude that "everything is to be thrown away". The project's core —
the N64Recomp output, which is C generated from a MIPS ELF — has no reason to be
incompatible: it is portable C manipulating integers. If the inventory concludes on
a total replacement, it is wrong.

## References

- `dependencies.lock.json` — the four pinned dependencies
- `docs/ARCHITECTURE.md` — protected boundaries and layers
- `runtime-recomp/CMakeLists.txt:26-31` — C17 / C++20 standards required
- `runtime-recomp/RecompiledRSP/aspMain.cpp:1-2` — `librecomp` includes
