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

## The denominator, measured at last — 28 August 2026

This report has carried both *factors* of the budget since 11 August — 2.16× for
the 64 → 32 bit move, 17.7× for the normalisation to a Pentium II 400 — and said
in its own words that what remained was "no longer a factor, it is a
**denominator**": how long a frame of the real game actually takes. That was
blocked on E02-S06, which is passed.

Measured on the emulated Pentium II over **1,016 frames**, with E02-S03's time
base (`QueryPerformanceCounter`, confirmed in the run at 1,193,180 Hz — the 8254
PIT, 4.19 µs a tick):

```
[gfx]   frame: period=169907 us (5.88 fps) render=23052 us elsewhere=146855 us
[gfx]   frame-worst: period=648633 us render=479857 us samples=1016 dropped=3
```

| | |
|---|---:|
| A frame | **170 ms** |
| Frame rate | **5.88 fps** |
| — of which the renderer (decode, transform, clip, convert, submit, present) | 23 ms — **13.6 %** |
| — of which everything else (recompiled MIPS code, scheduler, audio) | 147 ms — **86.4 %** |
| The N64's budget at 30 fps | 33.3 ms |
| **Over budget by** | **5.1×** |

### The split is the finding, not the total

**Eighty-six per cent of a frame is not the renderer.** E04 and E05 are five
months of this project's work and they account for one seventh of the time. The
whole graphics stack could be made free and the game would run at 6.8 fps.

That reassigns the optimisation epics without ambiguity.
[E08-S02](../stories/E08-perf/E08-S02-recompiled-code-optimisation.md) — the
recompiled code — is the only lever with the leverage to matter;
[E08-S03](../stories/E08-perf/E08-S03-vertex-path-optimisation.md), the vertex
path, is inside the 13.6 %.

And the renderer's own 23 ms is not comfortable either: it is **69 % of the entire
30 fps budget** on its own. It is not the blocker, and it has no room to grow.

### Corroborated by a second instrument

The period is measured by the PIT, from inside the game thread. The VI thread
counts its own presents against a different timer, and over the same run it
reports **8.53 presents per display list** — at 60 Hz, one list every **142 ms**
against the 170 ms measured. Two clocks, two threads, no shared code, agreeing to
within 17 %. The residue is expected: presents do not bracket lists exactly.

That agreement is what makes the figure usable. A single clock reading 170 ms
would have been one instrument's word, and this project has spent days on
instruments that were the only witness to their own answer.

### Three reservations, and one is a gap

**86Box's model, not silicon.** The measurement is in *emulated* time and reflects
what 86Box predicts of a Pentium II 400. That reservation is already this report's
and is unchanged; E09-S04 remains the only way to close it.

**The card's own work costs nothing here.** `docs/TEST-ENVIRONMENT.md` records that
the Voodoo emulation is functional and not temporal. The 23 ms is therefore the
*CPU* cost of the rendering path, with the card's fill unmeasured. On real
hardware the fill happens in parallel, but the 23 ms cannot be assumed to be the
whole story.

**The 147 ms is not proved to be work.** This instrument measures the interval
between graphics tasks; it cannot tell a processor that is busy for 147 ms from
one that is blocked for 147 ms. The argument that it is work is circumstantial and
worth stating as such: 5.88 fps is not a rate anything paces to — a wait-bound
loop would land on 60, 30 or 20 — and the message queue was measured on 26 August
carrying about two messages with no refusals, which is not a starved scheduler.
Circumstantial is not measured. Closing it needs the game thread instrumented
where it blocks, and that is a ticket of its own.

> The go/no-go was blocked for seventeen days on a number that took one afternoon
> to obtain once E02-S06 existed. What made it cheap was that the clock, the
> report and the display-list counters were all already there — and what nearly
> made it wrong is that `dkr_clock_now` returns **zero** until `dkr_clock_init` is
> called, and nothing in the game called it. Every frame would have read as
> instantaneous. The witnesses called it, which is why E02-S03's measurements were
> right and this one would not have been.

### The 147 ms is work — 30 August 2026

The measurement above left one gap and named it: *"the 147 ms is not proved to be
work. This instrument measures the interval between graphics tasks; it cannot tell
a processor that is busy for 147 ms from one that is blocked for 147 ms."* The
whole reassignment of the optimisation epics rests on that, so it is measured.

