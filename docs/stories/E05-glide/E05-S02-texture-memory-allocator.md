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

### Fragmentation was the candidate, and the instrument refutes it

`dkr_tmu_free_shape` walks the buddy tree and reports what a total cannot. Measured
the same day, in the menus:

    tmu0: hits=0/560 downloads=560 bytes=1215760 evict=48 fail=0 peak=1136K
    tmu0: free=933K largest=512K blocks=29

**933 K free and a whole 512 K block inside it.** That is not a shattered heap: an
ordinary texture here is a few kilobytes and finds room at once. So fragmentation
*at the scale of the textures this game uses* is refuted, and the paragraph below
that named it as the candidate was wrong to expect it.

What the numbers do leave is sharper than what they took away. `fail=0` means every
eviction eventually succeeded, so the 48 evictions were driven by allocations the
tree could not serve whole — and with 1,136 K committed of 2,048 K, no 1,024 K block
can exist, because a buddy block of that order needs both halves of a megabyte-
aligned region free. `round_up_pow2` turns anything above 512 K into a 1,024 K
request. That is a hypothesis with a shape, and it is one line of instrument away
from being measured: the requested `want` beside the granted address.

### The keys repeat, so the zero hits are a defect

The last cheap explanation was that there might be nothing to serve. The set that
would have said so was capped at 64 and reported 82 overflows, which made its
"64 distinct keys" a floor wearing the look of a total. Raised to 1024 and given a
repeat counter — the linear scan that computes it was already running, only the
counter was missing — it answers on the captures already in hand, with no machine
needed:

    capture              distinct   repeats   overflow
    CAP2600, a race          128       153          0
    CAP0420, a menu           28        33          0

**Two hundred and eighty-one asks for a hundred and twenty-eight textures**, and
`overflow=0` means 128 is a true total this time rather than a floor. More than half
of what the list asks for is something it has asked for before.

So the residency cache has plenty to serve and serves none of it. Every cheap
explanation is now spent — not memory, not fragmentation, not the slot table, not a
dead counter, not an unstable key, and not an absence of repeats — which makes
`hits=0` a defect in the residency path rather than a property of the workload.

### Both counters on one run, 18 September 2026 — and it is worse than suspected

The caveat above was that `hits=0` and the repeat count came from different runs.
They no longer do. One report, one session, one frame window:

    conversions: texels=0 distinct-keys=88 overflow=0 repeats=64
    tmu0: hits=0/210 downloads=210 bytes=506112 evict=0 fail=0 peak=494K
    tmu0: free=1553K largest=1024K blocks=6

**Zero evictions.** Nothing was ever thrown out, so everything acquired is still
resident. And 210 acquisitions against 88 distinct keys in the same report: at least
**122 acquisitions were for a key that was still resident**, and not one of them
hit.

Memory is not even under pressure — 494 K used of 2,048 K, 1,553 K free, and a whole
1,024 K block inside it. The allocator never had to refuse anything and never did.

So the residency lookup does not work. Not "is defeated by pressure", not "thrashes
under load": `find_resident` fails to match a key that is present and was never
evicted. Everything else in this path — the allocator, the buddy tree, the slot
table, the eviction policy — is exonerated by the same six numbers.

**What to look at first**, in the order the evidence suggests: `find_resident`'s
comparison and the `live` flag it tests, then whether `dkr_tmu_acquire` is reached
with the same key the slot table matched on, which is the one link this report has
not instrumented.

### The question this left, and which the instrument has now narrowed

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
