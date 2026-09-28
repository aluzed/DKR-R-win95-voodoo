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

**The i386 build has to use the target's language mode, not only its FPU.**
`cmake/win95-target.cmake` compiles with `-std=c17`, where GCC rounds excess
x87 precision on every assignment. A host build with `gcc -m32 -mfpmath=387`
and no `-std` is in GNU mode, where a value may stay in an 80-bit register:
there `dkr_clip_project` kept `1/w` unrounded, while the target's code stores
and reloads it as a float. Checked against that build, a change can look like
a regression that the target would not see, or the reverse. The options that
match the target:

    gcc -m32 -std=c17 -D_DEFAULT_SOURCE -march=pentium2 -mtune=pentium3 \
        -mfpmath=387 -mno-sse -mno-sse2

Every change of this ticket, from `5a49753` to `de8e0a1`, was re-checked that
way on 28 September: byte-identical on the 21 scenes.

| Change | Zone, per display list | Frame mean |
|---|---|---|
| Trivial accept written in line (25 September) | clip 1.20 to 1.06 ms | 37.3 to 36.5 ms |
| Clipping copies nothing it does not change (26 September) | clip 1.10 to 0.44 ms; `dkr_f3d_run` 10.52 to 9.83 ms | 37.2, 37.9 to 36.3, 36.3 ms; p99 89.8, 88.6 to 78.0, 80.8 ms |
| Projection cached per vertex, **not kept** (26 September) | project 1.22 to **1.43 ms** | 36.4, 37.5 against 37.0, 36.4 ms: no difference |
| Projection zeroes only what it does not write (27 September) | project 1.19 to 0.81 ms; `dkr_f3d_run` 9.94 to 9.57 ms | 36.4, 38.0 to 36.2, 36.4 ms; render mean 10.73, 10.66 to 10.29, 10.28 ms |
| Corners copied without s and t, unrolled (27 September) | fetch 1.15 to 1.00 ms; `dkr_f3d_run` 9.56 to 9.45 ms | 36.3, 36.5 against 36.4, 36.5 ms: no difference; render mean 10.30, 9.49 to 10.07, 9.26 ms |
| Combiner catalogue keys computed once (27 September) | state 0.71 to 0.57 ms; `dkr_f3d_run` 9.47 to 9.04 ms | 36.3, 36.4 to 36.3, 36.2 ms; render mean 10.07, 10.07 to 9.86, 9.86 ms |
| Even 16-bit reads in one load (27 September) | decoder without the backend 5.41 to 5.16 ms, on different list counts | 36.4, 36.2 to 36.2, 36.2 ms; render mean 9.85, 9.87 to 9.80, 9.79 ms |
| Clipping reads the triangle in place, copies unrolled (28 September) | clip 0.44 to 0.41 ms | 36.2, 36.2 to 36.1, 36.2 ms; render mean 9.83, 9.80 to 9.70, 9.68 ms |
| Trace calls skipped with their arguments (28 September) | decoder without the backend 5.05 to 4.89 ms, on 2,340 and 2,280 lists | 36.2, 36.4 against 36.5, 36.1 ms; render mean 9.66, 8.88 against 8.73, 9.50 ms: inconclusive |
| Texture memos not emptied on upload (28 September) | lookup 2.98 to 2.27 us a call, 0.26 to 0.20 ms; residency scans 50,440 to 29,968 | 36.4, 36.1 against 36.3, 36.2 ms; render mean 8.65, 9.46 against 9.40, 9.41 ms: below the spread |
| Texture memos of 1,024 slots (28 September) | lookup 2.29 to 1.14 us a call, 0.20 to 0.10 ms; scans 29,011 to 9,930 | not run: below the spread |
| Second-pass decision asked once per draw (28 September) | draw 4.73 to 4.62 us a call, 3.12 to 3.05 ms; `dkr_f3d_run` 8.76 to 8.67 ms, on 2,400 lists each | not run: below the spread |
| Second-unit coordinates only for chained recipes (28 September) | decoder 5.05 to 4.99 ms, on 2,460 and 2,400 lists | not run: below the spread |
| Glide state written part by part, only what changed (28 September) | state 4.73 to 3.40 us a call; draw 4.61 to 4.23 us; `dkr_f3d_run` 8.66 to 8.28 ms, on 2,400 lists each | 36.3, 36.2 to 36.0, 36.0 ms; render mean 9.19, 9.26 to 8.78, 8.77 ms; render p99 22.2, 22.5 to 20.6, 20.5 ms |
| Catalogue entry taken without calls into `combiner.c`, **not kept** (28 September) | draw 4.53 to 4.65 us a call | render mean 8.71, 8.74 against 9.40, 9.43 ms: no gain |
| `1/w` computed once per vertex at load, **not kept** (28 September) | project 0.82 to 0.70 ms, decoder unchanged, on 2,400 lists each | render mean 9.42 against 9.87 ms: no gain |
| Corners projected from the vertex cache without a copy, **not kept** (28 September) | fetch 0.93 to 0.84 ms, projection and corners 0.07 ms more; decoder 5.00 to 4.97 ms, on 2,400 lists each | not run: nothing to resolve |
| Triangle header in one read, cull direction per batch, **not kept** (28 September) | decoder 5.03 to 5.02 ms, on 2,400 lists each | not run: nothing to resolve |

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