The place to measure it is exact. Every guest thread is a real host thread parked
on a semaphore, and **exactly one runs at a time**: the scheduler signals one and
blocks the caller. The interval between a thread leaving `wait_for_resumed` and
re-entering it is that thread's running time; the sum over all of them is the time
the guest world spent executing. Patch 0028.

Over 176 seconds and 12,723 context switches:

```
[trace][cpu] guest-run=127518510 us wall=175950942 us switches=12723 busy=72%
```

**72.5 %**, and steady — six consecutive samples read 73, 73, 72, 72, 72, 72.

| per 173 ms frame | |
|---|---:|
| recompiled MIPS code executing | **125 ms** |
| the renderer, on its own host thread | 22 ms |
| neither | ~26 ms |

The recompiled code is genuinely working for **five sixths of a frame**. E08-S02
is confirmed as the lever, and E08-S03's vertex path is confirmed as living inside
the 13 % that is not.

**What the third row is, and is not.** The renderer runs on a thread ultramodern
creates, not on a guest `OSThread`, so it is outside this measurement and the two
can overlap — 86Box models one processor, so they interleave rather than run
together. The ~26 ms is therefore an upper bound on genuine idleness and not a
measurement of it; separating them needs the renderer thread instrumented the same
way, which is a ticket of its own and not on the critical path.

### The instrument took four runs, and each fault was a different one

Written down because the pattern is the report's subject as much as the number is.

1. **Silent because its clock returned zero.** `dkr_clock_now_us` answers 0 until
   `dkr_clock_init` runs, and the game initialised it *inside the renderer* —
   after the guest threads start. The guard `tl_resumed_at != 0` then never
   passed, so the accounting was switched off and said nothing about it. This is
   the same defect as 28 August's, one level along: the clock is now initialised
   in `DkrMain`, before a thread or a window exists, because a time base is not a
   renderer's property.

2. **Silent because it reported on a count.** Five thousand switches a report, and
   a run of three hundred display lists produced exactly one line — fewer than
   seventeen switches a list, which nobody had guessed. The report is now on the
   clock, five seconds, which gives the same number of samples whatever the guest
   does and costs nothing since the clock has already been read.

3. **Confounded by sharing a switch.** The first run put the trace behind
   `DKR_TRACE_SP`, which also turns on the scheduler trace and its several lines
   per display list to an emulated floppy. The game stalled at list 3, and with
   two variables changed at once the stall was attributable to neither. Its own
   `DKR_TRACE_CPU` separated them, and the patch turned out to be innocent.

4. **A function-local `static` in the hot path.** Its guard is
   `__cxa_guard_acquire`, built here on winpthreads, whose primitives E02-S01
   measured failing on Windows 95. A guard taken on every context switch from
   every guest thread is the last place to find that out. File scope, initialised
   before any thread exists.

> Three of the four made the instrument *quiet*, and quiet is the failure mode
> this report keeps meeting: an absence of output read as evidence about the thing
> being measured. The fix that generalises is the cheap one in fault 2 — a line at
> the first event, so that "switched off" and "nothing to say" stop looking alike.

## The go/no-go, pronounced — 17 September 2026

The verdict this report was opened to deliver. It waited for a denominator, got one
on 28 August, and then waited on a judgement that turns out not to be needed: the
arithmetic decides it without anyone having to define "playable" first.

### It is a no-go on the stated floor, and Amdahl says so rather than an estimate

The frame is 170 ms and **125 ms of it is recompiled MIPS code** — 73.5 %, measured
on 30 August by timing every guest thread between leaving and re-entering its
semaphore. The rest is 22 ms of renderer and ~26 ms that is neither.

That share is the whole answer, because it caps what optimisation can do:

    recompiled code reduced to zero   ->  45 ms per frame  =  22.2 fps

**Twenty-two frames per second is the ceiling of a perfect optimisation**, not a
plausible outcome of one. E08-S02 could succeed beyond anyone's hope and 30 fps
would still be out of reach on a Pentium II 400:

    target      whole-frame speedup   the recompiled part must fall by
    30 fps            5.10x           100 %  - impossible at recomp = 0
    25 fps            4.25x           100 %  - impossible at recomp = 0
    20 fps            3.40x            98.4 %
    15 fps            2.55x            85.1 %
    12 fps            2.04x            71.7 %

Twenty and fifteen are arithmetically reachable and practically not: a recompiler
does not give back 85 % of its output's cost. A realistic E08-S02 — better codegen
flags, the hot paths tidied — is worth something like 1.3× to 1.5× on that 125 ms,
which lands the frame at 130 ms, or **7.7 fps**.

