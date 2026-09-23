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

~~What has changed is the shape of the work. It was "a semantic change nobody has
verified"; it is now "one family of macros wants a masked form, comparisons are
provably unaffected, and eighteen stores want a wide path" — which is a day's work
to try rather than a research question.~~

**Corrected below, 20 September 2026.** "Comparisons are provably unaffected" is
wrong. The arithmetic above is right about values that are already sign-extended,
but the emitted code does not compare registers directly — it compares them through
`SIGNED(val)`, which is `((int64_t)(val))`. Widening an *unsigned* 32-bit register
to `int64_t` zero-extends, so `-1` becomes four billion and every signed comparison
in the game reads the wrong way. The next section measures it happening. The
remedy is one line rather than a scattered audit, so the estimate of the work
survives; the claim of safety did not.

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

### Narrowing the guest register, done properly — 20 September 2026

The previous section left the narrowing as an estimate resting on two checks. Both
checks have now been run as code rather than as arithmetic, and one of them fails.
`scripts/Measure-Narrow-Gpr.sh` reproduces everything below in one command.

#### Which code actually wants sixty-four bits

The earlier count — "20 wide operations in 116,795" — was right in magnitude and
wrong in shape: it counted `ld`/`sd` and missed the doubleword shifts. Counted from
the opcodes the recompiler prints in its own comments, DKR's entire 64-bit usage is:

    dsll     5        atan2s, rand_range
    dsll32   2        rand_range
    dsrl     2        rand_range
    dsrl32   2        rand_range
    ddivu    2        atan2s
    ld      12        10 are `ld $ra, 0($sp)`; 2 are dmacopy_doubleword
    sd      13        10 are `sd $ra, 0($sp)`; 3 are dmacopy_doubleword

There are also 30 `ld` and 5 `sd` that name a float register. Those are `ldc1` and
`sdc1` — double-precision traffic through `ctx->fN.u64`, a union field whose width
does not follow `gpr`. They are untouched by any of this.

So the whole game uses a general register as sixty-four bits of data in **three
functions**:

- `dmacopy_doubleword`, a sixteen-bytes-per-iteration block copy;
- `atan2s`, which does `dsll` by 11 then `ddivu` — a fixed-point divide that makes
  room for its fractional bits above bit 31;
- `rand_range`, which does the same with `dsll32`/`dsrl32`.

The twenty `$ra` saves and restores are not data. They are a prologue storing a
sign-extended return address into an eight-byte stack slot and an epilogue reading
it back; the high word is the sign extension and nothing else reads it.

#### The compiler finds one family by itself, and only one

Built narrow with `-Wshift-count-overflow`, gcc reports exactly four warnings, all
in `rand_range` — the `dsll32`/`dsrl32` sites, where the shift count is 32 or more
and the type is now 32 bits wide. That is a free mechanical detector, and it covers
four of the thirteen. The rest — `dsll` by 11, `ddivu`, the block copy — are
perfectly legal 32-bit expressions that compute the wrong number in silence. The
census above is what finds those; no warning will.

#### Three header changes, and the one that was missed

Address formation, as predicted, needs rewriting — but not masked. The upstream
expression's low word is *always* `(uint32_t)addr - 0x80000000`, and on a 32-bit
target only the low word reaches the pointer, so the narrowed form

    #define DKR_GUEST_OFF(addr) ((uint32_t)(addr) - 0x80000000u)

is bit-identical rather than equivalent-for-the-addresses-we-use. The mask proposed
in the previous section would have diverged for any address below 0x80000000.

`SD` needs to widen its argument by that argument's own size, because it is reached
both from a narrowed general register, which must sign-extend, and from a float
register's `u64`, which must pass through:

    #define DKR_WIDEN(v) (sizeof(v) > 4 ? (uint64_t)(v) : (uint64_t)(int64_t)(int32_t)(v))

And `SIGNED` — the one the previous section pronounced safe — has to say what it is
widening from:

    #define SIGNED(val) ((int64_t)(int32_t)(val))

It appears 9,376 times in the emitted code. Without the inner cast, a narrowed
register zero-extends and `SIGNED(0xFFFFFFFF) < SIGNED(1)` is false. The test below
caught it on its first run.

`S64` and `U64` turn out to need nothing: the recompiler always writes them as
`U64(U32(ctx->rN))`, narrowing before it widens. The two `DDIVU` sites are the
exception, and they are inside `atan2s`.

#### The test, and what it is allowed to disagree about

`tools/cpu-budget/narrow_gpr_test.c` compiles against either header and prints a
transcript: address formation at positive and negative offsets, the misaligned
`lwl`/`lwr`/`swl`/`swr` helpers, signed and unsigned comparison across the sign
boundary, the `sd $ra`/`ld $ra` round trip, and a block copy.

Two of its lines are deliberately excluded from the transcript hash. The block copy
appears twice — once written the way the recompiler emits it, with the temporaries
in general registers, and once with the wide locals a width-aware recompiler would
emit. The first is *expected* to diverge, and does: the narrow build copies 64 bytes
and gets 32 of them wrong, because every `ld` truncates. The second agrees exactly.
A test where every case passed would not have shown that the first case is real.

Everything else agrees:

    transcript 28e3206d1c158c51    at both widths

Two further lines differ in print and not in value, so they are recorded as 32-bit
quantities: `do_lwl`'s result and the restored `$ra` are the same MIPS value at
either width, shown with or without its sign extension.

#### What it costs the machine

Both trees built with the same compiler and the same flags, `gcc -m32 -O2`:

    .text                3,644,771  ->  2,465,574     -32.4%
    x86 instructions       927,976  ->    645,348     -30.5%
    memory-referencing     512,556  ->    351,083     -31.5%

The mix says plainly what the width was buying:

    sar     41,694  ->   1,171    -97.2%    sign-extending by shifting right 31
    sbb      3,990  ->      81    -98.0%    carrying between halves
    cltd     8,193  ->     371    -95.5%    sign-extending eax into edx:eax
    or       9,372  ->   1,503    -84.0%    recombining halves
    movl    63,401  ->  32,775    -48.3%    storing upper-half immediates
    mov    408,112  -> 241,180    -40.9%
    xor     30,511  ->  19,291    -36.8%    zeroing upper halves
    lea     51,538  ->  47,034     -8.7%

`sar`, `sbb` and `cltd` essentially vanish — between 95 and 98 percent of each. They
are the pure cost of emulating a 64-bit register on a 32-bit machine, and DKR was
paying it on every instruction to use it thirteen times.

The memory figure is the one that matters most here. 161,473 fewer instructions
reference memory, and this is a machine measured earlier in this note to have no L2
at all under 86Box and a three-cycle load-use on the real part, executing a workload
that is 61.7% memory-referencing and dependency-bound at 4.1 cycles per emitted
instruction. A third fewer memory operations is the third most likely to be on the
critical path.

#### What this still does not give

A frame time. None of this has run as a game: the three wide functions need their
wide path written, and the narrowed build cannot link against a runtime whose
`recomp_context` is shared with the 64-bit modern target. The 32.4% is code size and
the 30.5% is instruction count; neither is a speedup, and the relationship between
them and wall time is exactly the thing this note has spent two days establishing is
not one-to-one.

What it gives is a decision that no longer needs research. The work is: three
functions hand-written with wide locals, three macros changed, a shadowed header for
the 32-bit target, and a `recomp_context` whose register file is narrow. That is
scoped, measured, and tested. Whether it is worth doing depends on the direction
chosen for the hardware floor, which remains the open question this note keeps
handing back.

### The three wide paths, written and checked — 21 September 2026

The three functions now have the implementation a width-aware recompiler would
emit, in `tools/cpu-budget/wide_register_paths.h`. Each appears twice — as the
recompiler emits it, and with wide locals — and the test runs both. At the
upstream width the two agree; at the narrowed width only the second one is still
DKR. The transcript matches across widths, so the hand-written versions reproduce
the 64-bit semantics on a 32-bit register file.

What they do when they break is worth writing down, because the three fail very
differently.

**`dmacopy_doubleword` fails on every call.** Each `ld` truncates, so half the
payload becomes the sign extension of the other half. Sixty-four bytes copied,
thirty-two of them wrong, on the path of every DMA the game does.

**`rand_range` fails on every call, and fails to a constant.** The `dsll32` and
`dsrl32` shift by 32 or more, which is undefined on a 32-bit type; gcc folds them
to zero. Every seed tested — `00000001`, `12345678`, `80000000`, `ffffffff`,
`7a3b91c4` — returns the same thing:

    00000001 as emitted   0000000000000000
    12345678 as emitted   0000000000000000
    ffffffff as emitted   0000000000000000

