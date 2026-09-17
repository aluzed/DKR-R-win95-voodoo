# E05-S02 — TMU texture memory allocator

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E05-S01, E04-S07 |
| **Blocks** | E05-S04, E05-S08, E08-S01 |

## Context

Glide has no texture manager. It exposes the TMU's memory as a raw address space: the
application chooses an address, downloads a texture there through
`grTexDownloadMipMap`, and binds that address when drawing. All the rest — allocation,
fragmentation, eviction — is ours to write.

It is the item where the budget is tightest. A Voodoo 2 offers 2 or 4 MB per TMU
depending on the model, and the peak working set measured on the native port's side is
**1.20 MB per level** — comfortable within 2 MB, provided E04-S07's decoding has not
expanded the indexed textures, in which case the figure may quadruple and the budget
blow.

Two hardware constraints weigh on the allocator:

- the alignment and granularity the TMU imposes, which make fragmentation more
  expensive than a simple allocator would suggest;
- with two TMUs (E05-S04), a texture intended for multitexturing must be present on
  the right unit, which makes two spaces to manage, not one.

## Objective

To deliver a texture-memory allocator that holds a whole level with no download
mid-race.

## Scope

**In:** allocation, download, eviction, measurement, and the two TMUs.

**Out:** decoding the textures (E04-S07) and choosing the combiners (E05-S03).

## Work

1. Measure the real behaviour before designing: over a complete level, the number of
   distinct textures, their cumulative size, and the pattern of reuse from one frame
   to the next. It is that pattern which decides the policy, not allocation theory.
2. Implement the allocator with the TMU's granularity and alignment. If the game's
   textures fall into a small number of sizes, an allocator by size classes will
   eliminate fragmentation cheaply — to be checked against step 1's figures.
3. Implement the download through `grTexDownloadMipMap` and the tracking of what is
   resident.
4. Implement eviction. Least recently used is the reasonable starting point, but step
   1's measurement may show that a full preload at level load suffices — in which case
   there is no eviction at all mid-race, which is by far the best result.
5. Manage the two TMUs as two distinct spaces, with the placement policy E05-S04
   imposes.
6. Instrument it: occupancy, cache hit rate, number and volume of downloads per frame.
   Those counters must be readable in game (E08-S01), because a texture cache defect
   is diagnosed by playing.
7. Deal with saturation: if a level does not fit, decide — reducing the textures'
   resolution, or accepting downloads during play — and document it rather than
   letting the allocator fail.
8. Check on all the game's levels, not on a sample. It is an automatable measurement:
   load each level and record the peak.

## Acceptance criteria

- [~] The number of textures, the volume and the reuse pattern are measured **for a
      race**, 17 September 2026 — the stated blocker, an absent ROM, has not been
      true since 15 August, and a race is now reachable by a reproducible route. Not
      yet on every level. See the residency measurement below.
- [x] The allocator respects the TMU's alignment and granularity — 8 bytes, surveyed
      and not assumed: fourteen sizes out of fifteen cost exactly the computation, and
      the fifteenth (a 1×1 texture, 8 bytes for 2 useful) gives the granularity. The
      bounds come from `grTexMinAddress` and `grTexMaxAddress`; the first returns
      zero, which forbids making it a failure sentinel.
- [ ] No texture download mid-race on the levels that fit in memory — **measured,
      and the answer is no**: 1,552 downloads during a race, on a level whose peak
      occupancy is 1,165 K of the TMU's 2,048 K. The criterion stays open because it
      is not met, not because it cannot be tested. See below.
- [~] The eviction policy is least recently used, with protection of the textures the
      current frame uses. It is **not** justified by measuring the game, which
      presupposes the ROM; it is justified by a property of the hardware: every
      texture size being a power of two, a buddy allocator produces no external
      fragmentation, and eviction therefore never has to fight crumbling. The case
      that counts is exercised: a frame asking for more than the TMU holds saturates
      cleanly rather than evicting what it has just downloaded.
- [~] The two TMUs are initialised separately, each with its own surveyed bounds —
      measurement: both expose the same space. **Only TMU 0 is used**: the placement
      policy depends on multitexturing, which is E05-S04.
- [x] The counters are kept and formatted in one readable line — occupancy, hit rate,
      total and per-frame downloads, evictions, failures. They are exposed by
      `dkr_glide_backend_tmu` for E08-S01's display. A texture cache defect is felt on
      the controller rather than read in a log.
- [ ] The levels that do not fit are identified, and their handling is documented —
      **blocked by the absent ROM**. The allocator returns `DKR_TMU_NONE` and counts
      the failure rather than degrading silently, which is the condition for the
      question to be answerable at all.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## The residency cache reports no hit at all — 17 September 2026

The first measurement of this allocator against a race, taken from the runtime log
of a session driven to `gGameMode=0 (INGAME)`:

    tmu0: hits=0/1552  downloads=1552  bytes=3090720  evict=1040  fail=0  peak=1165K
    tmu1: hits=0/2     downloads=2     bytes=1024     evict=0     fail=0  peak=1K
    refusal-detail: aspect=0 size=0 slots=0 tmu-memory=0 reclaimed=1042

**Zero hits in one thousand five hundred and fifty-two acquisitions**, and the same
shape in a menu: `hits=0/377 downloads=377`. Three megabytes crossed the bus for
textures the card had already been given.

### What this rules out, and what it does not

*It is not a dead counter.* `dkr_tmu_acquire` increments `hits` when
`find_resident` succeeds, and the upload path reaches it even for a texture the slot
table already knows — deliberately, so that the LRU timestamp advances and so that
acquire is what tells a hit from a miss. The comment at that call says as much.

*It is not an unstable key.* The key is
`timg_address << 24 ^ fmt << 20 ^ siz << 18 ^ width << 9 ^ height` — the texture
image's address and its shape, stable from one frame to the next.

*It is not capacity, on the face of it.* Peak occupancy is **57 %** of the TMU and
`tmu-memory` refusals are **zero**. Yet 1,040 evictions happened.

*It is not the slot table.* `slots=0` refusals, and the table finds the key: the
search at `glide_backend.c` matches on `g_tex[i].key == desc->key` before anything
else.

### The question this leaves, stated precisely

**Why does `dkr_tmu_alloc` fail often enough to force 1,040 evictions when the unit
is 57 % full?** Fragmentation is the obvious candidate and it is a candidate, not a
finding: nothing here measures the free list's shape. An instrument that reported
the largest free block beside the total free would settle it in one run.

It matters beyond this ticket. E00-S03's verdict of 17 September rests on a frame
that is 5.1× over budget with the renderer at 13.6 % of it; three megabytes of
redundant PCI traffic per session is inside that 13.6 %, and it is the kind of cost
that a raised hardware floor does not remove.

## Risks

A texture download mid-race is a visible jolt: the PCI bus of 1998 is not fast, and
transferring a 64 KB texture during a 33 ms frame is felt. If the measurement shows
that a level does not fit, it is better to reduce its textures' resolution at load
time than to endure transfers during play.

## References

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — a peak of 1.20 MB
- E00-S05 — memory available per TMU on the target
- E04-S07 — format and size of the decoded textures
