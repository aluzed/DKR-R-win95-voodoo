# E08-S04 — Reducing the memory footprint

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | IN PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E00-S06, E08-S01, E07-S01 |
| **Blocks** | E09-S04 |

## Context

On a 64 MB machine, memory is not only a constraint of capacity: it is a constraint of
performance. As soon as the game exceeds physical memory, Windows 95 pages onto a 1998
disk, and the result is not a gradual degradation but a collapse.

E00-S06 established the budget and the reduction decisions. This ticket applies them and
verifies the result in real operation.

The most obvious item is the RDRAM snapshot: **8 MiB per pending graphics task**, sized
for a use — modern interpolation — that disappears with E07-S01. Without interpolation,
there is no longer any need to match two frames, and a single task in flight suffices.

## Objective

To bring the footprint back within E00-S06's budget, and to prove there is no paging
during play.

## Scope

**In:** applying E00-S06's decisions and verifying them.

**Out:** defining the budget itself (E00-S06).

## Work

1. Apply the decision on the RDRAM snapshot: size reduced to what DKR really uses, and
   the number of tasks in flight brought back to what the Accurate profile requires. The
   existing patch (`0007-snapshot-rdram-for-queued-graphics-tasks.patch`) is the entry
   point.
2. Apply the decision on ROM access (E02-S04).
3. Review the host-side caches: decoded textures (E04-S07), display lists, vertex
   buffers. Each must have an explicit ceiling rather than free growth.
4. Measure heap fragmentation over a long session. A game that allocates and frees for
   hours fragments, and under Windows 95 the heap does not compact. If the fragmentation
   grows, prefer preallocated buffers to dynamic allocations in the hot paths.
5. Check the absence of paging in operation: count the page faults over a real play
   session. That is the criterion that really counts — the sum of the items may fit on
   paper and the system page all the same.
6. Check the behaviour on a 32 MB machine, the fallback configuration assessed by
   E00-S06: what degrades, and whether the game stays playable.
7. Update E00-S06's budget with the real figures after reduction.

## Measurements

### Paging, counted (29 September 2026)

`DKR_TRACE_PAGING=1` starts a meter that reads, every five seconds, the VMM's
own statistics -- the ones System Monitor plots, under `HKEY_DYN_DATA`,
`PerfStats\StartStat` to start each and `PerfStats\StatData` to read it --
and `GlobalMemoryStatus`. Windows 95 has no per-process counters, so these are
the machine's, which is what paging is about anyway. Every name asked for is
known to the system: `cPageFaults`, `cPageIns`, `cPageOuts`, `cDiscards` are
counts since the counter started, `cpgFree`, `cpgSwapFile`, `cpgDiskcache`,
`cpgLocked` are bytes.

One 200 s run in normal mode with both opt-in options, 64 MiB machine, at the
build after `543e574`:

