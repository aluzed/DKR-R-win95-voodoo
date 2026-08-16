# E00-S06 — ADR: memory budget on a 64 MB machine

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E00-S01 |
| **Blocks** | E01-S05, E02-S04, E08-S04 |

## State as of 2026-08-12 — the ADR is written

[`docs/adr/0003-memory-budget.md`](../../adr/0003-memory-budget.md).

| Item | Decision | Budget |
|---|---|---:|
| Committed RDRAM | **4 MiB** — DKR's pool's real bound (`RAM_END`, `EXPANSION_PAK_SUPPORT 0`) | 4 MiB |
| Reserved space | 8 MiB + guard, instead of 4 GiB | 8 MiB |
| RDRAM snapshot | **4 MiB, a single task in flight** | 4 MiB |
| Recompiled code | **measured**: 3.85 MiB, against 10-40 MiB estimated | 3.85 MiB |
| ROM | **read on demand** instead of 12 MiB resident | 0.06 MiB |
| 3dfx stack | **measured**: 872 KiB only | 0.85 MiB |
| Runtime + texture reserves | declared, to be verified | 12 MiB |
| **Total / measured available** | | **32.8 / 47.0 MiB** |

Two measurements carried the decisions:

- **the memory available with the 3dfx driver active is 47.0 MiB**, recorded on the
  machine by `tools/win95/probes/glide_memory.c`. Windows 95 consumes 15.6 of it;
  the Glide stack, only 872 KiB — the 640×480 buffers live in the card's memory, not
  in the machine's;
- **the recompiled code is 3.85 MiB**, measured on E00-S03's 37 objects. The ticket
  estimated it between 10 and 40 MB.

A discovery along the way: the snapshot queue is an **unbounded**
`BlockingConcurrentQueue`. On a target where the rendering is slower than the
production of tasks, it grows until memory runs out — a delayed crash, harmless on a
modern machine.

**The 32 MB target is ruled out**: only ~16 MiB would remain there, with no margin.
64 MB is confirmed as the hardware floor.

**Not done, and accepted**: the current build's memory peak is not measured — no
build of the target exists (E01-S05), and profiling the modern build would measure
precisely the items that disappear. The budget is built from the bottom up. The
corresponding acceptance criterion stays open until E01-S05.

## Earlier state — the measured ceilings

[E00-S01](E00-S01-inventory-of-incompatible-dependencies.md) measured on the target
machine what the source code could not say:

| Quantity | Measurement |
|---|---:|
| Physical / available RAM | 63 MiB / 47 MiB |
| Virtual address space | 2,044 MiB |
| Allocation granularity | 65,536 |
| **Maximum reservation** | **1,024 MiB** |
| **Maximum read-write commit** | **256 MiB** |
| `librecomp`'s reserve+commit+protect scheme at 8 MiB | **OK** |

And a defect to correct, in `librecomp/include/librecomp/addresses.hpp`:

- `allocation_size = 4096ULL * 1024ULL * 1024ULL` **is 0** once truncated into a
  32-bit `size_t`. GCC flags it only as a warning; the game compiles, links, then
  dies on "Failed to allocate memory".
- `mem_size = 512 MiB` **would fail too** even without the truncation: the measured
  commit ceiling is 256 MiB.

`librecomp`'s mechanism is not at fault — it works at 8 MiB, that is exactly the
RDRAM of an N64 with the Expansion Pak. The ADR therefore bears on the choice of the
two constants according to the target, not on a change of approach.

## Context

The modern runtime spends memory without counting, because it has no reason to
count. Under Windows 95 with 64 MB, every item becomes a trade-off.

The known items:

| Item | Size | Origin |
|---|---|---|
| Emulated RDRAM | 4 MB (8 MB with the Expansion Pak) | `librecomp` |
| RDRAM snapshot per graphics task | **8 MB per queued task** | `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch` |
| RT64 workload queue | 4 slots | RT64 — disappears with RT64 |
| Recompiled code image | to be measured, probably 10 to 40 MB | E01-S05 |
| Host-side decoded textures | to be measured | E04-S07 |
| ROM image | 12 MB if loaded entirely | `librecomp` |

Two items leap out. The 8 MB snapshot per queued graphics task is calibrated for a
machine where memory is free; DKR is a 4 MB RDRAM game, and the snapshot can
presumably follow. And loading the ROM's 12 MB into memory is a luxury when
`librecomp` could read it piece by piece.

## Objective

To write `docs/adr/0003-memory-budget.md`: the budget per item, the total, and the
remaining margin on a 64 MB machine — then the reduction decisions that follow from
it.

## Scope

**In:** the quantified inventory and the reduction decisions.

**Out:** their implementation (E08-S04 for the snapshot, E02-S04 for the ROM).

## Work

1. Measure the current build's real footprint in operation: heap peak, committed
   memory peak, size of the binary's sections. An allocation profiler on the modern
   host suffices — the sizes do not depend on the target OS.
2. Break the peak down per item according to the table above, leaving no unattributed
   remainder above 10 %.
3. Decide the RDRAM snapshot's size. Check the RDRAM size DKR actually uses — the
   game does not require the Expansion Pak — and whether the snapshot can come down
   to 4 MB, or even be replaced by a copy of only the segment the display list reads.
4. Decide the number of graphics tasks simultaneously in flight. On this target, a
   single one in flight is probably the right compromise: the deep queue serves
   modern interpolation, which disappears with the "Accurate" profile (E07-S01).
5. Decide the ROM's access mode: a complete image in memory, or reading piece by
   piece on demand (E02-S04).
6. Set an overall ceiling and compare it against what is really available under
   Windows 95 with 64 MB, once the OS and the 3dfx driver are loaded. Measure that
   availability in the test machine rather than estimating it.
7. Write the ADR with the budget per item, the total, the margin, and the decisions.

## Acceptance criteria

- [ ] The current build's memory peak is measured and broken down per item, with the
      unattributed remainder under 10 %.
- [ ] The memory really available under Win95 / 64 MB, with the 3dfx driver loaded,
      is measured and not estimated.
- [ ] `docs/adr/0003-memory-budget.md` fixes a ceiling per item and an overall
      ceiling, with the remaining margin.
- [ ] The RDRAM snapshot's size and the number of tasks in flight are settled and
      justified.
- [ ] The ROM's access mode is settled.
- [ ] A 32 MB fallback configuration is assessed: what falls, and whether the game
      stays playable.

## Risks

Windows 95 pages to disk, and a 1998 disk makes paging catastrophic mid-race. The
budget must fit in physical memory with margin, not merely in virtual memory: a
budget that "fits" at 63 MB out of 64 is a false budget.

## References

- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — the 8 MiB snapshot and its role
- `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`
- `runtime-recomp/src/game/renderer_snapshot.hpp`