A random-number generator that returns zero. Loud, at least.

**`atan2s` fails only on large inputs, which is the dangerous one.** The shift is
by 11, so the emitted version keeps the right answer while the numerator fits in
twenty-one bits and loses it one step later. The boundary is exact:

    2097151/3 as emitted index   0x2aa      correct
    2097152/3 as emitted index   0x000      wrong

`atan2s` takes coordinate differences. Most of them are small, so a narrowed build
would compute correct angles almost everywhere and wrong ones at the far end of a
track — the failure that survives a play-test and shows up as something else
entirely. It is also the one of the three that compiles without a warning.

That is the argument for the census rather than for the compiler. gcc finds four
of the thirteen sites, all of them in the function that fails loudly. The two that
fail quietly, and the one that fails everywhere, are legal 32-bit expressions.

Folding these back in would go through the `stubs` list in the recomp policy,
which already exists for replacing a generated function with a hand-written one.
That is plumbing rather than semantics; the semantics are done and checked.

### The narrowing, run on the machine — 21 September 2026

It builds, it boots, and it changes nothing.

#### It runs

`DKR_WIN95_NARROW_GUEST_REGISTER=ON` produces an 8,059,248-byte `DKRR.EXE` against
the upstream build's 9,227,069 — 12.7% of the whole executable, which is the 32.4%
of the recompiled part diluted by everything else in the binary. Both pass the
Pentium II instruction-set check and the import audit.

On the test machine it plays the N64 logo, the Rare copyright, and the title
screen's animated scene, and exits cleanly when asked. A change to the width of
every general register across 116,795 operations, and the game does not notice.

That is worth stating plainly, because it was the open question: the three
hand-written wide paths and the three macro changes are sufficient. Nothing else
in DKR wanted sixty-four bits.

#### It changes nothing

`DKR_TRACE_CPU` stamps every context switch and reports every five seconds, reading
the guest's own 8254 for both numbers, so the ratio is in emulated time and does not
move with the host. Both builds were run through the same sequence — boot, launch,
the intro's attract loop — and the trace compared at the same wall offset, because
the ratio falls through a run as loading gives way to the title screen and two runs
compared at their last sample would not be comparing the same work.

At 60 seconds of guest wall time, with the Glide renderer:

    build     guest-run        wall        switches   busy    switches/s
    wide      43,441,571 us    60,337,034      4,215   72.0%       69.86
    narrow    43,174,877 us    60,495,651      4,235   71.4%       70.01

Guest execution time: **−0.6%**. Switch rate: **+0.2%**. Against a static change of
**−30.5% of emitted instructions** and −31.5% of the instructions that reference
memory.

The binary was checked rather than assumed: the executable on the test disk after
the second run has the same MD5 as `build/win95-narrow/bin/DKRR.EXE`, and a
different one from the wide build. Two measurements this close are also what "the
wrong binary ran" looks like, and that had to be excluded rather than argued away.

#### Why nothing moved

Three measurements already in this note predict it between them.

**The core was idle.** At 16 cycles per MIPS instruction and 3.89 emitted x86
instructions per MIPS instruction, the recompiled code retires one instruction
every 4.1 cycles on a three-wide core — one instruction for every twelve issue
slots, about 8% of what the machine can retire. Removing 30% of the instructions
removes work from a resource that was 92% idle. The prediction is that the cost per
instruction rises to absorb it, and it did: the same time over 0.695 of the
instructions is 5.9 cycles each.