| t (from the meter's start) | page-outs | page-ins | free physical | load |
|---|---:|---:|---:|---:|
| 5 s | 0 | 9,886 | 34,976 KiB | 31% |
| 22 s, loading | 0 | 17,462 | 29,484 KiB | 52% |
| 42 s | 0 | 17,863 | 24,296 KiB | 54% |
| 92 s, attract mode | 0 | 17,961 | 23,920 KiB | 54% |
| 112 s, the race | 0 | 19,718 | 23,176 KiB | 54% |

- **Nothing is ever written to the swap file**: `cPageOuts` stays at zero for
  the whole run. The machine is not short of memory; a quarter of it is free
  at every sample after loading.
- **The page-ins are not swap-ins.** With no page ever written out, none can
  be read back. They are file pages brought in on demand -- the 9.5 MB
  executable's code the first time it runs, and the ROM's file reads. In the
  attract mode they come at about two a second; the race's start brings 1,800
  in fifteen seconds, the new scene's code and data.

That answers work item 5 on this configuration: no paging during play, by
count. It does not yet say anything about a long session.

### On 32 MiB (29 September 2026)

The same build, the test machine's `mem_size` set to 32768 for one run and put
back after. Windows 95 reports 32,244 KiB. From the meter:

| t | page-outs | page-ins | free physical | swap file in use |
|---|---:|---:|---:|---:|
| 32 s, loading | 8 | 17,827 | 40 KiB | 35.3 MiB |
| 62 s | 133 | 29,735 | 0 | 35.3 MiB |
| 92 s, attract mode | 141 | 29,742 | 4 KiB | 35.3 MiB |
| 107 s, the race | 221 | 29,938 | 0 | 35.3 MiB |

- **It pages, and it pages while loading.** Free memory is gone from the
  first samples, the swap file holds 35 MiB, and the loading brings in about
  12,000 pages in thirty seconds, some of them written out first.
- **Then it settles.** Between 62 and 92 s, in the attract mode, seven pages
  come in and eight go out. The working set of a scene fits; what does not
  is swapped out once and left there.
- **Scene changes page again**: the race's start brings in 196 pages and
  writes out 80.
- **The frame does not suffer where it is measured.** Normal mode, the same
  timing export: 40 to 94 s at 34.8 ms a frame, as on 64 MiB (34.9 ms), and
  the race at 41.2 ms. The cost is in the loading and at scene changes, not
  in play.

So 32 MiB is playable, with longer loads and the risk of a stall where a scene
change pages. A long session there has not been measured.


### What the RDRAM snapshot costs, measured (29 September 2026)

Every run above used `DKR_RDRAM_SNAPSHOT=none`, the opt-in mode that draws
from live RDRAM and has no snapshot. The default mode, the same meter:

| | free physical, attract mode | swap file in use | page-outs |
|---|---:|---:|---:|
| `DKR_RDRAM_SNAPSHOT=none` | 23,920 KiB | 6.5 MiB | 0 |
| default: a pool of two snapshots | **8,832 KiB** | 28.5 MiB | 0 |

The snapshot costs **about 15 MiB** of physical memory. The copy is 4 MiB
(`[snap][size] 4096 KB copied of 8192`), but each of the pool's two buffers is
allocated at 8 MiB and zero-filled on creation, so all sixteen megabytes are
touched. On 64 MiB it still does not page; it leaves a third of the margin.

Two ways to meet the first criterion, both measured enough to choose from:

- **Make `none` the default.** Nothing to allocate, and the frame is faster
  too (`frame-budget.md`). It was left opt-in until it has been played, which
  is a decision for whoever plays it.
- **Keep the snapshot, at the size E00-S06 decided.** Allocating each buffer
  at the 4 MiB copied rather than 8 would save 8 MiB, and one buffer instead of
  two another 4; the decoder's RDRAM bound would then have to be the copied
  size, not eight megabytes, or it would read past the buffer. The scratch
  window at 0x7FE000 that a comment in `glide_renderer.cpp` placed in the
  snapshot belongs to the RT64 bridge (`kScratchVertexAddress` in
  `f3ddkr_rt64.cpp`); on this path `dkr_f3d_init` takes the RDRAM `const` and
  `f3ddkr.c` only reads it, so nothing writes above the four megabytes copied.
  The comment is corrected. The change spans the runtime's patch set and the
  renderer, and is not made here.

### The snapshot's pages, committed by touch (29 September 2026)

Patch 0055: on the target each snapshot buffer comes from `VirtualAlloc`
instead of `new uint8_t[8 MiB]()`. Its pages are zero by the system's
guarantee, as the fill made them, and become resident only when touched, so
the upper four megabytes, which DKR never writes and its lists do not read,
are never brought in. The same default-mode run, with and without it:

| | free physical, attract mode | frame over 40-94 s | race, 95-101 s |
|---|---:|---:|---:|
| before | 8,424 to 8,500 KiB | 64.35 ms, 839 frames | 71.8 ms |
| patch 0055 | **15,520 to 16,028 KiB** | 64.32 ms, 839 frames | 71.1 ms |

**7.5 MiB back, and the frame unchanged.** The first criterion is not met to
the letter -- E00-S06 decided one 4 MiB snapshot, and this is two buffers of 4
resident megabytes each -- but half of the gap is closed with no change of
behaviour.

The same table says something larger: **the default mode runs at 64 ms a
frame, `DKR_RDRAM_SNAPSHOT=none` at 35.** Copying four megabytes per display
list on one processor costs more than a frame. Making `none` the default would
give back both the 16 MiB and half the frame time; that it has not yet been
played for long is the only reason it is still opt-in.

**In a played race too** (`frame-budget.md`, "A race driven by hand"): 35.2 ms
a frame with `none`, 88% of frames on two retraces, against 60.2 ms in the
default mode, where two frames in three take four retraces.

### A longer session (29 September 2026)

The same meter over 1,260 s of wall time, which the emulator's 57% speed makes
9.4 minutes of the game: attract mode, its races and menus, looping. After
loading, the address space left to the process reads 1,992,320 KiB at every
sample to the end: nothing grows it. Free physical memory drifts from 23,920
to 22,836 KiB over eight minutes, a megabyte, and page-outs stay at zero.
Nine minutes are not the hours the criterion asks for, so it stays open; what
there is shows no growth.

A second run, 3,000 s of wall time, is **21.4 minutes** of the game. The
address space left reads 1,992,320 KiB from 3 minutes to the end, to the
kilobyte; free physical memory stays between 22,820 and 22,964 KiB; no page is
written out. Still short of hours, and still flat.

A third, 6,600 s of wall time, is **46 minutes** of the game. The address
space left is 1,992,320 KiB from 3 minutes to the end, and no page is written
out. Free physical memory holds at 22,884 to 22,904 KiB for forty minutes, then
drops once, by 2.8 MiB, around the 42nd minute, and stays at 20,084 KiB to the
end. With the process's address space unchanged, that megabyte-scale step is
not the game's heap growing; what took it -- the disk cache or another part of
the system -- is not established. The criterion stays open until a session of
hours has been run.

### Every host-side cache has a fixed ceiling (29 September 2026)

Work item 3, by reading the Windows 95 path's code. Nothing in the renderer,
the Glide backend, the audio mixer or the platform layer grows a container:
there is no `malloc`, `new` or `push_back` in any of them. Each cache is a
fixed array:

| Cache | Ceiling | Where |
|---|---|---|
| Texture descriptors | 512 (`GLIDE_MAX_TEXTURES`), least recently used reclaimed | `glide_backend.c` |
| Textures resident on the card | the card's memory, through a buddy allocator that evicts; 512 slots per unit (`DKR_TMU_MAX_RESIDENT`) | `tmu.c` |
| Residency memos | 1,024 slots each | `tmu.c`, `glide_backend.c` |
| Distinct texture keys counted per list | 1,024 (`DKR_DISTINCT_KEY_MAX`), overflow counted | `f3ddkr.h` |
| Transformed vertices | 32 slots, as the microcode | `f3ddkr.c` |
| Audio output | a ring of 16 buffers of 4,096 frames; a full ring drops, and says so | `audio_out.c` |
| RDRAM snapshot, when used | a pool of two buffers (`kSnapshotPoolSlots`) | ultramodern `events.cpp` |

Display lists are not cached: each is decoded from RDRAM as it arrives. The
runtime's lock-free queues (moodycamel) allocate blocks as producers need
them; they carry messages, not data that accumulates.

## Acceptance criteria

- [ ] The RDRAM snapshot and the number of tasks in flight are reduced according to
      E00-S06.
- [x] Every host-side cache has an explicit ceiling.
- [ ] Heap fragmentation is measured over a long session and does not grow without
      bound.
- [x] No paging during play on the target configuration — verified by counting page
      faults, not by observation.
- [x] The behaviour on 32 MB is assessed and documented.
- [x] E00-S06's budget is updated with the real figures.
- [ ] No regression in the game's behaviour.

## Risks

Reducing the RDRAM snapshot touches a mechanism which
`docs/RENDER_SNAPSHOT_ARCHITECTURE.md` explains protects the simulation's memory: the
decoder must never write back into live RDRAM. That property must survive the
reduction. Reducing it is legitimate, removing it is not.

## References

- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md`
- `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`
- E00-S06 — budget and decisions
- E07-S01 — removing the interpolation, which makes the reduction possible
