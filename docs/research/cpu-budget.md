# CPU cost of the recompiled code in 32-bit without SSE

Measurements from
[E00-S03](../stories/E00-scoping/E00-S03-spike-recompilation-cpu-budget.md).
Date: 2026-08-11.

## Summary

| Quantity | Measurement |
|---|---|
| x86 expansion per MIPS instruction, 64-bit | **2.74** |
| x86 expansion per MIPS instruction, 32-bit without SSE | **3.89** |
| Overhead in instruction count | **1.42×** |
| Overhead in code size (`.text`) | **1.25×** |
| Overhead in execution time, median | **2.16×** |
| Overhead in execution time, range | 1.54× to 3.00× |
| Normalisation 32-bit host → Pentium II 400 MHz | **17.7×** (range 15.6× to 21.7×) |
| Overall factor, modern 64-bit core → target | **≈ 38×** |

**The recompiled code compiles and runs in 32-bit without SSE.** A single blockage
was met, it is fixed, and it was not in the game's code.

The factor that was missing — the normalisation towards the target machine — **is
now measured**, on the emulated Pentium II, with the same code and the same inputs
as on the host. The go/no-go remains unpronounced, however: what it now lacks is a
denominator, the real CPU cost of a game frame. See "What is missing to conclude".

## What was built in order to measure

The measurement required real generated code, which did not exist:
`RecompiledFuncs` is absent from the repository and producing it went through
Windows.

| Step | Result |
|---|---|
| MIPS toolchain, cmake, ninja, without root | binutils extracted from `.deb` packages into a user prefix |
| Reference decomp ELF | `dkr.us.v77.elf`, 10.9 MB, 3,994 symbols |
| ROM produced by that build | SHA-1 `0cb115d8…b9670`, **identical to the player's ROM** — a matching build |
| N64Recomp configuration | `scripts/generate_recomp_toml.py`, a Linux port of the PowerShell script's TOML step |
| Generated code | **37 files, 587,625 lines of C, 20 MB, 1,810 functions emitted** |

The neighbouring decomp gave its MIPS toolchain up as absent; it is not any more,
and the project's oracle is buildable under Linux.

## The blockage met, and why it condemns nothing

In 32-bit, the compilation stops before the first function:

```
recomp.h:70:2: error: #error "128-bit integer type not found"
```

`recomp.h` implements the MIPS `DMULT`/`DMULTU` instructions with `__int128`, with
a fallback on MSVC's `_mul128` intrinsics. Neither exists in 32-bit. Since Windows
95 is 32-bit by definition, that blocked the whole target.

Two measured facts changed the problem's scope:

- that type is used **only** by `DMULT` and `DMULTU`, nowhere else;
- **DKR never calls them**: zero occurrences of either across the 3,823 recompiled
  functions.

A stub would therefore have sufficed. That is precisely why none was put in: the
function would have stayed wrong for any later port whose game does use them. The
`patches/n64recomp/0002-portable-128-bit-multiply-for-32-bit-targets.patch` patch
implements the long multiplication from four 32-bit partial products, with a sign
correction on the high word.

Verified against the `__int128` implementation on x86-64: 10,000,000 random pairs
plus the complete Cartesian product of the boundary values for sign and overflow,
that is **20,000,200 comparisons, zero deviations**.

Once that patch is applied, **all 37 files out of 37** compile in both
configurations.

## Absence of SSE, verified

The complete disassembly of the 32-bit objects contains **no** SSE instruction —
neither `movss`/`movsd`, nor `cvtsi2sd`, nor `pxor`/`movaps`. The
`-march=pentium2 -mfpmath=387 -mno-sse` options hold across all the generated
code.

The profile of the emitted instructions matches what one expects of an emulation
of 64-bit registers on a 32-bit machine:

| Instruction | Occurrences | What it translates |
|---|---|---|
| `mov` | 499,015 | moving the word pairs that form a VR4300 register |
| `sar` | 49,078 | sign extension, ubiquitous in the MIPS instruction set |
| `fstp`, `fucomi`, `jp` | ~43,700 | floating-point arithmetic on the x87 stack |

## Execution time

### Method

A bench loads the matching ELF's `PT_LOAD` segments into a 512 MiB memory image
covering the whole KSEG0 window, then calls real **leaf** functions of the game —
those that call no other function, hence measurable in isolation. Of the 722 leaves
available, the 48 largest were retained, and 24 run without faulting against a
static memory image.