**And the audio is not in any of these numbers.** E00-S04 measured the microcode
path at 3.9 % of the throughput it needs, and E03-S03, which replaces it, is
unwritten. Whatever it costs, it is added to a frame that is already 5.1× over.

### The minimum frequency, with its hypothesis written out

Scaling by clock alone, which this report elsewhere warns is optimistic for old
cores:

| hypothesis | needed | nearest real part |
|---|---|---|
| no optimisation, 30 fps | 2.04 GHz of Pentium II-equivalent throughput | none exists |
| E08-S02 delivers 1.5×, 30 fps | 1.57 GHz of Pentium II-equivalent | a 1.4 GHz Pentium III (Tualatin), whose better IPC puts it near 1.6-1.75 GHz PII-equivalent |

So the raised floor that reaches 30 fps is **a Pentium III around 1.4 GHz, and only
if E08-S02 delivers about 1.5×** — with the audio still unaccounted for. That is
three steps beyond ADR 0002's Pentium II / Voodoo 2, and it pairs a late Socket 370
part with a Voodoo, which is buildable but not the machine the project set out to
serve.

### What would flip this verdict

* **A measurement on silicon.** Everything above is 86Box's timing model. E09-S04
  is the only instrument that closes it, and emulator timing models are not
  reliable to a factor of five in either direction. This is the single largest
  uncertainty and it is not small.
* **A different reading of "playable".** At 12 fps the requirement is 2.04× and the
  recompiled part must fall 72 % - still out of reach, but it is the first row that
  an unusually good optimisation plus a modest clock bump could meet.
* **A change of approach.** The fallback is documented and unchanged: the
  neighbouring decomp's native port compiles the original C and carries none of
  this translation overhead, at the cost of far heavier work everywhere else.

### What this does not condemn

Nothing built so far. The renderer is 13.6 % of the frame and is not the problem;
the Glide backend, the F3DDKR decoder, the threading and clock layers, the save
codec and the test harness are all independent of which CPU the game ends up on,
and all of them transfer unchanged to a raised floor or to the native port.

The risk section of E00-S03 said this outcome was acceptable and was the ticket's
reason for being. It cost six weeks rather than the three months it would have cost
after E04 and E05, which is what the ordering was for.

## How wrong is the model? Measured — 18 September 2026

Every figure above is 86Box's prediction, and this report has said so since August
without ever putting a number on the error. `CPUMODEL.EXE`
(`tools/win95/witnesses/cpu_model_probe.c`) puts one there.

It cites no published benchmark on purpose. Dhrystone's Pentium II figures vary by
compiler, version and who ran them, and calibrating against a number whose
provenance cannot be checked would reintroduce the same uncertainty by another
door. The four kernels are chosen so that their cost on silicon follows from the
**documented architecture** — a three-wide core, 16 KB of L1, 512 KB of L2 at half
clock, a 100 MHz memory bus.

    clock 1193180 Hz, modelling 400 MHz
    dep_add       1.13 cycles/op   (real P2: 1.00, the latency of add)
    ind_add       0.75 cycles/op   (real P2: near 0.33, three-wide)
    l1_chase      2.12 cycles/step (real P2: about 3, L1 load-use)
    mem_chase    11.04 cycles/step (real P2: tens, on a 100 MHz bus)
    RATIO mem/L1 = 5.2

**The kernels carry loop overhead that must be subtracted before any of this is
read as a bias**, and doing so changes two of the four readings:

| kernel | measured | expected *with* the loop | reading |
|---|---:|---:|---|
| `dep_add` | 1.13 | 1.25 | the cycle model is sound |
| `ind_add` | 0.75 | 0.59 | mildly pessimistic, not the 2.3× it first looks |
| `l1_chase` | 2.12 | ~3 | mildly optimistic |
| `mem_chase` | 11.04 | 60–80 | **optimistic by roughly six** |

### What this says, and it is the answer to "is it the Pentium II or the emulator?"

**It is the Pentium II.** The instruction-timing model is accurate where it can be
checked against a documented latency: a dependent `add` chain comes out at 1.13
cycles against 1.25 predicted with the loop, which is as close as this method can
resolve. Superscalar issue is if anything under-credited, which makes the emulated
CPU look *slower* than silicon on parallel work.

