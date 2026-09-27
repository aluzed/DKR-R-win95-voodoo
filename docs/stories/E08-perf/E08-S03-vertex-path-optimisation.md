# E08-S03 — Optimising the vertex path

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | IN PROGRESS |
| **Priority** | P1 |
| **Estimate** | L |
| **Depends on** | E08-S01, E04-S03, E04-S05 |
| **Blocks** | — |

## Context

Vertex transformation is work the N64 entrusted to the RSP and that here falls to the
host processor. It is regular computation, in volume, on contiguous data — exactly the
profile that lends itself to optimisation.

Three levers, in the usual order of profitability:

1. **Not computing.** Early rejection by bounding volume (E04-S05) is the cheapest
   gain: a rejected object is an object none of whose vertices is transformed. It is
   almost always the most profitable lever, and it is often neglected in favour of the
   next.
2. **Computing better.** The choice between x87 and fixed point, settled in E04-S03,
   may be re-examined in the light of the real profile. The Pentium II's x87 has a
   notable latency and a constrained register stack; MMX offers 16-bit integers in
   parallel, which suits fixed point.
3. **Computing less often.** A level's static geometry is transformed every frame
   whereas only the view matrix changes; there may be invariants to exploit, provided
   one checks that they really hold.

A point of vigilance: MMX shares its registers with the x87 stack (see E03-S01), and
mixing the two in the vertex pipeline imposes expensive transitions. If MMX is
retained, it must cover a whole block, not a few isolated operations.

## Objective

To bring the vertex path back within its budget allocation, measurement in hand.

## Scope

**In:** transformation, clipping, preparing the vertices for Glide.

**Out:** the recompiled code (E08-S02) and the Glide backend on the card's side.

## Work

1. Establish the vertex path's detailed profile from E08-S01: the share of
   transformation, of clipping, of rejection, of preparation.
2. Work on rejection first. Measure how many vertices are transformed for nothing —
   that is, belonging to geometry that ends up invisible. If that figure is high, all
   the rest of the ticket is secondary.
3. Optimise the transformation loop: data layout in memory, unrolling, prefetching if
   the architecture allows it. On a Pentium II, data layout often weighs more than the
   number of instructions.
4. Evaluate MMX for fixed point, covering a whole block of the pipeline to avoid
   transitions with x87. Measure before writing a lot of code.
5. Eliminate the copies. The vertex must be produced directly in Glide's format
   (E04-S01); check that no intermediate conversion remains, and that the vertex buffer
   is reused rather than reallocated.
6. Explore drawing through vertex arrays rather than triangle by triangle, if the
   retained version of Glide allows it: it reduces the number of calls, whose unit cost
   is not negligible.
7. Measure the gain at each step and enter it in the budget. An optimisation whose gain
   is not measured is a complication.
8. Check for visual regressions after each change, by image comparison (E09-S02). An
   optimisation of geometric computation that moves a position by a pixel must show.

## Measurements

Each change is measured on the target on its own: render zones
(`DKR_TRACE_RENDER_ZONES`) for the zone it touches, then interleaved runs in
normal mode with both opt-in options for the frame. Each is also checked
byte-identical on 21 captured scenes through `tools/render/replay`, image and
decoder counts, built for x86-64 and for i386 with x87 arithmetic.

| Change | Zone, per display list | Frame mean |
|---|---|---|
| Trivial accept written in line (25 September) | clip 1.20 to 1.06 ms | 37.3 to 36.5 ms |
| Clipping copies nothing it does not change (26 September) | clip 1.10 to 0.44 ms; `dkr_f3d_run` 10.52 to 9.83 ms | 37.2, 37.9 to 36.3, 36.3 ms; p99 89.8, 88.6 to 78.0, 80.8 ms |
| Projection cached per vertex, **not kept** (26 September) | project 1.22 to **1.43 ms** | 36.4, 37.5 against 37.0, 36.4 ms: no difference |
| Projection zeroes only what it does not write (27 September) | project 1.19 to 0.81 ms; `dkr_f3d_run` 9.94 to 9.57 ms | 36.4, 38.0 to 36.2, 36.4 ms; render mean 10.73, 10.66 to 10.29, 10.28 ms |
| Corners copied without s and t, unrolled (27 September) | fetch 1.15 to 1.00 ms; `dkr_f3d_run` 9.56 to 9.45 ms | 36.3, 36.5 against 36.4, 36.5 ms: no difference; render mean 10.30, 9.49 to 10.07, 9.26 ms |
| Combiner catalogue keys computed once (27 September) | state 0.71 to 0.57 ms; `dkr_f3d_run` 9.47 to 9.04 ms | 36.3, 36.4 to 36.3, 36.2 ms; render mean 10.07, 10.07 to 9.86, 9.86 ms |
| Even 16-bit reads in one load (27 September) | decoder without the backend 5.41 to 5.16 ms, on different list counts | 36.4, 36.2 to 36.2, 36.2 ms; render mean 9.85, 9.87 to 9.80, 9.79 ms |

The second change: the decoder asks `dkr_clip_trivially_inside` and
projects the triangle's own vertices, rather than having `dkr_clip_near` copy
all three. In the Sutherland-Hodgman path, a plane every vertex is inside is
skipped, and the two polygon buffers trade places instead of being copied
back. The sampler had put the copy loops at a third of `dkr_clip_near`'s
samples.

The fourth change: `dkr_clip_project` cleared the whole 84-byte vertex with
`memset`, which GCC inlines as a loop, then wrote 48 of those bytes again. It
now zeroes the nine fields it does not write. The render mean moves by about
0.4 ms a frame in both pairs of runs, and the median render by the same.

