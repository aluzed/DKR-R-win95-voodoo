# ADR 0003 - Memory budget on a 64 MB machine

- **Status**: accepted
- **Date**: 2026-08-12
- **Ticket**: [E00-S06](../stories/E00-scoping/E00-S06-adr-memory-budget.md)

## Context

The modern runtime spends memory without counting, because it has no reason to
count: it reserves 4 GiB of address space and commits 512 MiB of it. On the target
machine, every item becomes a trade-off — and Windows 95 pages onto a 1998 disk,
which makes any overrun catastrophic mid-race.

## What is measured

### Memory really available, with the 3dfx driver active

Recorded on the test machine by `tools/win95/probes/glide_memory.c`, which queries
`GlobalMemoryStatus` at four moments:

| Moment | Physical free | Cost |
|---|---:|---:|
| At rest, desktop loaded | 49,028 KiB | — |
| `glide2x.dll` loaded | 48,880 KiB | 148 KiB |
| Glide context open, 640×480, double buffer + Z | **48,156 KiB** | 724 KiB |
| Context closed | 48,292 KiB | (136 KiB not returned) |

Total physical: 65,012 KiB. **Windows 95 and its drivers consume 15,984 KiB at
rest**, that is 15.6 MiB.

A counter-intuitive lesson: **the 3dfx stack costs only 872 KiB of system RAM**.
The 640×480 16-bit buffers — front, back, depth, that is 1.76 MiB — live in the
card's memory, not in the machine's. The Voodoo therefore barely touches the
budget.

**Working ceiling retained: 47 MiB**, on the strength of the third row.

### The recompiled code's image

The ticket estimated "probably 10 to 40 MB". The measurement, made on the 37
32-bit objects produced by [E00-S03](../research/cpu-budget.md) — that is, the
whole of the generated game code:

| Section | Size |
|---|---:|
| `.text` | 3,896,223 B (3.72 MiB) |
| `.eh_frame` | 100,456 B |
| `.rodata` (all variants) | 37,085 B |
| **Total loadable** | **4,037,386 B (3.85 MiB)** |

The estimate was an order of magnitude high. It is **3.85 MiB**, and this item
stops being a worry.

### RDRAM actually used by DKR

`librecomp` declares 8 MiB to the game (`recomp.cpp:510`). The game does not want
that much. In the decomp (`src/memory.h:22-23`, `include/config.h:7`):

```c
#define RAM_END           0x80400000   /* 4 MiB */
#define EXPANSION_RAM_END 0x80800000   /* 8 MiB */
#define EXPANSION_PAK_SUPPORT 0        /* the game does not use the Expansion Pak */
```

`mempool_init_main` therefore takes `RAM_END`: **DKR's main pool stops at 4 MiB**.
A single address in the link map exceeds that bound, `assets_VRAM_END` at
`0x80b09df0` — but `asset_loading.c` transfers the assets from the ROM by DMA on
demand; it is a ROM segment address, not occupied RAM.

## Decision

### Budget per item

| Item | Today | Decision | Budget |
|---|---:|---|---:|
| Reserved space | 4 GiB (→ **0** in 32-bit) | reserve 8 MiB + guard | 8 MiB |
| Committed RDRAM | 512 MiB | **4 MiB**, the game's real bound | 4 MiB |
| RDRAM snapshot per task | 8 MiB × unbounded queue | **4 MiB × 1 in flight** | 4 MiB |
| Recompiled code image | — | measured | 3.85 MiB |
| ROM image | 12 MiB resident | **read on demand** | 0.06 MiB |
| RT64 load queue | 4 slots | disappears with RT64 | 0 |
| 3dfx stack | — | measured | 0.85 MiB |
| Runtime, CRT, C++ heap | — | **reserve** | 4 MiB |
| Host-side decoded textures | — | **reserve**, to be measured in E04-S07 | 8 MiB |
| **Total** | | | **32.8 MiB** |
| **Measured available** | | | **47.0 MiB** |
| **Headroom** | | | **14.2 MiB (30 %)** |

The headroom is deliberately generous. A budget that "fits" at 46 MiB out of 47 is
a false budget: the first unforeseen allocation makes it page, and a race that
pages is not playable.

### 1. Correct `librecomp`'s two constants

`librecomp/include/librecomp/addresses.hpp`:

```cpp
constexpr size_t mem_size        =  512ULL * 1024ULL * 1024ULL;
constexpr size_t allocation_size = 4096ULL * 1024ULL * 1024ULL;   // is 0 in 32-bit
```

These values become target-dependent: **`mem_size` = 4 MiB**, `allocation_size` =
8 MiB, which leaves a 4 MiB protected region above RDRAM to trap invalid accesses
— `librecomp`'s mechanism stays intact, and
[E00-S01](../research/win95-blockers.md) verified that it works at that size on
the real machine.

