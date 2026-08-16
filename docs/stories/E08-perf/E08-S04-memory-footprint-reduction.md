# E08-S04 — Reducing the memory footprint

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | TODO |
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

## Acceptance criteria

- [ ] The RDRAM snapshot and the number of tasks in flight are reduced according to
      E00-S06.
- [ ] Every host-side cache has an explicit ceiling.
- [ ] Heap fragmentation is measured over a long session and does not grow without
      bound.
- [ ] No paging during play on the target configuration — verified by counting page
      faults, not by observation.
- [ ] The behaviour on 32 MB is assessed and documented.
- [ ] E00-S06's budget is updated with the real figures.
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
