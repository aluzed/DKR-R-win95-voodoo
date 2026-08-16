# E01-S05 — Compiling the generated recompiled code

| | |
|---|---|
| **Epic** | E01 — 32-bit Windows 95 build chain |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E01-S01, E01-S02, E00-S06 |
| **Blocks** | E02-S06, E08-S02 |

## Context

This is the port's core: the thousands of C functions N64Recomp produces from the
decomp's ELF, plus the recompiled audio microcode. On the modern host they compile
without incident. In 32-bit, three questions stay open:

1. **The language.** The generated C manipulates the VR4300's registers as 64-bit
   integers. In 32-bit, every operation becomes a pair — the compiler does it, but it
   must be checked that it does not rely on an absent extension.
2. **The volume.** The number of generated files is of the order of a thousand, and
   `runtime-recomp/CMakeLists.txt` groups them into a single static library.
   Compilation time, the linker's memory and the final binary's size are all unknown
   on this target.
3. **The address space.** `librecomp` reserves RDRAM through a contiguous
   allocation. In 32-bit under Windows 95, user space is about 2 GB, fragmented, and
   the reservation must succeed reliably at launch.

## Objective

To compile `DKRRecompiledFuncs` and `RecompiledRSP` for the Win95 target, to measure
what that costs, and to make the result loadable within E00-S06's budget.

## Scope

**In:** compilation, linking, measuring size and time, reserving RDRAM.

**Out:** optimising run-time performance (E08-S02) and the RSP's vector emulation
(E03-S01).

## Work

1. Generate the sources by the existing path (`Build-DKR-Runtime.cmd` then
   `Generate-DKR-RSP.cmd`) and record the number of files and the volume of C
   produced. E01-S06 deals separately with the question of generating off Windows.
2. Compile `DKRRecompiledFuncs` with E01-S01's toolchain. Deal with the errors by
   family: the generated code is repetitive, and a single error repeats thousands of
   times and is fixed in one go.
3. Check the behaviour of the generated 64-bit arithmetic: register operations,
   shifts, multiplication and division, sign extension. A targeted test on a few of
   the game's arithmetic functions, compared against the 64-bit oracle's output, is
   worth more than a code review.
4. Compile `RecompiledRSP/aspMain.cpp`. It includes `librecomp/rsp_vu_impl.hpp`: if
   the vector emulation does not compile without SSE, put in a stub that compiles,
   leave the behaviour to E03-S01, and note it as an explicit debt.
5. Measure and record: full compilation time, the linker's peak memory, the size of
   the `.text` and `.rdata` sections, the binary's size.
6. If the link fails through memory exhaustion or by exceeding a format limit, split
   into several static libraries, or evaluate a separate DLL for the generated code.
   The split must stay automatic — a manual split does not survive the next
   regeneration.
7. Check that the RDRAM reservation succeeds under Windows 95: how much, at what
   address, and what the behaviour is on failure.
8. Set the size obtained against E00-S06's memory budget and report the discrepancy.

## Acceptance criteria

- [x] `DKRRecompiledFuncs` compiles in full for the Win95 target — 37/37.
- [x] `RecompiledRSP/aspMain.cpp` compiles, the vector emulation being
      **functional**: the scalar fallback suffices, no stub is put in.
- [~] The generated 64-bit arithmetic is verified by comparison against the oracle.
      **Agrees exactly on the functions it reaches**, faults included; the comparison
      stops on a fault Windows 95 does not deliver as a signal. See the note above.
- [x] Compilation time, peak memory and section sizes are measured and recorded.
- [x] The link produces a 32-bit PE that passes both guard rails — 4.23 MB, no
      instruction outside the Pentium II set, loadable under Windows 95.
- [ ] The RDRAM reservation is verified under Windows 95, with its failure behaviour.
- [ ] The size obtained is set against E00-S06's budget, and the discrepancy is
      reported.

## State as of 2026-08-13 — everything compiles and links; the comparison is partial