**The removed traffic never missed.** `recomp_context` is 540 bytes wide and 412
narrow; both live in a 16 KB L1 permanently. The upper half of a register shares its
cache line with the lower half, so the accesses that went away were L1 hits that
cost an instruction and no miss. Classified by addressing mode across the two
disassemblies:

    simple displacement (the context)   184,700 -> 149,110
    indexed base (the guest's RDRAM)     57,048 ->  63,301

The guest's own memory traffic is untouched — the same loads from the same eight
megabytes. The count rises only because a 32-bit address folds into the addressing
mode where a 64-bit one needed a separate `lea`, which is also why `lea` fell by
4,509.

**And the emulator does not hide it.** 18 September's calibration found main memory
at 11 cycles a step where silicon charges tens, a second-level cache that does not
exist because everything up to 256 KB is already L1-fast, and mispredicts free. All
three make the model *less* memory-stalled than the real part, which means the model
is the environment where an instruction-count saving should show up best. It did
not.

So the register width was not the bottleneck, and the reason it was not is that the
bottleneck is the guest's own memory behaviour — eight megabytes of game data,
accessed the same way whatever the host register is made of.

#### The renderer was not hiding it

The obvious objection to the table above is that `guest-run` counts a guest thread's
whole slice, graphics included, so a renderer that dominates would dilute any change
in the recompiled code. `DKR_RENDERER=null` answers it: the diagnostic renderer
counts display lists instead of drawing them, and guest execution rises from 72% of
wall to 82%. If the renderer had been the dilution, the difference would open up
here.

At 60 seconds of guest wall time, with the renderer taken out:

    build     guest-run        wall        switches   busy
    wide      49,682,886 us    60,542,978      4,614   82.1%
    narrow    49,594,001 us    60,413,214      4,580   82.1%

**−0.18%.** Smaller than with the renderer in, not larger. Whatever the recompiled
code is waiting for, it is not the graphics pipeline and it is not the instruction
count.

#### What this costs the argument for the fifth lever

E08-S02 listed the guest register's width as the largest of five levers, on a
measurement of emitted instructions. That measurement was right and the inference
from it was wrong: on this machine instruction count is not the currency. The lever
is now measured at nothing, and the same objection applies in advance to any other
lever whose claim is "fewer instructions" — including the first of the four the
ticket already had.

What survives is narrower and more specific: the levers worth measuring are the ones
that change *what the guest touches and when*, not how many instructions it takes to
touch it.

#### What this does not settle

It is one scene. The title screen spends 72% of its time in guest execution, which
is enough for the recompiled code to be worth optimising, but it is not a race:
nothing here has been driven into gameplay, and the mix of game logic against
graphics there is not measured.

It is one machine, and an emulated one. The three deviations named above all point
the same way, so the direction of the extrapolation is sound, but E09-S04 remains
the way to replace "a model where this shows nothing" with "silicon where this shows
nothing".

And the narrow build has run for three minutes of attract loop, not a session. The
equivalence tests cover the arithmetic across 386 inputs; they do not cover a race,
a save, or the live recompiler, which computes its register offsets from
`sizeof(recomp_context::r0)` and so follows the width for addressing but has not
been audited for the widths of the moves it emits. Nothing in this target reaches
it — it exists for mods — but the option is off by default and should stay off until
someone plays the game on it.

### The split is not a constant: it is 72/28 at the title and 50/50 in a world

The measurement above is at the title screen, and the note said so as a limitation.
Driving the route — `pad-until-screen` to PLAYER SELECT, to CAUTION, then on into
the adventure hub — and reading the trace interval by interval says how much that
limitation mattered.

    phase                                   busy      switches/s
    attract sequence and early menus     72 - 91 %     55 - 79
    the character and caution screens    51 - 58 %     62 - 64
    menus again                          70 - 75 %     69 - 78
    the adventure hub, last 105 seconds  47 - 54 %     58 - 67

The last block is the one that matters and it is the one I can attribute with
confidence: the hub was on screen when the route arrived, it was still on screen
when the run ended, and nothing moved it in between. Timber's Island with its
waterfall, water, vegetation and a butterfly is the richest scene this port has
drawn, and there the recompiled code is **half** the wall time, not three quarters.

The frame rate falls with it — 74 context switches a second at the title screen,
63 in the hub — so the extra time is not idle.

**Where it goes, from the renderer's own clock.** `glide_renderer.cpp` has timed
itself all along and reports `[gfx] frame: period=… render=… elsewhere=…`. Read
alongside the trace above it corrects one thing and adds another.

The correction first: `busy` is the share of wall time that guest *threads were
scheduled*, which is not the same quantity as the share of a frame spent in
recompiled code, and the section above slid between the two. A guest thread blocked
on the renderer is wall time that `busy` does not count and the frame still pays
for.

    scene            period      render            elsewhere
    title screen    196 ms      47 ms (24%)       149 ms (76%)
    the route       213 ms      58 ms (27%)       154 ms (73%)

Both columns are **cumulative means** — `render_us_total_ / render_n_` — so a
per-scene value cannot be read off them directly. What can be read is the
direction: through the hub the render mean climbed from 16% to 27%, and a mean only
climbs when the arriving values are above it, so the hub's own frames cost more than
27% in the renderer. That is what the falling `busy` is: the game thread waiting on
a renderer that has more to do.

And the addition, which is the more useful half: **`elsewhere` is flat.** 147, 148,
149, then 151, 152, 153, 154 — through menus, through loading, through the richest
scene the port draws, everything that is not the renderer costs about **150 ms a
frame and does not move.** The scene changes the renderer's share; it does not
change the game's own cost.

That is the same 125–153 ms this note has been quoting for the recompiled code since
August, arrived at from a different instrument, and it is the reason the frame is
190 ms rather than 33: not that any scene is expensive, but that the floor is.

**Which cuts the same way as everything else in this section.** The richer the
scene, the larger the renderer's share and the smaller the recompiled code's, so the
less any change to the latter can buy. But the sharper point is the flat 150 ms: the
game's own cost is the floor under every frame in every scene, it is five times the
whole budget on its own, and narrowing a third of its instructions moved it by
nothing.

Two cautions. The earlier phases are not attributed — the route was walking through
menus and loading, and which interval is which screen is a guess, so only the final
block is quoted as a finding. And this is one route on one build: the narrow build
was not driven through it, because the marginal question it would answer is smaller
than the variance the route introduces. The documented route drifts — five
ten-minute runs were lost to that in September — and a comparison needs the same
scene twice, which is what the title screen gave and this does not.

**For E08-S01's budget**, the entry is not one ratio. The recompiled code is 72% of
the frame in a menu and 50% in a world scene, and a budget that carries a single
figure for it will be wrong in whichever of the two it was not measured in.

### A quarter of the frame is in neither the game nor the renderer

Three instruments now report on the same frame and they can be put together, with
the caution that one of them is a per-interval ratio and the other two are
cumulative means, so the arithmetic below is approximate and is offered as an
accounting rather than a measurement.

**The renderer runs outside the guest threads, and the run with the diagnostic
renderer proves it without reading a line of the threading code.** Take the whole
graphics pipeline away and `busy` — the share of wall on which guest threads are
scheduled — *rises*, 72% to 82%. Had the renderer been running on a guest thread,
removing it would have taken guest execution away with it and the share would have
fallen. It rose, so the time the renderer spends is wall time the guest is not
executing: the game thread is waiting on it.

So, for the adventure hub, from a 213 ms frame:

    the renderer, its own clock                    ~58 ms     27%
    guest threads executing, 50% of wall          ~106 ms     50%
    neither                                        ~49 ms     23%

`elsewhere`, which the renderer computes exactly as period minus render, is 154 ms,
and it divides into those 106 and 49.

**Nothing in this repository has ever measured that 49 ms.** It is not the
recompiled code, which is the 106; it is not the renderer, which is the 58; and at
roughly a quarter of the frame it is larger than the whole 33.3 ms budget. The
candidates are the ones this port has been building instrumentation around all along
— the SP and DP handshake, the emulated vertical interval, the audio output path,
the host's own scheduling — and patches 0035 through 0040 already trace the first of
them behind `DKR_TRACE_SP`.

That is the next measurement, and it is a better one than any remaining question
about the recompiled code. This section has spent two days establishing that the
game's own execution is a flat floor that does not respond to having a third of its
instructions removed. The 49 ms has never been looked at.

### The 49 ms, measured: a sixth of all time is nobody working — 21 September 2026

The previous entry arrived at the remainder by subtracting one instrument's ratio
from another's per-display-list mean. Two instruments with different populations
and different denominators should not be subtracted from one another, so the
remainder now has an instrument of its own: patch 0042 counts running guest threads
and stamps each edge between one and none, which gives the interval exactly. And it
asks the renderer, at the moment the last guest thread goes to sleep, whether it is
inside a display list — so the wait is not merely measured but charged.

At the title screen, over 95.6 seconds:

    guest threads executing             64.7 s    67.6%
    no guest running, renderer drawing  14.2 s    14.9%
    no guest running, nothing drawing   16.8 s    17.5%
                                                 -------
                                                 100.1%

**The accounting closes**, which is the first thing to say about it: 67.6 and 32.4
are two independent measurements from the same hook and they sum to the wall. The
subtraction the previous entry performed was giving roughly the right answer for
roughly the wrong reason, and now it does not have to.

6,550 intervals, a mean of 4.73 ms each. The interval count rose steadily through
the run — 1040, 2132, 3204, 4208, 5163, 6224 — which matters because the obvious way
for this instrument to break is a guest thread that exits while counted, leaving the
running count permanently above zero and the accounting silently switched off. It
did not happen here. A flat count is the signature and the harness now warns on it.

#### The half that is not the renderer

Fourteen points of the wait are the game thread blocked while the renderer draws,
which is unsurprising and is the serialisation the pipeline is built around.

**Seventeen and a half points are neither.** No guest thread is running and nothing
is being drawn — a sixth of every second, on a machine that needs to be five times
faster than it is. That is not the recompiled code, which this note spent two days
establishing is a flat 150 ms floor that does not respond to losing a third of its
instructions; and it is not the renderer, which is timed separately and was idle
throughout.

What it is has not been measured. The candidates are the ones the runtime's own
patches already trace — the SP and DP handshake, the emulated vertical interval, the
audio output path — and the instrument that would name it is the same one again with
the wake source recorded beside the interval rather than only the renderer's state.

**This is the first number in the note that is neither the game nor the graphics**,
and at a sixth of the wall it is larger than several of the budget's named items put
together. Whether it is reclaimable is unknown. That it is there is now measured.

#### Who ends the silence

Patch 0043 records the id of the thread that wakes first out of each interval, and
the ids are recoverable from the game's own creation sites — the second argument to
`osCreateThread`, read out of the recompiled code:

    id  created by           what it is                 of the wait   wakes   mean
     5  osCreateScheduler    the scheduler                   43.8%     2737   4.95 ms
     3  thread1_main         the game thread                 31.2%     1845   5.25 ms
     4  amCreateAudioMgr     the audio manager               21.6%     1512   4.43 ms
     1  recomp_entrypoint    the idle thread                  3.4%      503   2.07 ms
    30  bgload_init          the background loader             0%         1

The columns sum to 31.0 s and 6,598 wakes, which are the same 31.0 s and 6,598
intervals the previous table measured, so the attribution is complete rather than a
sample.

**Nearly half of the dead time ends when the scheduler wakes.** In libultra the
scheduler is the retrace loop: it waits on the vertical interrupt and hands the
graphics and audio tasks out. A fifth ends when the audio manager wakes. Between
them, two thirds of the time nobody is working is time the guest world is waiting on
a *clock*, not on a computation.

The game thread's 31% is the other shape — it is the thread that would be waiting
for a task it submitted to come back.

**What this names and what it does not.** It names who ended each wait, which is not
the same as what every sleeping thread was waiting for: the scheduler waking first
does not prove the others were waiting for the scheduler. And the mean interval is
about 5 ms in every row, which is suspiciously close to uniform and deserves a look
of its own — a 60 Hz retrace is 16.7 ms and an audio frame is neither.

What it does establish is the shape of the remaining question. The port's time
divides into 68% computing, 15% waiting for the renderer, and **17% waiting for a
clock** — and the third of those has never been an item in the budget at all.

#### Reproducibility

Two runs, the second after adding the waker attribution:

    run 1   none-running 32%   renderer 45.9%   other 54.1%   6550 intervals
    run 2   none-running 32%   renderer 44.7%   other 55.3%   6598 intervals

Same machine, same route, a little over one percent apart on the split. The
instrument is stable enough to compare builds with, which is what it will be used
for next.

#### The five milliseconds was an average of two populations

The mean wait came out near five milliseconds for every thread, which was
suspicious enough to instrument: four threads waiting on different things have no
reason to agree. Patch 0044 records the shape in octave buckets rather than the
mean, and the shape is bimodal.

    bucket     count   of n   est. time   of t
    <256 us     3246   50.0%     0.59 s    2.0%
    <512 us      705   10.9%     0.26 s    0.9%
    <1 ms        239    3.7%     0.17 s    0.6%
    <2 ms         91    1.4%     0.13 s    0.4%
    <4 ms        194    3.0%     0.56 s    1.9%
    <8 ms        369    5.7%     2.14 s    7.2%
    <16 ms      1100   16.9%    12.74 s   43.1%
    <32 ms       531    8.2%    12.30 s   41.6%
    <64 ms        13    0.2%     0.60 s    2.0%

Estimating each bucket at its geometric midpoint totals 29.6 s against the 30.7 s
measured, so the histogram accounts for the time rather than merely sorting it.

**Half the waits are two hundred microseconds and cost nothing.** That is
`moodycamel::LightweightSemaphore`, which `UltraThreadContext::running` is, spinning
its default ten thousand iterations before it sleeps. Three thousand of those a run,
two percent of the idle time. Worth knowing — on a single emulated core a spinning
thread delays the one it is waiting for — but not worth chasing.

**A quarter of the waits, between 8 and 32 ms, are 85% of the time.** 1,631 of them
over about 480 frames: **3.4 such waits a frame, of roughly 16 ms each.** That is
some fifty-five milliseconds of a hundred-and-ninety-millisecond frame spent in
waits the length of one vertical interval, and the thread that ends the largest
share of them is the scheduler — libultra's retrace loop.

#### What that points at, and what would confirm it

The reading is that the frame is being quantised to the emulated vertical interval:
the game finishes a piece of work, waits for the next retrace boundary before the
next piece is dispatched, and pays a rounding-up several times a frame. At 5 fps the
game is nowhere near 60 Hz and should never be waiting for a retrace at all.

It is a reading and not yet a measurement. The buckets straddle 16.7 ms rather than
landing on it, and "the scheduler woke first" is not "the thread was waiting for a
retrace". What would settle it is the vertical interval's own period timed on this
machine and the retrace count per frame beside it — which is a smaller instrument
than either of the two already added here.

~~If it holds, it is the first thing this note has found that is neither the
recompiled code nor the renderer and is plainly wasteful: not work that is slow, but
work that is not being done while the machine waits for a clock it has already
missed.~~

**It does not hold. Refuted the same day.** `ultramodern::get_speed_multiplier` is a
compile-time constant that scales both the vertical interval's cadence and the
guest's own counter together, so setting it to 2 doubles the retrace rate without
changing what the game perceives. Both builds, compared at the same wall offset:

    VI rate     busy    idle    renderer   other   waits 8-32 ms   switches/s
    60 Hz      67.9%   32.1%      14.6%   17.5%      25.1%            68.2
    120 Hz     66.4%   33.8%      15.2%   18.6%      24.4%            74.0

The cluster does not move. Idle goes marginally *up*. The change did take effect —
the switch rate rises 8.5%, which is the scheduler seeing twice as many retraces —
and the waits are simply not made of retraces.

So the 8 to 32 milliseconds are set by something whose period is not the vertical
interval, and the reading above was wrong. Written out rather than quietly replaced,
because the experiment took an hour and the reason it was worth running is that the
reading was plausible.

#### The quantum was the right guess and the wrong conclusion

If the waits are not retraces and four unrelated threads agree on their length, the
next candidate is the scheduler's own slice. And there was a plausible cause sitting
in the runtime: `ultramodern::set_native_thread_priority` computes a native priority
for every thread it creates — `Critical` for the vertical-interval thread,
`VeryHigh` for the timer — and then does not apply it. The `SetThreadPriority` call
is commented out upstream. On one core with every thread at NORMAL, a signalled
thread does not preempt; it waits for the running slice to end, which is tens of
milliseconds on Windows 95.

Applying it, on this target only, does exactly what the hypothesis predicted to the
waits:

    build                   busy    idle    waits 8-32 ms    switches/s
    priorities unapplied   67.9%   32.1%   1631 of 6495       68
    priorities applied     97.0%    2.6%     32 of 22427     124

The cluster is gone. 93% of the remaining waits are under 256 µs — the semaphore's
spin and nothing else. Guest threads run 97% of the wall.

**And the game stops.** Over 181 seconds the renderer processed **one** display
list, against 477 in the baseline's 95; `gGameMode` never left INTRO, where the
baseline reaches the title screen in twenty seconds. The screen holds the logo and
does not advance. The frame figures the run produced are a single sample and mean
nothing.

So the waits were the quantum, and the quantum was not the enemy. Give the guest
threads priority on one core and they take it — from the graphics thread, which is
the one that has to run for the frame to finish.

#### Which corrects what the idle time was called

The entry above called the 17.5% "nobody executing, nothing drawing", and that is
wrong in its second half and misleading in its first. The instrument counts **guest**
threads. The ultramodern graphics thread, the audio output, and Windows 95 itself are
none of them guest threads, and everything they do lands in that bucket looking like
silence.

The renderer's time *inside* `send_dl` is separately accounted, which is what made
the mistake easy: having subtracted the part of the renderer that is measured, it
was tempting to read the remainder as empty. The priority experiment is what says
otherwise — take that time away from whoever was using it and the frame stops
finishing.

What the 17.5% is remains open. What it is not is idle, and the instrument that
would settle it has to time the non-guest threads too, which neither patch 0042 nor
0043 does.

#### The synchronised swap costs nothing here, and E06-S04 can stop wondering

`dkr_glide_swap` called `grBufferSwap(1)` — schedule the flip for the next vertical
retrace and block until it comes — under a comment saying the choice between that
and an immediate swap "is measured in E06-S04". E06-S04's criterion for it is
unticked; the measurement had not been taken. It has now.

The argument for expecting a cost was good: E06-S04 says in as many words that a
game which misses its deadline loses a whole scan to a synchronised swap, and this
game misses its deadline by a factor of eleven. A 16.7 ms wait inside a 190 ms frame
would be nine percent, and it would sit inside both the renderer's own timing and
the interval on which every guest thread is blocked.

Same binary, same route, compared at the same wall offset:

    swap            busy    idle   renderer   other   switches/s
    synchronised   67.7%   32.3%     15.2%   17.2%       69.4
    immediate      67.9%   32.2%     14.5%   17.7%       69.1

Nothing. The emulated Voodoo 2 does not make the caller wait for a scan, so the
choice is free here and the no-tearing one is the one to keep.

**Said once, in the log, because it had to be.** The first run of this experiment
produced the same null result and was worthless: a switch that silently fails to
take and a switch that takes and changes nothing are the same measurement. The
second run carries `[gfx][swap] immediate (DKR_GLIDE_SWAP=immediate)` and is the one
quoted. It cost a build and a run to learn that again.

**And it is a measurement of this machine.** On real silicon the retrace is real and
the wait would be too; what is settled is that 86Box's card does not model it, which
is a fact about the bench rather than about the port. E09-S04 still owns the
question.

`update_screen` was brought into the renderer's flag at the same time — it runs on
the graphics thread, outside the display-list path, and was invisible to both
accounts at once. It moves about half a point from "other" to "rendering". The
unattributed share is still seventeen.

### The seventeen percent is the graphics thread, outside the part that was timed

Three candidates for the unattributed wait were tested and refuted — the vertical
interval, the scheduler quantum, the synchronised swap — and the third experiment
along the way established that the time is not idle. So it was measured directly
rather than guessed at again. The graphics thread now counts its own rounds, its
empty rounds, and the span it spends awake.

Over 80.6 seconds at the title screen:

    guest threads not blocked                       55.4 s   68.8%
    no guest running, renderer flagged              11.8 s   14.6%
    no guest running, nothing flagged               13.4 s   16.7%

    graphics thread not blocked                     23.5 s   29.2%
      of which inside send_dl and update_screen              14.6%
      of which elsewhere in its own loop                     14.5%

**14.5 against 16.7.** The graphics thread awake outside the two functions that
were timed is, to within a couple of points, the whole of the share nothing could
account for. The seventeen percent has a name.

#### What it is doing there

    9,670 rounds in 80.6 s   120 a second
    5,719 of them empty      59%
    2.43 ms awake per round  on average

Its loop polls the action queue on a one millisecond timeout — `wait_dequeue_timed
(action, 1ms)` — so it was expected to wake a thousand times a second. It wakes a
hundred and twenty, which says the timeout is rarely what ends the wait, and **three
rounds in five find nothing at all**. The work between the two timed functions is
the loop itself: the variant dispatch, `sp_complete`, the DP edge publication, the
queue handling, and the clock reads.

#### The caveat this number carries

"Not blocked" is not "executing". A thread that has been preempted is runnable and
counts as awake here, and the same is true of `guest-run` on the other side of the
table — the instrument in patch 0041 stamps from resume to the next voluntary block,
and Windows may have taken the processor away in between. On a single emulated core
the two cannot really overlap, so 68.8 and 29.2 summing to ninety-eight is a
coincidence of two ceilings rather than a closed account.

What survives the caveat is the comparison that matters: the graphics thread's
unflagged time and the unattributed idle are the same size, they move together, and
no other thread was found doing anything of that order. The budget's missing item is
the graphics thread's own loop.

#### Which is a different ticket from the one it was charged to

The frame's 150 ms of `elsewhere` has been read all along as the recompiled game.
Part of it is the graphics thread running between display lists, and that belongs to
E08-S03 and E05, not to E08-S02. Whether the loop can be made cheaper — a queue that
blocks properly instead of polling, fewer rounds that find nothing — is work that has
never been scoped, because until now nobody knew it cost anything.

### The graphics thread, decomposed

Attributing the thread's awake time to the loop was as far as the last measurement
went, and it left two stories that fit the same six seconds: three thousand screen
updates at two milliseconds each, or four hundred display lists at sixteen
milliseconds each outside `send_dl` — the latter being where an eight-megabyte RDRAM
snapshot is released. Splitting the awake time by what the round was handling
settles it, and the answer is both, in comparable parts.

Over 80.3 seconds at the title screen:

    display-list rounds     389 x  49.10 ms = 19.10 s   23.8% of wall
      of which in send_dl   389 x  37.61 ms = 14.63 s   18.2%
      of which outside it   389 x  11.49 ms =  4.47 s    5.6%
    screen updates         3371 x   0.99 ms =  3.35 s    4.2%
    empty rounds           5594 x   0.01 ms =  0.03 s    0.0%

**The poll costs nothing.** An empty round is 5.5 µs — the loop's one-millisecond
timeout, its variant test and its clock reads together. Three rounds in five are
empty and they account for three hundredths of a second in eighty. Whatever is worth
attacking here, it is not the polling.

**A display list costs 11.5 ms outside the renderer.** That is `sp_complete`, the DP
edge publication, and the destruction of the queued action — which frees the RDRAM
snapshot that patch 0007 attaches to every graphics task. Eleven and a half
milliseconds per frame, on a frame that is a hundred and ninety, for work that is
not drawing.

**And the screen updates are nine to one.** 3,371 of them against 389 rendered
frames: the vertical-interval thread enqueues one per retrace, the queue coalesces
to at most one pending, and **8.7 still get through for every frame the game
actually produces.** Each costs 0.99 ms in a handler that assigns two registers,
clears a flag, and calls a function that increments a counter — so the cost is the
queue round trip rather than the work, and it is paid nine times for one frame's
worth of effect.

#### What is worth taking, and what is not

Ranked by what they cost the wall:

    the renderer proper                18.2%   E05, and the subject of E08-S03
    a display list's non-drawing tail   5.6%   sp_complete, DP edge, 8 MB free
    redundant screen updates            4.2%   8.7 per frame, 1 ms each
    the queue poll                      0.0%   nothing

The third is the only one that is plainly waste rather than work: presenting the same
image nine times between two frames buys nothing that presenting it once would not.
It is four percent of the wall, which is eight milliseconds of a hundred-and-ninety
millisecond frame — small beside the floor, and the first thing found in this note
that could simply be deleted.

The second is not waste but it is not drawing either, and an eight-megabyte
allocation released on the consumer's thread every frame is worth looking at on its
own terms.

Neither of them changes the verdict. The floor is still the floor.

### Deleting the redundant presents: the first gain this note has measured

The decomposition left one item that was plainly waste rather than work — 8.7 screen
updates for every frame the game produces — so it was removed and measured.

The vertical-interval thread posts a `ScreenUpdateAction` every retrace and the queue
keeps at most one pending, which was assumed to coalesce enough. It does not: the
graphics thread drains the pending one long before the game draws again, so the next
retrace posts another. The change is to post one only when a display list has been
handled since the last, with the first sixteen let through because the very first is
what starts the recompiled game.

It is safe here for a reason worth writing down rather than assuming:
`ultramodern::renderer::get_vi_regs` — the reader of what these actions carry — **has
no caller in this tree**. RT64 is its consumer and RT64 is not built for Windows 95.
`update_screen` neither presents nor draws; it assigns two registers, clears a flag
and increments a counter.

Control on the same binary, truncated to the same number of frame reports:

    policy             period      render      elsewhere    updates
    every retrace     190.2 ms    38.2 ms      152.0 ms      3,770
    only on a frame   184.9 ms    38.2 ms      146.8 ms        386

**−2.8% on the frame, and the renderer's own figure is unchanged** — 38.2 ms on both
sides — so the whole of the gain is in `elsewhere`: **5.2 ms a frame**. Ninety
percent of the updates are gone and the game reaches the title screen as before.

#### What it is worth, honestly

Five milliseconds out of a hundred and ninety, against a budget of thirty-three. It
does not move the verdict and was never going to. What it does is close a loop this
note has been open on for two days: the frame's `elsewhere` was read as the
recompiled game, then measured to contain the graphics thread, then decomposed, and
the one part of it that bought nothing has been deleted and the deletion measured.

It is also the first change in this investigation that made anything faster. The
register narrowing removed a third of the emitted instructions for nothing; this
removes about six thousand queue round trips a minute for three percent. The ratio
between effort and effect is the opposite of what the instruction counts predicted,
which is the note's recurring lesson in one line.

On by default where it was measured, off elsewhere, and `DKR_VI_PRESENT=every`
restores the old behaviour — which is how the two were compared.

### Sixty-three percent of the frame was a memory copy, and half of it was zeroes

The `elsewhere` floor — 150 ms a frame, flat across every scene, unmoved by removing
a third of the recompiled instructions — has been read as the game's own execution
since August. It is not. Most of it is one `memcpy`.

Every graphics task carries an eight-megabyte snapshot of RDRAM, taken on the guest
thread that submits it (patch 0007, so that the CPU can recycle its display-list
buffers while the task is queued). Timed:

    zero-fill by make_unique   50.99 ms a snapshot   157 MB/s
    memcpy of 8 MB             64.70 ms a snapshot   247 MB/s
    together                  115.69 ms              63% of a 182 ms frame

The bandwidths are the first thing to check and they settle the doubt that has
attached to every other number in this note: 157 MB/s for a write-only fill and
247 MB/s for a copy are what a Pentium II on a 100 MHz bus achieves. This is memory
traffic, not a thread being preempted with the clock running.

**And half of it is a fill nobody reads.** `std::make_unique<uint8_t[]>` value-
initialises; the memcpy on the next line overwrites all eight megabytes of it.

    same binary, same route, same number of frame reports
                  period      render     elsewhere     snapshot
    zero-filled  209.5 ms    40.9 ms     168.6 ms     115.6 ms
    raw          187.3 ms    39.9 ms     147.4 ms      94.0 ms

**−10.6% on the frame**, 4.77 fps to 5.33. The copy itself grows, 64.8 to 93.4 ms,
because the page faults move into it instead of happening during the fill; the net
is twenty-one milliseconds a frame all the same.

#### What this does to the note's own conclusions

It does not overturn the verdict — 187 ms against a 33 ms budget is still five times
over. But it reopens something the verdict rested on.

E00-S03 measured 125 ms of a 170 ms frame as "recompiled code" and the go/no-go was
pronounced on that split. The measurement was the interval between graphics tasks,
which counts whatever the guest thread was doing, and **the guest thread was
spending most of it inside an eight-megabyte memcpy that belongs to the runtime's
task queue, not to DKR**. The recompiled code's real share is smaller than the
verdict assumed, and by an amount nobody has yet measured.

Which is consistent with the thing that made no sense until now: removing 30% of the
emitted instructions changed the frame by 0.6%. If the recompiled code were 73% of
the frame that would be inexplicable. If it is a third of it, sitting behind a copy
that is 63%, it is exactly what should have happened.

The ranked list from two days ago now reads:

    the RDRAM snapshot, per display list   63%   half of it deleted here
    the renderer proper                    18%   E05, E08-S03
    a display list's other overhead         6%   sp_complete, DP edge
    redundant screen updates                4%   deleted
    the recompiled game                      ?   never separately measured

The last row is the one this note has spent two days arguing about, and it has never
once been measured on its own.

### The frame budget, closed — 22 September 2026

E08-S01 has wanted this table since August. It closes now because the snapshot is
timed from inside the guest thread that takes it, which is what was missing: the
instrument that separates the runtime's work from the game's.

One run, the raw-allocation build, 85.5 seconds and 400 display lists:

    RDRAM snapshot              94.0 ms a frame    50.2%
    guest, minus the snapshot   50.9 ms            27.2%
    renderer (send_dl)          39.9 ms            21.3%
    --------------------------------------------------
    sum                        184.8 ms            98.7%
    measured frame period      187.3 ms
    unaccounted                  2.5 ms             1.3%

Three quantities, measured by three different instruments against the same clock,
summing to the frame within one and a third percent. On a single emulated core they
serialise, so the sum is the right operation.

**The middle row is by subtraction** and carries that weight: `guest-run` minus the
snapshot's own stamps. It is the one row here that was not measured directly, and it
is the row everything else in this note has been about. Within it sit the recompiled
game, the audio microcode this note measured at 5.5 ms a frame in August, and
libultra's scheduler.

#### What it costs the verdict

E00-S03 pronounced the go/no-go on a frame of 170 ms of which **125 ms was
"recompiled code"**, measured as the interval between graphics tasks. That interval
counts whatever the guest thread was doing, and what it was doing, for most of it,
was an eight-megabyte `memcpy` belonging to `ultramodern`'s task queue. The
recompiled game's real share is **50.9 ms**, and that figure still has audio and the
scheduler inside it.

Which finally explains the result that made no sense: removing 30% of the emitted
instructions moved the frame by 0.6%. At 125 ms of 170 that is inexplicable. At 45
of 187, behind a copy that is 94, it is what should have happened.

**It does not make the port viable.** 187 ms against 33.3 is five and a half times
over, and deleting the snapshot entirely — which nothing here has shown to be
possible — would leave 93.3 ms and 10.7 fps. The floor is still far above the
budget.

What changes is where the floor is. It was "the recompiled game, and nothing can be
done about it". It is now "an eight-megabyte copy per frame that the runtime does on
the game's behalf, and which no measurement has yet shown to need its full size" —
DKR's display lists reference RDRAM widely, so a partial snapshot is a correctness
question and not a free win, but it is a question, where the recompiled game's cost
was an answer.

#### The order the remaining work should be looked at in

    the RDRAM snapshot      50%   half already deleted; the rest is a design question
    the recompiled game     27%   E08-S02, whose five levers now address a quarter
                                  of the frame rather than three quarters
    the renderer            21%   E05, E08-S03
    everything else          1%

### The copy was twice the size it needed to be — 4.77 fps to 7.20

Half the snapshot's cost was a zero-fill nobody read, and deleting it gave 10.6%.
The other half is the copy, and the question it raises is whether eight megabytes is
the right number.

It is not. `osMemSize` answers eight megabytes to the guest, so the snapshot has
always been eight. DKR is a four-megabyte game. Scanned backwards for its last
non-zero byte, once every hundred display lists over four hundred of them, the
high-water mark is

    0x3FFF68 — 4,095 KB of 8,192

a hundred and fifty-two bytes below the four-megabyte line, and never once above it.
Half of every copy was memory the game has never written, and the texture images the
renderer asks for span 0x20CCE0 to 0x3712C0 — all inside the half that is kept.

The allocation stays eight megabytes so that a read above the line is still in
bounds; only the copy shrinks. Same binary, same route:

    copy size    copy       period      render      elsewhere    fps
    8192 KB     93.5 ms    188.6 ms    39.8 ms     148.8 ms     5.30
    4096 KB     46.7 ms    138.9 ms    39.9 ms      99.0 ms     7.20

**−26.4% on the frame, +35.8% on the rate**, and the renderer's own figure does not
move — 39.8 against 39.9 — which is what says the gain is the copy and not the
weather.

#### Where the stack now stands

Against the state this investigation found:

    zero-filled, 8 MB      209.5 ms    4.77 fps
    raw, 8 MB              188.6 ms    5.30 fps
    raw, 4 MB              138.9 ms    7.20 fps

**−33.7% on the frame and +51% on the rate**, from two changes that delete work
rather than do it faster: a fill that was overwritten, and a copy of memory that was
never written.

And the frame closes again, at 4 MB:

    RDRAM snapshot              47.3 ms    34.1%
    guest, minus the snapshot   50.9 ms    36.6%
    renderer (send_dl)          39.9 ms    28.7%
    --------------------------------------------
    sum                        138.1 ms    99.5%
    measured period            138.9 ms

The middle row is **50.9 ms in both configurations** — the same number from the same
subtraction in two runs whose other terms differ by a factor of two. That is the
strongest evidence yet that the row is real, and it is the recompiled game, the
audio and the scheduler together.

#### What is left, and what it would take

    the recompiled game        36.6%   E08-S02, whose levers measured at nothing
    the renderer               28.7%   E05, E08-S03
    the RDRAM snapshot         34.1%   a copy per display list that still exists

The snapshot is now the smallest of the three and still a third of the frame. What
would remove the rest of it is not another size reduction but a different shape: the
buffer is allocated and freed per display list, and Windows zeroes a fresh
eight-megabyte block in the page-fault handler, which is why removing the explicit
fill made the copy slower rather than making the frame faster by the fill's whole
cost. **A pool of two reused buffers would pay neither.** That is a change to how
`ultramodern` owns the queued task's memory, and it is the next thing worth trying.

None of this reaches 33.3 ms. 138.9 is still four times over, and E00-S03's verdict
stands on its conclusion. What has changed in two days is that the frame went from
"170 ms of which 125 is the recompiled game, and nothing can be done" to "138.9 ms
of which 50.9 is the game, and two of the three remaining items are the runtime's
own bookkeeping".

### A buffer that is reused pays the page faults once — 8.47 fps

Halving the copy left the allocation, and the allocation is not free even after the
explicit fill is gone. Windows hands back untouched pages and zeroes them when they
are first written, which is why removing `make_unique`'s fill made the copy slower
instead of saving the fill's whole cost: the zeroing moved into the fault handler.

Two buffers, zeroed once at their birth and reused, pay neither. Three
configurations, the same instrument in each:

    configuration                     alloc      copy   snapshot   period    fps
    pool of two, 4 MB copied        0.23 ms   32.28 ms  32.51 ms  118.0 ms  8.48
    fresh raw alloc, 4 MB copied    0.66 ms   46.68 ms  47.34 ms  138.9 ms  7.20
    fresh zeroed alloc, 4 MB       50.89 ms   32.39 ms  83.28 ms  176.5 ms  5.67

**The third row settles the mechanism.** With the pages pre-touched by the explicit
fill, the copy is 32.4 ms — the same as the pool's — so the raw allocation's 46.7 ms
copy was carrying fourteen milliseconds of fault-zeroing inside it. Nothing was
saved by not zeroing; it was only moved. The pool is what removes it.

Sixteen megabytes standing against eight that were transient. The test machine has
sixty-four, and if both buffers are busy the allocator is used as before, so the
queue can never block on the pool: 600 acquisitions in the measured run, 0 misses.

#### Where the frame is now

    zero-filled, 8 MB, fresh     209.5 ms    4.77 fps    where this began
    raw, 8 MB, fresh             188.6 ms    5.30 fps
    raw, 4 MB, fresh             138.9 ms    7.20 fps
    pooled, 4 MB                 118.0 ms    8.47 fps

**−43.7% on the frame and +77.6% on the rate**, from three changes that between them
add no cleverness at all: stop filling a buffer that is overwritten, stop copying
memory the game never wrote, stop asking the operating system for the same eight
megabytes sixty times a minute.

    RDRAM snapshot              32.5 ms    27.6%
    guest, minus the snapshot   34.0 ms    28.8%
    renderer (send_dl)          38.4 ms    32.5%
    unaccounted                 13.1 ms    11.1%

~~The guest row is **50.9 ms for the third time**, unchanged across configurations
whose other terms have moved by a factor of three.~~

**That was wrong and the number above is the corrected one.** 50.9 ms was the
previous configuration's figure, carried over instead of recomputed; this run's
guest-minus-snapshot is 34.0 ms, and the table as first published summed to 121.8 ms
in a frame of 118, which is the arithmetic that should have caught it. The row is
not invariant across configurations and was never measured to be.

#### What that leaves

Still three and a half times the 33.3 ms budget, so the verdict is untouched in its
conclusion for the third time. But the shape of the remaining problem is finally the
one the tickets describe: a renderer at 32.5% that E05 and E08-S03 own, a game at
43.1% that E08-S02 owns, and a snapshot at 27.6% that is now the smallest of the
three rather than the largest.

Whether the snapshot can go further is a different kind of question from the three
above. Those were waste. What is left is a genuine copy of four megabytes, and
removing it means changing when the guest is allowed to touch its display-list
buffers — which is what patch 0007 bought with it, and what the crash at display
list 344 cost to learn.

### The guest, split by thread: the game is twelve percent of its own frame

With the runtime's bookkeeping cut from 63% of the frame to 27%, the guest's own
time became the item worth decomposing, and it never had been: the recompiled game,
libultra's scheduler and the audio manager were one number. `guest-run` is already
stamped per thread; bucketing it by the id the game gave each thread costs nothing.

The ids come from the game's own `osCreateThread` calls: 1 the idle thread from
`recomp_entrypoint`, 3 the game thread from `thread1_main`, 4 the audio manager from
`amCreateAudioMgr`, 5 the scheduler from `osCreateScheduler`.

500 display lists, a frame of 122.5 ms:

    id 5   the scheduler          38.80 ms   31.7%   the snapshot's 32.7 is inside it
    id 1   the idle thread        17.23 ms   14.1%
    id 3   the game thread        15.32 ms   12.5%
    id 4   the audio manager       2.11 ms    1.7%
           renderer (send_dl)     38.82 ms   31.7%
    ------------------------------------------------
           sum                   112.38 ms   91.7%
           unaccounted            10.16 ms    8.3%

The scheduler's figure is where the snapshot lands, because the scheduler is the
thread that submits the graphics task. Take it out and libultra's scheduler costs
**6.1 ms**.

#### Two things fall out of this and neither was expected

**The recompiled game is 15.3 ms.** Twelve and a half percent of its own frame. E00-S03
put it at 125 of 170; the honest figure, with the runtime's copy taken out and the
scheduler and audio separated from it, is an eighth of a frame that is itself down
to 122 ms. E08-S02's five levers address that eighth.

**The idle thread costs more than the game.** 17.2 ms against 15.3. libultra's idle
thread exists to have something to run when nothing else can; under `ultramodern` it
is a real host thread, and on one emulated core the time it is not blocked is time
the others do not get.

That second one carries the caveat this note has repeated all session: *not blocked*
is not *executing*. A thread that is runnable but preempted accumulates here exactly
as a thread that is working. The idle thread is the one place where that distinction
decides whether there is anything to win — a spinning idle loop is 14% of the frame
to reclaim, and a thread merely sitting in the run queue is nothing at all. The
instrument as built cannot tell them apart.

#### The frame, as it now stands

    the RDRAM snapshot          32.7 ms   26.7%   a real copy; what is left of 63%
    the renderer                38.8 ms   31.7%   E05, E08-S03
    the idle thread             17.2 ms   14.1%   unexplained, and possibly nothing
    the recompiled game         15.3 ms   12.5%   E08-S02
    the graphics thread's loop  10.2 ms    8.3%   measured earlier at 5.6% + presents
    libultra's scheduler         6.1 ms    5.0%
    the audio manager            2.1 ms    1.7%

Four of the seven rows are the runtime's, not the game's, and together they are
two thirds of the frame.

### The idle thread is mostly asleep, and the code said so before the clock did

The question the previous section left open -- is the idle thread's 17.2 ms work,
or a thread that merely holds its place? -- has an answer in the recompiled code.
The game's idle loop is a `b .` at `0x80065E18` in `thread1_main`, and N64Recomp
emits it as a call to `pause_self`:

    extern "C" void pause_self(RDRAM_ARG1) {
        while (true) {
            ultramodern::wait_for_external_message(PASS_RDRAM1);
            ultramodern::check_running_queue(PASS_RDRAM1);
        }
    }

`wait_for_external_message` blocks on a `moodycamel` semaphore until a VI, SP or DP
event arrives. The idle thread does it **while holding the guest token**: it never
passes through `wait_for_resumed`, so none of 0048's clock reads fall inside the
sleep, and the whole of it is charged to thread 1 as running time.

So the instrument was not unable to tell spinning from queued, as the note
assumed: it was counting a third thing, a thread blocked in the kernel with the
token in its hand. Patch 0049 times that wait and prints it beside `[trace][ran]`.
One run on the test machine, glide renderer, 80 s, a frame of 121.5 ms:

    [trace][ran]     t1=9024865 us
    [trace][parked]  t1=5602408 us     62.1% of the idle thread's "run"

Applied to 0048's figure:

    the idle thread, as reported     17.2 ms
      asleep in pause_self           10.7 ms   waiting on an interrupt
      executing                       6.5 ms   delivering it, handing the token over

The 10.7 ms is not CPU the frame can have back. It is the frame waiting for
something -- the renderer, a vertical interval -- and it belongs with the
"no guest running" time 0042 measures, not with any guest thread. The 6.5 ms is
real: `do_send` and the switch to the thread the message woke, once per
interrupt. It is the runtime's cost, not the game's, and it is small beside the
snapshot and the renderer.

The frame, corrected:

    the renderer                38.8 ms   31.7%   E05, E08-S03
    the RDRAM snapshot          32.7 ms   26.7%   a real copy
    the recompiled game         15.3 ms   12.5%   E08-S02
    waiting, charged to idle    10.7 ms    8.7%   not CPU
    the graphics thread's loop  10.2 ms    8.3%
    delivering interrupts        6.5 ms    5.3%   the idle thread's real work
    libultra's scheduler         6.1 ms    5.0%
    the audio manager            2.1 ms    1.7%

The idle thread no longer costs more than the game. The three items worth a
ticket are unchanged, and they are the ones E08-S01 was meant to find: the
renderer, the snapshot, and the recompiled code, in that order.

### The 48% with no guest thread running is the processor, busy elsewhere

Patch 0042 has said since the start that no guest thread runs for 48% of the
wall, and could name half of it: the renderer's bit, sampled when a silence
begins, covered 45%, and the remaining 55% was "other". Two things in the logs
pointed at the audio microcode. It runs on ultramodern's SP task thread, which is
not a guest thread, so on one processor its time can only show up as a silence.
And `[audio][cost]` put it at 26.7 s of microcode in an 80 s run, against 21.4 s
of unnamed silence.

Patch 0050 makes the game export `dkr_audio_busy` beside `dkr_renderer_busy`,
samples both at the start **and** at the end of every silence, and charges the
interval to the union. One run, 75 s, a frame of 123.4 ms:

    [trace][idle-by] neither=2241083 render=10459776 audio=11263068 both=12274423 us

                          total     share     per frame
    the renderer only    10.46 s    28.9%       17.2 ms
    the audio only       11.26 s    31.1%       18.5 ms
    both in progress     12.27 s    33.9%       20.2 ms
    neither               2.24 s     6.2%        3.7 ms
    -------------------------------------------------
    no guest running     36.24 s   100.0%       59.5 ms

**94% of the silence has the renderer or the audio microcode in progress.** The
silence is not waiting. It is the one processor running host threads that are
not guest threads, and the guest thread that was woken sits ready until the
Windows 95 scheduler hands it back. That is also where the histogram's hump comes
from. Most of the time is in silences of 4 to 32 ms, which is the scale of a
quantum, not of a handoff.

What is left, **3.7 ms a frame**, is the handoffs themselves. Some of that is the
histogram's other peak: 4,077 silences between 128 and 256 µs. That is the size
you would expect from `LightweightSemaphore` spinning 10,000 times before it
blocks. On one processor that spin can never succeed, because the thread it is
waiting for cannot run while it spins. The two have not been matched by
measurement yet.

#### What this does to the budget table

The audio microcode has never had a row in it, and the table still summed to the
frame. Both are true for the same reason. Every row is a **wall interval** taken
on one processor: the renderer's `send_dl`, each guest thread's run, the
snapshot. A display list preempted by an audio task goes on counting the audio
task's time as its own. The microcode, at 30.9 ms of wall per task and about 1.36
tasks per presented frame, is inside the other rows as preemption. So is a good
part of the renderer, inside the guest rows. The table partitions the wall, but it
does not partition the processor.

So, with what this instrument can and cannot say:

- The audio microcode is a first-rank item, the size of the renderer. E03-S03, the
  high-level mixer, already owns it, and `rsp-audio-cost-measured.md`'s trigger
  for starting that ticket is met a second time over.
- The 48% is not a separate problem to solve. It is the renderer and the audio,
  seen from the guest's side.
- A budget in processor time, rather than wall time, needs an instrument that
  knows which thread holds the CPU. On Windows 95 that means a sampler, because
  `GetThreadTimes` is not implemented there. Until one exists, no row of the table
  should be read as the CPU time of its item.

### The budget in processor time: the audio microcode is a quarter of the machine

The previous section ended on a limit: every row of the table is a wall interval
on one processor, and Windows 95 does not implement `GetThreadTimes`, so nothing
could say how much processor any item actually used. There is a way around it
that needs no new API. **A thread at `THREAD_PRIORITY_TIME_CRITICAL` cannot be
preempted by any other thread of the process**, so its wall interval is its
processor time, give or take hardware interrupts.

`runtime-recomp/src/game/exclusive_section.hpp` does exactly that under
`DKR_TRACE_EXCLUSIVE`, around the three host sections that have a timer already:
the audio microcode, `send_dl`, and `update_screen`. The measurement script passes
the variable through with
`DKR_MEASURE_SET="DKR_TRACE_EXCLUSIVE=1" scripts/Measure-Guest-Time-VM.sh`.

The frame did not move, 123.4 ms against 123.4 ms, which is what you expect from a
processor that was already full: raising priorities reorders the work but does not
remove any. The two timed sections lost a third of their wall each:

                              shared      exclusive
    audio microcode, a task   30.9 ms      20.9 ms
    send_dl, a display list   39.7 ms      29.7 ms

20.9 ms is the figure `rsp-audio-cost-measured.md` found by a different route, so
the method agrees with the one earlier measurement taken when little else competed.
The ten milliseconds that disappear from each were other threads' time, counted as
their own.

One run, 85 s, as shares of the wall and per 123.4 ms frame:

                                              share   per frame
    audio microcode           exclusive       24.4%     30.1 ms
    send_dl                   exclusive       22.9%     28.2 ms
    update_screen             exclusive        3.3%      4.1 ms
    graphics thread, rest     upper bound     12.2%     15.1 ms
    scheduler, id 5           upper bound     29.8%     36.7 ms   snapshot inside
    game thread, id 3         upper bound      9.0%     11.1 ms
    audio manager, id 4       upper bound      2.2%      2.7 ms
    idle thread executing     upper bound      4.2%      5.1 ms
    ---------------------------------------------------------
                                             108.0%

The exclusive rows are processor time. The others are still wall intervals, and a
section at time-critical priority preempts them just as the audio preempted the
renderer before. That is where the extra 8% sits, so each of them is an upper
bound. The table closes to within that 8%, with the idle thread's parked sleep
taken out. That means the processor is **never idle**: there is no waiting left in
the frame to remove, only work.

Three things change:

- **The audio microcode is the largest single item, 30.1 ms a frame**, and it has
  had no row in this table until now. ~~If its cost follows the audio played and not
  the frames drawn, and 1.52 tasks a frame at 8 fps suggests so, then it is a fixed
  quarter of the processor at any frame rate: 8.1 ms of a 33.3 ms frame at the
  target, before anything else runs.~~ **Wrong, and optimistic: see the next
  section. Real-time audio would need 63% of the processor, about 21 ms of a
  33.3 ms frame.** E03-S03 replaces it.
- **The recompiled game is at most 11.1 ms, not 15.3.** Its old figure carried the
  audio's and the renderer's preemption.
- **The renderer is 32.3 ms of processor** (`send_dl` plus `update_screen`), not
  the 39.7 ms its own line reports in a normal run.

In this mode the silence attribution from patch 0050 comes back as `neither` for
the whole silence. That is expected. A section that cannot be preempted runs
entirely inside a silence, and a start/end sample never sees it. Read `idle-by`
from normal runs only.

### Real-time audio would take 63% of the processor

The previous section guessed that the audio microcode's cost follows the sound
played rather than the frames drawn, which would make it a fixed quarter of the
processor at any frame rate. That was a guess, so it was measured. The game now
prints `[audio][rate]` every five seconds of wall clock. The line gives the
microcode's cumulative cost, the samples handed to the audio interface, the
interface's frequency, and the display lists drawn. The run was 240 s in exclusive
mode, so that the cost is processor time and the renderer's preemption, which
varies by scene, does not blur it.

First, what the audio interface is told. On Windows 95,
`platform::audio_frames_remaining` has no device behind it, because E06-S03 is
still TODO, and it always returns 0. DKR's audio manager reads that as an empty
DMA queue and synthesises its largest quantum every time: **exactly 848 frames,
38.5 ms of sound at 22,050 Hz, in each of 1,350 tasks.** No sound clock paces the
production, and nothing is played.

Over the 121.7 s of the run with a steady rate:

    tasks                          1,350      11.1 a second
    sound synthesised             51.9 s      42.7% of real time
    processor spent on it         32.6 s      26.8% of the wall
    processor per task            24.2 ms     10 to 35 ms by scene
    processor per second of sound  628 ms     263 to 922 ms by scene

Window by window, the task rate follows the frame rate (correlation 0.73). The
audio manager falls behind whenever the game does, so it is paced by the
processor it can get and not by the sound. The share of the processor it takes
does **not** follow the frame rate (correlation 0.07). What each task costs
depends on the scene: more voices, more work.

So neither reading of the guess survives:

- **The 25% is not a property of the audio.** It is what the audio manager
  manages to take while starving. It produces less than half of the sound the
  game needs, and would be heard at 43% speed if anything played it.
- **The invariant is the cost per second of sound, 628 ms.** Real-time audio
  would take **63% of this processor** on average, and up to 92% in the heaviest
  window. At the 30 fps target that is **about 21 ms of every 33.3 ms frame**, not
  8.1. Faithful microcode audio cannot run in real time on this machine, even with
  every other item at zero.

That settles `rsp-audio-cost-measured.md`'s trigger for E03-S03 beyond argument.
The high-level mixer is not the first optimisation of E08; it is a precondition of
the port having sound at all. E06-S03 will also have to give
`audio_frames_remaining` a real clock. Until it does, the game's audio pacing on
this target is an artefact, and cost figures per task are measured at the largest
quantum.

### Every row in processor time: the table is now `frame-budget.md`

Patch 0051 moves the exclusive sections into ultramodern's `threads.cpp` and
extends them to the RDRAM snapshot and to each round of the graphics thread. It
also charges every exclusive section that interrupts a counted guest thread to
that thread, as `[trace][preempted]`. That was the missing piece. The scheduler's
52.7 s of `ran` in a 100 s run turned out to be 25.4 s of snapshot, 24.8 s of
other threads' work, and 2.5 s of its own.

The table closes to 90.4% of the wall in processor time. The rest is host threads
nobody times, while the idle thread sleeps. From here on the budget is kept in
`frame-budget.md`, and this note keeps the reasoning behind it.

### Inside the renderer, and what the instruments cost

Both are recorded in `frame-budget.md`. In short:

- The decoder is at least three quarters of a display list's 30.3 ms. The Glide
  calls are 2 to 7 ms, and texture conversion is 0.13 ms, because the cache hits.
- `DKR_TRACE_CPU` costs less than the spread between two runs.
- A read of the 8254 clock costs about 5.8 µs. That is fine at a few thousand
  reads a second and not at seven hundred per display list. The finer zones
  E08-S03 will need call for `RDTSC`.

### Half the renderer was formatting log lines nobody printed

The previous section stopped on a clock too dear for fine timing: 5.8 µs to read
the 8254. `platform/win95/clock` now has a second, profiling-only clock,
`dkr_cycles_now()`, which is `RDTSC`. It is calibrated against the first one
twice at startup, over 20 and 40 ms, and refused if the two calibrations disagree
by more than 1%. On the test machine it calibrates at **399,993,987 Hz**, which is
86Box's emulated 400 MHz Deschutes. With it, the render zones cost about 0.9 ms a
display list instead of 8.

Two new cuts of the decoder, both on that clock:

- **by opcode**: the time between one command and the next is charged to the
  first, backend calls included;
- **inside `cmd_triangle`**, which turned out to be 25.1 ms of a 31 ms list: eight
  phases, and then four more inside the corner loop once that loop was 18 ms by
  itself.

Per display list, exclusive mode:

    cmd_triangle, 0x05                    25.1 ms
      the corner loop                     18.1 ms
        trace-args                        16.7 ms   <--
        fetch, (s,t) stats, NDC stats      2.0 ms
      draw_triangles                       2.1 ms
      clipping, projection, state, rest    4.9 ms
    everything else in the decoder         ~6 ms

`trace-args` is a single line: `trace(c, "vtx corner=%d raw s=%d ...", ...)`,
once per triangle corner. `trace()` returns at once when `c->trace` is null, but
the renderer set `context_.trace = trace_decoder` unconditionally. So every call
went through `vsprintf` into a 192-byte buffer and handed the line to a function
that prints the first 24 lines of the session and throws away every one after
that. Forty-five call sites in the decoder did the same.

The fix does not change a single line of output. The renderer installs `trace`
only while the 24 context lines are not yet spent. A new field,
`reject_trace`, keeps the rejections' route to the log. Rejections are
rate-limited per kind in `reject()`, so formatting them costs nothing. The log of
a 150 s run has 924 `[gfx][f3d]` lines before the fix and 924 after it.

                         before      after
    display list, CPU    31.2 ms    12.1 ms   exclusive mode
    display list, wall   38.4 ms    16.1 ms   normal mode
    frame                120.9 ms   98.3 ms   normal mode, trace off
    frame rate           8.27 fps   10.17 fps

**−18.7% on the frame from one line of the renderer's setup.** It is the largest
single gain this note has recorded, and it came from an instrument pointed one
level further down than the last one.