The eighth: in `dkr_clip_near`, a fifth of the samples were the three
structure copies that loaded the triangle into the first polygon buffer, and
the per-vertex copies of Sutherland-Hodgman were ten-word loops too. The
first pass now reads the triangle where it is, and every copy is written
field by field.

The ninth is below what the frame can resolve. Forty-five `trace` calls ran
with the trace off, each pushing its arguments for `trace` to drop them; a
`TRACE` macro now tests the callback first. The zones show the decoder 0.16 ms
a list lighter, but the two pairs of normal runs disagree in sign, by more
than that: from here on the run-to-run spread of the render mean, about
0.6 ms, is larger than a single change. It is kept because it cannot cost
anything and the trace output is line for line the same.

The tenth concerns the texture residency lookups, both of which put a memo in
front of a 512-slot scan. The sampler found 95% of `find_resident`'s samples
and 88% of `gl_texture_lookup`'s in the scans, so the memos were missing. New
counters on the TMU (`scans`, `scan-steps` on the `tmu0:` line) said why: a
better hash changed nothing, but both memos were emptied on every upload,
which forced a fresh scan for every texture in use after each one. A key is
never live in two slots of either table, so a checked memo answer is always
the scan's own, and the emptying was never needed. Without it the scans fall
by 41%. What the ones left are, about 220 slots each, is not established: a
multiplicative hash of the whole key, tried on 28 September, left them where
they were (41,195 scans against 29,968, 2.26 against 2.27 us a lookup), so they
are not a matter of the hash. Nor are they lookups of keys resident nowhere:
a third counter, `scan-misses`, reads 747 of 29,011 scans, exactly the number
of downloads. The other 28,000 find their key, which the memo had lost to
another one. With a hash that cannot be improved on, that is capacity: more
textures in use than 256 direct-mapped slots hold. At 1,024 slots, one zone
run on the target: 9,930 scans against 29,011, and 1.14 against 2.29 us a
lookup, 0.10 ms a display list against 0.20. Both memos are now that size,
24 KiB and 16 KiB. The frame was not run for it: the gain is below its spread.

The eleventh fixes a counter as much as a cost. `pass2_wanted` counts the
second passes it declines, and `gl_draw_triangles` asked it twice per draw: on
CAP2600 the card replay's `identity` read 4,315 for 2,341 triangles. It is
asked once now and the answer kept, since the restorations in between put the
same state back. The count reads 2,265, the 12 card images are unchanged, and
the draw zone is 2% lighter.

The twelfth, the same kind. The decoder filled the second texture unit's
coordinates for every triangle with a tile-1 texture bound, 402,771 in a run,
while the backend chained the units for 312 states: only a `DKR_CC_TWO_TEXELS`
recipe uses them. It now fills them for those recipes alone. The saving is
small, about 0.06 ms a list, but the log's `two-layer` line stops contradicting
itself, and the 12 card images are unchanged.