Full report:
[`docs/research/win95-recompiled-code.md`](../../research/win95-recompiled-code.md).

| | |
|---|---:|
| Generated files compiled | **37 out of 37**, zero errors |
| Compilation, in parallel | 5.76 s, peak of 142 MB per process |
| The linked PE's `.text` | 3,813 KB |
| Complete PE | **4.23 MB** |
| `aspMain.cpp` | compiles, **without a stub** — the scalar fallback suffices |
| Guard rails | instruction set **and** imports: passed |

Three results deserve noting.

**No absent extension.** The 64-bit arithmetic calls `__divdi3`, `__moddi3`,
`__ashrdi3` and their neighbours, all supplied by libgcc, which ADR 0001 already
links statically. That was the first of the ticket's three questions.

**Point 4 is moot.** `aspMain.cpp` compiles without SSE thanks to
`rsp_vu_impl.hpp`'s scalar fallback: there is no stub to put in and no debt to refer
to E03-S01. Its slowness stays E03-S03's subject, not the compilation's.

**The comparison against the oracle agrees on what it reaches** — bit-for-bit
identical digests, and down to the faults, which occur on both sides on the same
functions. It stops on `func_8001CD28`, whose fault is not delivered as `SIGSEGV`
under Windows 95 while it is on the host. It is not a stack overflow: a 64 MB reserve
changes nothing. What follows requires a driver that resumes after the current
function, which works on both targets without depending on what the system is
willing to deliver.

### `librecomp` compiles too, and both 64-bit assumptions fall

That was not this ticket's scope, but it was what blocked all the others, and the
wall turned out thinner than E01-S02's count suggested. **`librecomp`'s 26
translation units compile** for the target, with no instruction outside the Pentium
II set.

The "six errors" recorded were in reality **a single cause for five of them** —
`mods.hpp`'s `static_assert(sizeof(std::size_t) == 8)`, which travelled up through
includes — plus two missing include paths and a header miniz's CMake manufactures.
Two patches suffice:

- **0016** — `HookDefinition`'s hash is assembled in a `uint64_t` then folded if
  `size_t` is narrower, instead of requiring 64 bits; and `patch_func` receives its
  i386 trampoline, `mov eax, imm32 ; jmp eax`, the exact counterpart of the x86_64
  variant's other six bytes. The 64-bit hash is unchanged, verified over two million
  triples.
- **0017** — `allocation_size` was `4096ULL * 1024 * 1024`, which **is exactly
  zero** when `size_t` is 32 bits: `VirtualAlloc` reserved nothing, failed, and the
  game stopped on "Failed to allocate memory" before the first frame. The sizes now
  follow the target, according to what
  [ADR 0003](../../adr/0003-memory-budget.md) had settled — 4 MiB committed, 8 MiB
  reserved, so that the trap for invalid accesses stays armed.

The recompiled code's image measured here, **3.81 MiB of `.text`**, confirms the
3.85 MiB estimate on which that ADR built its budget.

**What remains out of reach**: point 7, the RDRAM reservation under real conditions,
and point 8, the comparison against E00-S06's budget. Both require the game linked
for real, which still awaits `librecomp` (E02-S05) and the SDL2 decoupling
(E07-S03). E00-S01 has already measured the ceilings — 1 GiB of reservation, 256 MiB
of commit, `librecomp`'s scheme working at 8 MiB.

## Risks

If the binary greatly exceeds the physical memory available, Windows 95 will page
during the race, and paging on a 1998 disk makes the game unplayable regardless of
any CPU optimisation. That figure must therefore be measured early: it can on its own
reopen the hardware-floor ADR (E00-S05).

## References

- `runtime-recomp/CMakeLists.txt:80-120` — the `DKRRecompiledFuncs` library
- `runtime-recomp/CMakeLists.txt:122-130` — generated RSP sources
- `docs/BUILDING.md` — the generation path
- `Generate-DKR-RSP.cmd`, `Diagnose-DKR-Recompile.cmd`
