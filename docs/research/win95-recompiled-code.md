# What the recompiled code costs on the Windows 95 target

Measurements from
[E01-S05](../stories/E01-build/E01-S05-compiling-the-recompiled-code.md), taken on
the generated sources present in the repository.

## The volume

| | |
|---|---:|
| Generated `.c` files | 37 |
| Lines of C | 587,625 |
| `RECOMP_FUNC` functions | 1,810 |
| of which **leaves** (no call, no indirect jump) | 722 |

## Compilation and linking

E01-S01's toolchain, `-O2 -march=pentium2 -mno-sse`, on the build machine:

| | |
|---|---:|
| Compilation of the 37 files, in parallel | **5.76 s** |
| Peak memory of one compilation process | **142 MB** |
| Static archive | 4.5 MB |
| Cumulative `.text` of the objects | 3.81 MB |
| Linked PE, with the harness and the stubs | **4.23 MB** |
| The PE's `.text` | 3,813 KB |
| `.rdata` | 37.7 KB |
| `.data` + `.bss` | 2.8 KB |

**Nothing resisted**: 37 files out of 37, zero errors. The `__int128` type
blockage, which E00-S03 had met and fixed with the `n64recomp/0002` patch, does
not recur.

`RecompiledRSP/aspMain.cpp` compiles as well — 1.73 s, 33 KB of `.text` — and
**without a stub**: `rsp_vu_impl.hpp`'s scalar fallback works without SSE. The
ticket's point 4, which planned to put one there and refer the behaviour to
E03-S01, is moot. That said, this code is 25 times too slow for real time
([E00-S04](rsp-audio-budget.md)): it is E03-S03 that must answer, not the
compilation.

Both guard rails pass on the final PE: **no instruction outside the Pentium II
set**, and **loadable under Windows 95**.

## The 64-bit arithmetic goes through libgcc

The generated C manipulates the VR4300's registers as 64-bit integers. In 32-bit,
the divisions and shifts do not inline: they call the compiler's helpers. Among
the 154 symbols the generated code leaves undefined, one finds `__divdi3`,
`__moddi3`, `__divmoddi4`, `__udivdi3`, `__umoddi3`, `__ashldi3`, `__lshrdi3`,
`__ashrdi3` — **all supplied by libgcc**, which ADR 0001 already links statically.

The other 146 are `librecomp`'s helpers (`__ll_mul_recomp`,
`__osContRamRead_recomp`…) and the project's hooks. None is missing in the proper
sense: they come from the runtime, not from the generated code.

There is therefore **no absent extension** — that was the first of the ticket's
three open questions.

## The comparison against the oracle

The ticket asks for better than a code review: "a targeted test on a few of the
game's arithmetic functions, compared against the 64-bit oracle's output".

`tools/win95/oracle/` does that. 96 **leaf** functions — chosen automatically as
the richest in arithmetic — are run on an entirely determined state: RDRAM filled
by a fixed-seed generator, registers drawn from the same generator and pointing
into the region. The final state is summarised by an FNV-1a digest.

Two traps were worth noting, because they made the comparison silently hollow:

- **`f_odd` is a pointer**, not an array. The digest therefore cannot bear on the
  raw structure: its value changes on every run, and `sizeof(recomp_context)`
  **differs between 32 and 64 bits**. The state is summarised field by field, at
  fixed width.
- The functions that leave the region corrupted the heap and killed the program
  much later, in an unrelated place. RDRAM is now **framed by forbidden pages** —
  the scheme `librecomp` uses for real — and the fault becomes immediate and
  attributed to the right function.

### Result

The 64-bit oracle is **deterministic**: three runs, identical digests. On the
target, the comparison **agrees exactly on the functions it reaches** — digests bit
for bit, and down to the faults, which occur on both sides on the same functions:

| Function | 64-bit | 32-bit |
|---|---|---|
| `func_80014B50` | `4C4E693D94850625` | `4C4E693D94850625` |
| `collision_get_y` | `F1415FD72BFC0752` | `F1415FD72BFC0752` |
| `obj_animate` | `FAULT` | `FAULT` |

### What stops it going the distance, and why that is not a defect of the port

Catching the faults rests on `signal(SIGSEGV)`. On the host it is reliable — an
alternate stack even settling stack overflow, which required `sigsetjmp` with mask
saving, `longjmp` from a handler otherwise leaving the signal blocked.

**Under Windows 95 it is not.** `obj_animate`'s fault is duly delivered;
`func_8001CD28`'s is not, and the process dies. It is not a stack overflow — a
64 MB reserve changes nothing — but a fault mingw's CRT does not translate into a
signal on this target.

The comparison therefore stops at the first function of that kind. **What is known
of the functions it reaches stays true**: their 64-bit arithmetic produces a bit-
for-bit identical state on both targets.

To go the distance, the route is a resuming driver: the program notes the function
it is about to run, a relaunch restarts after it, and the list of unrunnable
functions becomes data. That works on both targets without depending on what
Windows 95 is willing to deliver.

## Reproducing

```sh
# 1. compile the recompiled code for the target
i686-w64-mingw32-gcc-posix -std=c17 -O2 -march=pentium2 -mno-sse \
  -D_WIN32_WINNT=0x0400 -fno-strict-aliasing -c runtime-recomp/RecompiledFuncs/funcs_0.c ...

# 2. the stubs come from the linker's verdict, not from a guessed list
LC_ALL=C <link that fails> 2>&1 | tools/win95/oracle/generate-stubs.py > stubs.c

# 3. regenerate the function selection
tools/win95/oracle/functions.inc   # 96 leaves, the most arithmetic ones
```

The reference digests are in `tools/win95/oracle/digests-64bit.txt`.