The exception is memory, and it is a large one. Eleven cycles for a random access
across eight megabytes is 27.6 ns; a Pentium II 400 on a 100 MHz bus takes 150 to
200 ns for the same thing. The model is optimistic there by about a factor of six,
and the `mem/L1` ratio of 5.2 against tens on silicon says the hierarchy is barely
represented.

**Which way that moves the verdict.** The recompiled code chases an eight-megabyte
RDRAM image and is the most memory-bound thing in the frame; the renderer works on
warmer, smaller buffers. So the 73.5 % term is precisely the one the model
flatters. Real silicon would be **worse than 170 ms**, not better, and E00-S03's
no-go is conservative rather than an artefact of emulation.

### Completed, 18 September 2026: two tiers of the model are simply absent

The first run measured instruction timing and main memory. Adding the middle cache
tier and a branch kernel finishes the picture, and both new readings are flat zeros
rather than approximations:

    dep_add       1.13 cycles/op   (real P2: 1.00, the latency of add)
    ind_add       0.75 cycles/op   (real P2: near 0.33, three-wide)
    l1_chase      2.16 cycles/step (real P2: about 3, L1 load-use)
    l2_chase      2.03 cycles/step (real P2: about 8-12, L2 at half clock)
    mem_chase    11.05 cycles/step (real P2: tens, on a 100 MHz bus)
    branch predictable       5.52 cycles/iter
    branch unpredictable     5.18 cycles/iter
    MISPREDICT COST = -0.35 cycles  (real P2: about 10-15)

**The second-level cache does not exist in the model.** A 256 KB working set — well
inside the real 512 KB L2 and sixteen times the L1 — costs 2.03 cycles a step
against L1's 2.16. The same, within noise. On silicon that tier costs three to four
times L1, because it runs at half the core clock.

**A mispredicted branch is free.** The unpredictable pattern comes out 0.35 cycles
*faster* than the alternating one, which is noise around zero and has to be read as
"no charge at all". On silicon the same experiment separates by ten to fifteen
cycles.

### So the model flatters exactly the code this port is made of

Three independent axes, all leaning the same way:

| effect | model | silicon | who it flatters |
|---|---|---|---|
| instruction latency | 1.13 | 1.25 expected with the loop | nobody — it is right |
| superscalar issue | 0.75 | 0.59 expected | mildly pessimistic |
| L2 | absent | 8–12 cycles | **anything with a working set over 16 KB** |
| main memory | 11 cycles | 60–80 | **anything that walks megabytes** |
| branch mispredict | free | 10–15 cycles | **anything branch-dense** |

Translated MIPS is branch-dense by construction — every `beq` in the original
becomes a test and a jump — and it chases an eight-megabyte RDRAM image. It is the
workload those three rows describe. The renderer, working on warmer and smaller
buffers with longer straight runs, is much less affected.

So the 73.5 % term is flattered on three counts and the 13.6 % term on none of them
much. The answer to *"is it the Pentium II or the fact that we emulate one?"* is
**the Pentium II**, and more firmly than the first run could say: the emulator is
not making the machine look bad, it is making it look considerably better than it
would be.

### The workload's shape, measured statically — 18 September 2026

The calibration establishes a direction. Turning it into a magnitude needs the
recompiled code's own mix, and hardware counters are not available here: `perf`
refuses without elevated privileges (`perf_event_paranoid` is 4), and changing a
system-wide kernel setting to take a measurement is not this project's to do.

What is available is the 32-bit objects the bench already builds. Counted across
all 37 of them:

    instructions             933,015
    conditional branches      30,546   3.3 %, one every 30 instructions
    calls                     17,624   1.9 %
    memory-referencing       575,765   **61.7 %**

**Nearly two instructions in three touch memory**, which follows from the
translation itself: x86-32 has eight registers for the VR4300's thirty-two, so the
guest context lives in memory and almost every operation reads or writes it.

### What this does and does not settle

It does **not** license multiplying 61.7 % by the six-fold memory optimism. Most of
those accesses are to the register context — a few hundred bytes, L1-resident on
silicon as in the model, and correctly charged by both. The accesses that the model
flatters are the ones reaching the eight-megabyte RDRAM image, and this static count
does not separate the two.

What it does settle is that the recompiled code is **memory-shaped rather than
compute-shaped**, and that is the category the model's two absent tiers punish. Any
RDRAM access at all is an L2 miss on silicon, because eight megabytes cannot sit in
512 KB, and the model charges that at a sixth. The direction of E00-S03's error is
confirmed by the workload's shape and not only by the calibration.