The fifth: a quarter of `cmd_triangle`'s samples were on one loop, the
40-byte structure copy that fetches each corner from the vertex cache. It now
copies the eight fields other than s and t, which are written just after. A
`memcpy` of those eight stays a loop on this target, so the copy is written as
assignments, which GCC unrolls. The frame does not move; the render mean falls
by 0.23 ms in both pairs of runs.

The sixth is outside the vertex path proper, but on the same thread and found
by the same sampler. Every state change looks its combiner up in the
twenty-nine-entry catalogue, and `dkr_cc_lookup` recomputed each entry's key on
every call: `dkr_rdp_combiner_key` alone was 2% of the graphics thread. The keys
are now computed on the first lookup, and the entry's index is taken from its
address rather than by a second search.

The seventh: `read_s16` had no fast path, where `read_u32` has had one for
long. A third of `dkr_f3d_run`'s samples were in the inlined vertex load, two
byte reads and a shift per coordinate. An even address on the interleaved
layout is now one native 16-bit read at `a ^ 2`, as N64Recomp's `MEM_HU` does
it. The gain is small, 0.06 ms of render a frame, but it is in the same
direction in both pairs and the code is no harder to read.

The projection cache was byte-identical on the 21 scenes, and slower. Each
vertex was projected once and its projection shared by the triangles using it,
with only s and t computed per corner. Copying the 84-byte projected vertex out
of the cache, and checking it was still valid, cost more than projecting again:
one division and a few multiplications. It is not in the tree. The diff is not
kept either: an idea measured slower is recorded here, not stored.

### How many vertices are transformed for nothing (27 September)

Work item 2. The decoder now follows each vertex slot from its load to its
retirement, when it is overwritten or the list ends, and counts two kinds of
waste: a vertex no triangle referenced, and a vertex whose every triangle was
culled or clipped away. `replay` prints them on a `vertices:` line. The counts
are deterministic, so the 21 captured scenes give them on the host:

| | Vertices | Share |
|---|---:|---:|
| Transformed | 26,919 | |
| Referenced by no triangle | 260 | 1.0% |
| Referenced, but no triangle drawn | 8,405 | 31.2% |
| Drawn | 18,254 | 67.8% |

A third of the transformations are wasted, and **that is not where the time
is**. The whole of `G_VTX` (0x04), fetch and transformation, is 0.71 ms of a
10.1 ms display list in the last render-zones run on the target, 2,400 lists.
Avoiding every wasted vertex would save at most a third of that, about 0.23 ms
a list, and that is a ceiling: back-face culling is decided per triangle
after projection, so a vertex whose triangles all face away cannot be known
to be wasted before it is transformed. Rejection is not the lever here, and
the rest of the ticket is not secondary to it. The triangle opcode is 7.9 ms of the list, and the Glide
calls are the largest share inside it.

The counters are skipped with the other statistics under `DKR_GFX_NO_STATS=1`.

### Drawing through vertex arrays: not worth an API change (27 September)

Work item 6. Glide 2.54, which ADR 0002 retains, has no `grDrawVertexArray`:
its only list entry points draw one polygon, a fan, which DKR's independent
triangles do not fit. Arrays would mean moving to Glide 3, which
`glide3x.dll` on the test machine would allow and the ADR rejected.

What it could win was measured first. A 60 s sampler capture in steady state
(`DKR_TRACE_SAMPLER=60`, `DKR_TRACE_SAMPLER_DELAY=50`, normal mode, both opt-in
options), with `GLIDE2X.DLL`'s export table read from the machine's disk and
given to `sampler_report.py --exports`. On the graphics thread, 10,631 samples:

| Where | Share |
|---|---:|
| `GLIDE2X.DLL`, internal code after `guTexSource`: triangle submission | 9.4% |
| `GLIDE2X.DLL`, `grCheckForRoom`: waiting for FIFO space | 3.2% |
| `GLIDE2X.DLL`, state calls (combine, depth, texture source) | about 2% |
| `cmd_triangle` | 16.7% |
| `dkr_clip_project` | 12.2% |
| `dkr_f3d_run` | 8.5% |
| `dkr_clip_near` | 5.9% |
| `dkr_transform_to_clip` | 4.5% |

The driver's per-triangle work is a tenth of the thread. A vertex array saves
the call and its argument checks, not the packet each vertex becomes on a
Voodoo 2, so it could recover only part of that tenth. The decoder's own
triangle path is more than three times as large, and that is where this
ticket's remaining work goes. These are samples of a thread whose instruction
pointer moved, not processor time, but the graphics thread rarely waits.

## Acceptance criteria

- [ ] The vertex path's detailed profile is established.
- [x] The number of vertices transformed needlessly is measured, and rejection worked on
      first.
- [ ] Every optimisation is measured separately.
- [ ] MMX is used only if the measurement justifies it, and on whole blocks.
- [ ] No copy and no reallocation per frame in the vertex path.
- [x] Drawing through vertex arrays is evaluated.
- [ ] The vertex path fits within its budget allocation.
- [ ] No visual regression after optimisation, verified by image comparison.

## Risks

Optimising geometric code easily introduces precision deviations. A change in the order
of floating-point operations, a different rounding in fixed point, and the geometry
starts to wobble. Checking by image comparison after each step is not an excessive
precaution: it is what allows a regression to be attributed to the optimisation that
caused it, rather than to the whole.

## References

- E04-S03 — transformation, floating-point / fixed-point choice
- E04-S05 — clipping and rejection
- E03-S01 — MMX and transitions with x87
- E08-S01 — profile and budget