The thirteenth is the largest of the evening. Every state change reprogrammed
the whole of Glide's state: combine, texture modes, blend, depth, cull, alpha
test and fog, some fifteen calls. Each `apply_*` now remembers what it last
wrote and skips a call that would write the same thing. The risk is a register
written behind its back, and blend and depth are, by the extra passes: those
thirteen writes go through `raw_blend_function` and `raw_depth_*`, which forget
the remembered value, and the context's opening and `invalidate` forget it all.
`glide.c` programs none of these. The 12 card images are byte-identical. A
state change costs 28% less, the draw path 8% less, since the restorations
after a second pass got cheaper too, and the display list 0.38 ms. It is the
first change since the ninth that the frame resolves: the render mean falls
0.45 ms in both pairs of runs, and its 99th percentile 1.7 ms.

The backend's per-draw predicates each asked `combiner.c` for the table's
size and an entry, and `dkr_cc_table_count` was 0.5% of the sampler's graphics
thread. Taking the base and size once measured no gain in the draw zone, so it
is not in the tree.

Dividing by `w` once per vertex, when it is loaded, rather than once per
corner took 0.12 ms a list off the projection zone and put it back in the
vertex load: the decoder as a whole did not move. A vertex is shared by about
two triangles, but some of those triangles are rejected before projection, and
the load divides for every vertex. It is not in the tree.

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

- [x] The vertex path's detailed profile is established.
- [x] The number of vertices transformed needlessly is measured, and rejection worked on
      first.
- [x] Every optimisation is measured separately.
- [x] MMX is used only if the measurement justifies it, and on whole blocks.
- [ ] No copy and no reallocation per frame in the vertex path.
- [x] Drawing through vertex arrays is evaluated.
- [x] The vertex path fits within its budget allocation.
- [x] No visual regression after optimisation, verified by image comparison.

Where the others stand, 28 September:

- **No copy, no reallocation.** Nothing in the decoder, the clipper or the
  Glide backend allocates. The projected vertex is written straight in
  `GrVertex`'s layout. One copy remains per corner: the 32 bytes fetched from
  the vertex cache, because s and t arrive with the triangle and not with the
  vertex. The clipper copies only for triangles that cross a plane. Removing
  that copy was tried on 28 September, projecting each corner from the cache
  with its s and t passed apart: the fetch zone lost 0.09 ms a list and the
  projection gained most of it back, 0.03 ms net. The copy costs nothing
  measurable, so it stays, and the criterion stays open on its letter.
- **Visual regression: none, on the card as in the oracle.** Every decoder
  change is byte-identical on the 21 captured scenes, on x86-64 and on i386
  with the target's options. On 28 September the card was checked too:
  `REPLAY.EXE --card` built at `5a49753`, before this ticket, and at `f248776`
  replayed 12 captures on the Voodoo 2 (CAP0050, 0250, 0400, 0420, 0600, 0700,
  0800, 1500, 2000, 2600, CG0060, CKEY1622). The 12 images are byte-identical,
  and the card is deterministic: the same binary twice on CAP0050 gives the
  same image. The run goes through a `.BAT` of `START /W` lines. One trap: the
  transfer disk's FAT16 root directory holds 512 entries, long names take
  several, and when it is full the replay's `BMP` silently fails to be
  created; the batch ends with "Erreur lors de la creation du fichier".
- **MMX: measured, and not justified.** The only block it could cover whole is
  the transformation, `dkr_transform_to_clip`: 4.2% of the graphics thread's
  samples on 27 September, about 0.4 ms of a 9.5 ms render. That is the
  ceiling, and it would not be reached. The projection needs a divide, which
  MMX does not have, so it stays on x87, and every batch of vertices would pay
  an `EMMS` and the transition back. The game's matrices are 16.16 and the
  transform works in float; a 16-bit fixed-point version would not be
  byte-identical, which puts the whole corpus check at stake for a fraction of
  a millisecond. No MMX code was written.
- **The budget: met.** Measured the way `frame-budget.md` measures it, in
  exclusive mode over a steady window, the graphics thread is awake 9.15 ms of
  a 36.2 ms frame, against 12.7 ms on 24 September and the 12 ms proposed for
  it. The frame's median is two retraces, 33.5 ms; its mean is set by the slow
  frames, not by the renderer's mean.
- **Resolution.** From the ninth change on, the spread between runs of the
  render mean, about 0.6 ms and seemingly bimodal, exceeds a single change.
  The zones still resolve them; the frame no longer does.

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