And it is static. Loops execute their bodies many times, so a dynamic mix would
weight hot regions differently — most likely upward for branches, since loop
back-edges are branches. The figure to trust here is the *shape*, not the decimals.

### Why raising the emulated clock cannot answer the raised-floor question

The obvious next experiment is to reconfigure 86Box as a faster machine and measure
the frame again — the verdict names "a ~1.4 GHz Pentium III *and* a 1.5× optimisation"
and that 1.5× is an extrapolation. It was considered and **deliberately not run**.

The calibration above is what rules it out. A model that charges main memory at a
sixth and has no second-level cache at all has nothing to hold a workload back as
the clock rises: in that model almost everything scales linearly with frequency. So
the experiment would return a clean linear speed-up and it would be an artefact of
the emulator, not a property of the code.

And linear scaling is exactly the assumption in doubt. The recompiled code
references memory in 61.7 % of its instructions and walks an eight-megabyte image;
on silicon that is the archetype of a workload that **stops** scaling with clock,
because the memory behind it does not get faster when the core does. A 1.4 GHz part
on a 133 MHz bus has three and a half times the core clock of a 400 MHz part on a
100 MHz bus and only a third more memory bandwidth.

So the one number the raised-floor decision needs — does this frame scale with
clock, or is it memory-bound? — is precisely the number this emulator cannot
produce. Running it anyway would yield a confident figure pointing the wrong way,
which is worse than having none.

**What would answer it**: the same frame on two real machines of different clock and
the same memory subsystem, which is [E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md)
with a second data point. Failing hardware, a cache-and-bandwidth-aware simulator
would do, and 86Box is measured above not to be one.

### What the audio microcode costs, and where the rest of the time goes

Raised by the user, who objected that a VR4300 at 93.75 MHz should not leave a
Pentium II 400 five times short, and that something unexplained sat in the middle.
They were right that something did. Two measurements answer it.

**The audio microcode runs, and it is not the explanation.** `GetRspMicrocode`
dispatches `dkrAspMain` for every audio task, so E00-S03's claim that "the audio is
in none of these numbers" was false — its own table lists audio in the 147 ms.
Instrumented:

    first RSP task: type=2 data_size=7968
    calls=1    total=101750 us   mean=101750 us
    calls=100  total=1644033 us  mean=16440 us

Over 300 display lists that is **5.5 ms per frame, 3.2 %** of 170 ms. The mean per
call, 16.4 ms, is large for work the console gave to a dedicated 62.5 MHz vector
DSP running in parallel — and its share here is *understated*, because at five
frames a second the audio tasks arrive about once in three frames. At thirty they
would arrive at real-time rate, and 16.4 ms against a 33 ms budget would be half the
frame. That strengthens E03-S03's place on the critical path rather than weakening
it.

**The rest is dependency-bound execution, not a mystery.** Working back from the
measured frame:

    P2 cycles per frame on recompiled code            50.0 M
    guest instructions per frame, VR4300 100 % busy    3.12 M
    implied                                           16.0 cycles per MIPS instruction
    at 3.89 x86 instructions per MIPS instruction      4.1 cycles per x86 instruction

Four cycles per emitted instruction on a three-wide out-of-order core is the
signature of code that cannot issue in parallel. The objection's arithmetic assumed
roughly 1.3 — near the issue limit — and that assumption is the whole gap. Each
guest instruction feeds the next, and on 32-bit every 64-bit guest register carries
its add-with-carry chains behind it.

One assumption to name: 3.12 M guest instructions per frame takes the VR4300 as
fully busy at 30 fps. If DKR used less of it, the implied cost per instruction is
**higher**, not lower. The figure is a floor.

### DKR pays for 64-bit registers it does not use — 19 September 2026

The diagnosis above says the recompiled code is dependency-bound, and names the
64-bit guest register carried on a 32-bit host as part of what lengthens the chain.
That suggested a question nobody had asked: **does DKR use 64 bits at all?**

Counted across the whole generated set, 40 files:

    32-bit-width operations      116,795
    64-bit-width operations           20   (18 `SD`, 2 `DDIV`)
    share genuinely needing 64 bits   0.017 %

`MEM_D`, `DADDU`, `DADDIU`, `DSLL`, `DSRL`, `DSRA`, `DSUBU`, `DMULT`: **zero
occurrences each**. The earlier finding that `DMULT`/`DMULTU` are never called was
the tip of it — the game is, in practice, pure 32-bit MIPS.

