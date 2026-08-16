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

- [ ] The number of textures, the volume and the reuse pattern are measured per level,
      on every level — **blocked by the absent ROM**. The measurement that could be
      made bears on the hardware and not on the game, and it sufficed to decide the
      design: see `docs/research/win95-tmu.md`.
- [x] The allocator respects the TMU's alignment and granularity — 8 bytes, surveyed
      and not assumed: fourteen sizes out of fifteen cost exactly the computation, and
      the fifteenth (a 1×1 texture, 8 bytes for 2 useful) gives the granularity. The
      bounds come from `grTexMinAddress` and `grTexMaxAddress`; the first returns
      zero, which forbids making it a failure sentinel.
- [ ] No texture download mid-race on the levels that fit in memory — **blocked by the
      absent ROM**. The counter that will establish it exists and is reset every
      frame; the cumulative counters survive a level change, precisely so that this
      question can be answered at the end of a session.
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

## Risks

A texture download mid-race is a visible jolt: the PCI bus of 1998 is not fast, and
transferring a 64 KB texture during a 33 ms frame is felt. If the measurement shows
that a level does not fit, it is better to reduce its textures' resolution at load
time than to endure transfers during play.

## References

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — a peak of 1.20 MB
- E00-S05 — memory available per TMU on the target
- E04-S07 — format and size of the decoded textures