The two binaries are built from the same sources, with the same options apart from
`-m32 -march=pentium2 -mfpmath=387 -mno-sse`. Three campaigns of 3,000,000 calls
per function; the minimum of the three is kept.

A first set of measurements had to be thrown away: the `sigsetjmp` resume point was
placed inside the timed loop, and its signal-mask saving makes a system call. The
times were then uniform at ~226 ns, that is the harness's cost and not that of the
code being measured.

### Results

| Function | 64-bit | 32-bit | Factor |
|---|---:|---:|---:|
| `search_level_properties_forwards` | 5.40 ns | 16.20 ns | 3.00× |
| `slowly_change_fog` | 23.90 ns | 59.80 ns | 2.50× |
| `particle_allocate` | 3.80 ns | 9.40 ns | 2.47× |
| `apply_vehicle_rotation_offset` | 4.30 ns | 10.40 ns | 2.42× |
| `__sinf_recomp` | 4.70 ns | 11.00 ns | 2.34× |
| `debug_text_character` | 6.50 ns | 14.10 ns | 2.17× |
| `get_wave_properties` | 5.80 ns | 11.40 ns | 1.97× |
| `void_generate_primitive` | 6.10 ns | 10.80 ns | 1.77× |
| `light_update_ambience` | 4.40 ns | 7.00 ns | 1.59× |
| `resolve_collisions` | 5.00 ns | 7.70 ns | 1.54× |
| **Sum over the 24 functions** | **140.70 ns** | **304.80 ns** | **2.17×** |

Median factor **2.16×**, range 1.54× to 3.00×.

The overhead in time (2.16×) exceeds the overhead in instruction count (1.42×). The
gap is expected: the added instructions are not free, and code 25 % larger weighs
more on the instruction cache — an effect that will be **more marked**, not less, on
a Pentium II whose caches are counted in tens of kilobytes.

## Normalisation towards the target machine

The bench was ported to Win32 (`tools/cpu-budget/bench_win32.c`) and run on the test
machine — Pentium II 400 MHz, 64 MB, Windows 95 OSR2.5 — with **the same generated
code, the same ELF, the same functions and the same inputs** as on the host. Only
the machine changes.

23 of the 48 candidate functions run without faulting on the target, against 24 on
the host: the RDRAM window there is 16 MiB instead of 512, because committing
512 MiB on a 64 MB machine would page — and a time measurement under paging is
worth nothing. The comparison below bears on the 23 common functions.

| Function | 32-bit host | Target | Factor |
|---|---:|---:|---:|
| `func_80072E28` | 4.50 ns | 97.6 ns | 21.7× |
| `search_level_properties_backwards` | 11.80 ns | 237.5 ns | 20.1× |
| `debug_text_character` | 14.10 ns | 267.8 ns | 19.0× |
| `func_8002F2AC` | 24.90 ns | 451.5 ns | 18.1× |
| `resolve_collisions` | 7.70 ns | 134.0 ns | 17.4× |
| `__sinf_recomp` | 11.00 ns | 187.7 ns | 17.1× |
| `func_8001E4C4` | 10.80 ns | 168.6 ns | 15.6× |
| **Sum over the 23 functions** | **245.0 ns** | **4,353.3 ns** | **17.8×** |

Median factor **17.7×**, range **15.6× to 21.7×**. The spread is narrow — three
times tighter than the one for the 64 → 32 bit move — which indicates a sound
measurement rather than an artefact.

**Putting it in perspective.** The host runs at ~3.5 GHz, the target at 400 MHz: a
frequency ratio of about 9. The measured factor being 17.7, the Pentium II is about
**twice as inefficient per cycle** as the modern core on this code. That is
plausible for recompiled code, made of long dependency chains that out-of-order
execution cannot overlap much, and it is reassuring that the figure falls within the
expected order of magnitude rather than at an extreme.

Combined with the 64 → 32 bit factor, the ratio between the development machine and
the target machine is about **38×**. That is the coefficient to apply to any
measurement made on the host in order to estimate it on the target.

### What this measurement does not say

**The target is an emulated Pentium II, not a real one.** 86Box models the
processor's instruction timings, and the measurement is taken in *emulated* time
(the 1,193,180 Hz counter as the guest sees it), not in the host's real time. It
therefore reflects what 86Box's model predicts of a Pentium II 400 — a reasonable
model, but one that reproduces neither the real caches, nor the branch prediction,
nor the period's memory bandwidth. Validation on real hardware
([E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md)) remains
indispensable, and it is what will say by how much this model is wrong.