`osMemSize` stays declared at 8 MiB or moves to 4: **to be settled in E02-S04**,
after checking that nothing in DKR reads that value to size anything other than
the pool. It is the only point in this ADR that stays open, and it is deliberately
left open rather than settled without proof.

### 2. RDRAM snapshot: 4 MiB, a single task in flight

The `0007-snapshot-rdram-for-queued-graphics-tasks` patch copies `0x800000` bytes
— 8 MiB — per queued graphics task, into a
`moodycamel::BlockingConcurrentQueue` **which is not bounded**.

Two corrections, for two distinct reasons:

- **the size moves to 4 MiB**, because that is DKR's pool bound: copying beyond it
  copies memory the game never writes;
- **a single task in flight**, because the deep queue serves RT64's frame
  interpolation, which disappears with the "Accurate" profile
  ([E07-S01](../stories/E07-scope/E07-S01-accurate-profile-only.md)).

The second point is the more important, and it is not only a question of budget:
on the target, **rendering will be slower than the production of tasks**. An
unbounded queue in front of a consumer slower than the producer does not level off
— it grows until memory runs out. What is harmless on a modern machine becomes a
delayed crash here.

In time, the snapshot ought to disappear in favour of a copy of only the segment
the display list reads — but that requires knowing that segment, which belongs to
[E04-S02](../stories/E04-hle-f3ddkr/E04-S02-standalone-display-list-parser.md).
It is not this ADR's decision.

### 3. ROM: read on demand

`librecomp/src/pi.cpp:14` keeps the whole ROM in memory:

```cpp
static std::vector<uint8_t> rom;
```

and the PI DMA reads from it by `memcpy` (lines 70 and 80). **12 MiB resident for
two read sites.**

Decision: replace it with a file handle and a transfer buffer, with a 64 KiB
cache. Those two sites are the only ones to convert, and DKR is already written
for that model — it transfers its assets by asynchronous DMA rather than assuming
the ROM present. Implemented in
[E02-S04](../stories/E02-system/E02-S04-rom-access-pi-dma.md).

The target machine's disk is slow, and that is this decision's risk: if DMA on
demand causes stutter mid-race, the fallback is to load only the hot regions into
memory. To be measured in E02-S04, not assumed here.

### 4. A 32 MB fallback configuration

Windows 95 consumes 15.6 MiB at rest, measured. On a 32 MB machine there would
therefore remain on the order of **16 MiB** — against 32.8 MiB of budget.

**The 32 MB target is not met, and is not retained.** What could conceivably fit
there, by removing every texture reserve and the snapshot: RDRAM 4 + code 3.85 +
Glide 0.85 + runtime 4 ≈ 12.7 MiB, leaving 3 MiB for the decoded textures. That is
a budget with no headroom, on a machine that would page at the slightest
deviation.

**64 MB is therefore the project's hardware floor**, which confirms the chosen
hardware target
([E00-S05](../stories/E00-scoping/E00-S05-adr-hardware-target-glide.md), ADR 0002
still to be written) rather than relaxing it.

## What is not measured, and why

The ticket asked for the **memory peak of the current build in operation** to be
measured, broken down per item with less than 10 % unaccounted for. **That is not
done**, and it is not feasible today:

- no build of the target exists yet — that is
  [E01-S05](../stories/E01-build/E01-S05-compiling-the-recompiled-code.md);
- profiling the modern build would measure RT64, ImGui, SDL2 and the texture
  packs, that is to say precisely the items that disappear. The figure would be
  exact and unrelated to the question.

The budget above is therefore built **from the bottom up**, item by item, from
measured values where they existed (target availability, Glide's cost, code size,
DKR's pool bound) and from declared reserves where they did not (textures, runtime
heap).

**A consequence to honour**: as soon as E01-S05 produces an executable, the real
peak must be measured and set against this table. The two reserves — 4 MiB of
runtime and 8 MiB of textures — are the items to check first, since they are the
only ones resting on no measurement at all.

## Consequences

- **E02-S04** inherits two decisions: the ROM read on demand, and the arbitration
  on `osMemSize`.
- **E08-S04** inherits the snapshot: 4 MiB, one task in flight.
- **E04-S07** must measure the decoded textures' footprint against the 8 MiB
  reserve.
- **E01-S05** must measure the real peak and set it against this table.
- The hardware floor stays **64 MB**, confirmed by measurement and not relaxed.

## References

- `tools/win95/probes/glide_memory.c` — the measurement on the target
- [`docs/research/win95-blockers.md`](../research/win95-blockers.md) — truncation of `allocation_size`, `VirtualAlloc` ceilings
- [`docs/research/cpu-budget.md`](../research/cpu-budget.md) — the 37 objects measured
- `extern/n64-modern-runtime/librecomp/include/librecomp/addresses.hpp:10-12`
- `extern/n64-modern-runtime/librecomp/src/pi.cpp:14,70,80`
- `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`
- `../../Diddy-Kong-Racing/src/memory.h:22-23`, `include/config.h:7` — DKR's pool bounds