Yet `recomp.h` declares `typedef uint64_t gpr` unconditionally, so every one of
those 116,795 operations carries a register pair on the target. The measured price
is in this report's own summary: **3.89 x86 instructions per MIPS instruction in
32-bit against 2.74 in 64-bit**, a 42 % increase in count — applied to code already
running at 4.1 cycles per emitted instruction because it cannot issue in parallel.
The carry chains do not merely add instructions; they lengthen the dependency chain
that is the bottleneck, so the cost is plausibly worse than the count suggests.

**This is a lead, not a result, and the risk has a name.** MIPS III sign-extends a
32-bit result into the full 64-bit register, and code that compares registers or
forms addresses can depend on the upper half. Narrowing `gpr` is therefore not a
width change but a semantic one, and nothing here has verified it is safe. The
twenty wide operations would also need a path of their own, which is easy; the sign
extension is not.

**Built, and it prices the lever statically.** A third set of objects was compiled
from the same sources with the same flags, differing only in `typedef uint32_t gpr`
shadowed ahead of the real header:

    .text, 32-bit wide register      4,034,404 bytes
    .text, 32-bit narrowed register  2,637,831 bytes
    reduction                            34.6 %

**A third of the emitted code is there to carry a width the game uses twenty times
in 116,795 operations.** It corroborates this report's own earlier figure from the
other direction: 2.74 against 3.89 x86 instructions per MIPS instruction is 29.6 %
fewer, measured on instruction counts where this is measured on bytes.

**And the run-time gain cannot be had this cheaply, which corrects what this section
first said.** It claimed the third variant would "put a number on the gain in one
run, before anyone touches semantics for real". That is wrong, and building it is
what showed why: the narrowed objects need 214 stubs where the wide ones need
fewer, and they reference game symbols the wide build optimises away. Different code
survives compilation, because the semantics differ — sign extension is not handled.
A timing comparison would be timing two different computations and reporting the
difference as a speed-up.

So the static figure stands on its own and the dynamic one waits on correctness.
The order is the opposite of what was written here: **sign extension first, then
measurement**, not measurement first as a cheap preview.

### The sign-extension risk, scoped

"Sign extension is not handled" names a danger without sizing it. Worked through,
it is narrower than it sounds.

**Address formation is what breaks, and only it.** `MEM_W(offset, reg)` computes
`rdram + ((reg + offset) - 0xFFFFFFFF80000000)`, which relies on the register
holding a sign-extended KSEG0 address:

    MEM_W(0, 0x80100000)
      64-bit register, sign-extended : rdram + 0x100000       correct
      32-bit register                : rdram + 0x100100000    four gigabytes out

A narrowed build would fault on its first load, or worse, not fault. The fix has an
obvious shape — **mask instead of subtract**, `rdram + (addr & 0x7FFFFFFF)` gives
back `0x100000` — and it lives in a handful of macro definitions rather than being
scattered through 116,795 operations.

**Comparisons survive truncation**, which is the other classic worry and the one
that would have been scattered. Values that are all sign-extended the same way
compare identically at either width, signed or unsigned. Checked on the two cases
that would fail if anything did:

    0xFFFFFFFF vs 0x00000001 : unsigned, 64-bit >, 32-bit >
    0x80000000 vs 0x7FFFFFFF : unsigned, 64-bit >, 32-bit >

**What this does not clear.** Two failure modes were named and checked; others may
exist and have not been. The eighteen `SD` sites write eight bytes from a register
and need a path of their own. The `& 0x7FFFFFFF` mask assumes every guest address
sits in the low two gigabytes, which holds for KSEG0 and KSEG1 on this machine and
is an assumption all the same. And none of this is a build that runs.

What has changed is the shape of the work. It was "a semantic change nobody has
verified"; it is now "one family of macros wants a masked form, comparisons are
provably unaffected, and eighteen stores want a wide path" — which is a day's work
to try rather than a research question.

### What this does not license

It does not turn the six into a corrected frame time. How much of the 125 ms is
memory-bound is not measured, so the correction cannot be applied — only its
direction is established. And the "150 to 200 ns" is a documented range for the
class of machine, not a measurement of one; E09-S04 remains the way to replace it
with a number.

What has changed is that the reservation is no longer open-ended. It was "the model
is wrong by an unknown amount in an unknown direction". It is now "the model is
right on instruction timing, optimistic by about six on main memory, and therefore
wrong in the direction that makes the port look better than it is".