## Limits of these figures

To be stated before any conclusion:

1. **These are leaf functions, called against a static memory image.** They
   presumably take early-exit paths for want of a reconstructed game state. The
   instruction mix is real, the execution depth is not.
2. **The measurement is made on a Ryzen 9 3950X, not on a Pentium II.** It
   establishes the cost of the 64 → 32 bit move without SSE, which is exactly what
   the ticket asked to measure — but not the ratio to the target machine.
3. **24 functions out of 48**, chosen because they do not fault. The subset is
   identical in both configurations, which the comparison requires, but it is not a
   representative sample of a game frame.

## What is missing to conclude

Both factors are now measured. What is missing is no longer a normalisation but a
**denominator**: how much CPU work DKR really demands per frame.

The bench measures leaf functions called in isolation. It gives the *relative* cost
from one machine to another, which was the ticket's object, but not the *absolute*
cost of a game frame. To pronounce the go/no-go one needs to:

1. **Run the game under Windows 95**
   ([E02-S06](../stories/E02-system/E02-S06-game-bring-up.md)), with the diagnostic
   renderer, and measure the CPU time per frame. That is the figure that is
   missing, and it alone compares against the 33.3 ms.
2. **Put a number on the audio microcode**
   ([E00-S04](../stories/E00-scoping/E00-S04-spike-rsp-cost-without-sse.md)). It is
   the budget's most worrying item: the console entrusted it to a dedicated 62.5 MHz
   vector DSP, and here it falls on the same processor. Generating its code is
   possible — `RSPRecomp` reads the ROM directly.
3. **Confront real hardware**
   ([E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md)), in order to
   measure the gap between 86Box's model and a real Pentium II.

In the meantime, the **38×** factor allows any measurement made on the development
machine to be transposed onto the target, which is usable straight away by E08-S01.

## What can already be said

No measured element condemns the approach, and two support it:

- the only compilation blockage was in a header, it is lifted, and the game did not
  touch the offending code;
- 3.89 x86 instructions per MIPS instruction remains an ordinary expansion factor
  for static recompilation.

The item that remains entirely unmeasured, and which is the most worrying, is not
the game's CPU: it is the `aspMain` audio microcode, run here on the host processor
whereas the console entrusted it to a dedicated 62.5 MHz vector DSP. It is the
subject of
[E00-S04](../stories/E00-scoping/E00-S04-spike-rsp-cost-without-sse.md), and
generating its code is now possible — `RSPRecomp` reads the ROM directly, without
going through the ELF.

## Reproducing

```bash
# 1. tools, without root
scripts/Setup-Win95-Toolchain.sh

# 2. reference ELF, in the neighbouring decomp
export PATH="$HOME/.local/dkr-win95/bin:$PATH"
cd ../Diddy-Kong-Racing
uv venv .venv --python 3.12 && uv pip install --python .venv/bin/python -r requirements.txt
.venv/bin/python ver/splat/update_baserom_names.py && make -C tools
make extract && make -j"$(nproc)"

# 3. recompiled sources
cd ../DKR-R-win95-voodoo
scripts/generate_recomp_toml.py \
  --elf ../Diddy-Kong-Racing/build/dkr.us.v77.elf \
  --rom ../Diddy-Kong-Racing/build/dkr.us.v77.z64 \
  --policy runtime-recomp/dkr.us.v77.recomp-policy.json \
  --output-funcs runtime-recomp/RecompiledFuncs \
  --output runtime-recomp/dkr.us.v77.generated.toml
extern/n64-modern-runtime/N64Recomp/build-linux/N64Recomp \
  runtime-recomp/dkr.us.v77.generated.toml

# 4. measurement
tools/cpu-budget/run.sh ../Diddy-Kong-Racing/build/dkr.us.v77.elf
```

The decomp does not create its Python environment with `python3 -m venv` on this
machine: that requires the `python3-venv` package, hence `apt`, hence root
privileges. `uv` creates the same environment without `ensurepip`.

`tools/cpu-budget/run.sh` redoes the whole measurement end to end: the equivalence
proof for the 128-bit multiplication, the selection of leaf functions, the double
compilation, the check for the absence of SSE, and three measurement campaigns.
