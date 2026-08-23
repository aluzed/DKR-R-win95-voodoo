# Bringing the game's rendering up on the Voodoo

Measured on the test machine on 16 August 2026, with the ROM. This report
consolidates a session that took the port from "the game runs and presents
nothing" to "the game draws its menu on the card, and the screen is black for a
reason that is now named".

It is written after the fact, from the commits and the logs. That is a debt, not
a method: what is measured belongs here, and the eleven defects below were found
by measurement whose only trace was a commit message.

## Where it started and where it stands

| | before | after |
|---|---:|---:|
| Commands decoded per run | 3,580 | 46,992 |
| Vertices read | 0 | 66,363 |
| Triangles read | 0 | 66,212 |
| Triangles emitted to the card | 0 | 294,345 |
| Triangles with a texture bound | 0 | 291,916 |
| Textures converted and resident | 0 | 45,972 |
| Rejections | 589, one per list | 75 |

The chain is continuous on real hardware: display list → F3DDKR decoder →
transformation → clipping → Glide backend → Voodoo 2. The card takes the video
output, the Windows desktop disappears from the capture, and what remains on
screen follows the game's state.

What is **not** established is that the image is right. It is not.

## The eleven defects, in the order they were found

Each was found by a measurement, and most of them were invisible to every
measurement that preceded them. That ordering is the report's real content.

### 1. The decoder stopped every list at its first unknown opcode

It knew seven opcodes. Facing the game rather than hand-built lists, every list
stopped at its first RDP command — and the geometry was always after it. 600
lists, 3,580 commands, zero triangles, exactly one rejection per list.

The F3D (`0xB0..0xBF`) and RDP (`0xE4..0xFF`) families are now recognised and
skipped, eight bytes each. **Enumerated rather than accepting everything**: an
opcode outside those ranges still stops the list, and it is that property which
later established that the memory layout was right — no address rejection ever
appeared.

The lower bound was first set at `0xB6`, by reading a table. The machine
answered `0xB4` — `G_RDPHALF_1` — six hundred times. The table listed only the
commands with a geometric effect.

### 2. The startup sequence contains no geometry at all

The opcode histogram, a thousand bytes of instrument, answered what no reasoning
settles: what a frame of DKR is made of.

```
BA:6901  SETOTHERMODE_H     FC:2569  SETCOMBINE
BC:5946  MOVEWORD           EF:1798  RDPSETOTHERMODE
E7:4646  RDPPIPESYNC        FF:1795  SETCIMG
                            ED:1788  SETSCISSOR
                            01:1570  Matrix
```

Neither `0x04` (Vertex) nor `0x05` (Triangle). Zero triangles was not a defect of
the chain — it is what the game sends. `FILLRECT` is the **only** draw order
emitted at startup, twice per frame.

Without a counter named for it, "absent from the top eight" would have read as
"rare" rather than "nil", and the missing geometry would have been hunted in the
decoder.

### 3. The counted display list never returned

`OP_DLCOUNTED` pushed the return address, jumped, and **never used the count** —
yet the count is what ends a counted list, which by definition has no `ENDDL`.
The decoder fell out of the bottom and carried on into whatever memory followed
until it hit something random.

The symptom was mute, and that is what made it expensive: seventy commands per
list, **constant** from list 300 onward, two fills and not one triangle. Nothing
cried out. Three instruments were needed, each answering what the previous one
left open:

1. the opcode histogram — "there is no geometry";
2. named draw counters — "and no textured rectangle either, so nothing at all,
   not merely rare";
3. **dumping a whole list** — the counters had gone stable, hence mute. A
   constant number says nothing more about what the game is building.

What list 300 showed: two full-screen fills, the matrices set up, then
`CountedDisplayList 7 commands` loading a texture — and nothing more. DKR loads
its textures that way, and **all the geometry comes after that return**. It had
been lost there, every frame, from the beginning.

List 300 was not picked at random: it is where the throughput stabilises, hence
the first list that describes the state the game stays in. Dumping the first
would have given the initialisation sequence.

### 4. The RDP state reached no one

`rdp_state.c` already decoded the RDP state and knew how to translate it; nobody
called it. The decoder skipped `SETCOMBINE` and the `SETOTHERMODE`s, so every
triangle left with the backend's default state — hence an entirely white image.

The encoding was **surveyed on the machine** rather than read from a header:

```
0xBA001402 w1=0x00000000   shift 20, length 2  -> cycle type
0xBA001701 w1=0x00800000   shift 23, length 1  -> the bit is already there
0xB900031D w1=0x0F0A4000   shift  3, length 29 -> render mode
```

Three concordant samples, and they were necessary: **F3DEX2 inverts the shift**.
Getting the family wrong would have touched fields adjacent to the intended ones
— a filter in place of a cycle type — that is, a plausible and wrong image
rather than an outright error.

### 5. The viewport was invented

It came from `dkr_transform_init` — 640×480, plausible and false. The symptom: a
single triangle covering half the screen while the geometry and the shading were
correct. **An invented scale does not show in the counters, only on screen, and
it looks like a wrong matrix.**

`MOVEMEM` with index `0x80` carries it: `short vscale[4]` then `short vtrans[4]`,
in 2.2 fixed point. Measured: scale 160,120 and translation 160,120, that is
exactly one 320×240 buffer, carried to the screen by a factor of 2.00 read from
`SETCOLORIMAGE`. The game reinstalls it every frame — 1,642 times — so nothing
invented survives in the scale.

Three ways that path goes wrong, each guarded: the y sign inverts between the
game's convention and `dkr_transform`'s; a zero scale is refused rather than
projecting every vertex onto one point; and the buffer-to-screen factor is
shared with the filled rectangles, so the 2D interface and the 3D geometry
cannot end up at two different scales.

### 6. Texture loading was refused one time in two

25,853 loaded, 21,195 refused. The backend's descriptor table and the texture
memory allocator had independent lives:

- **An evicted texture kept its slot alive.** The 512 slots fill in a dozen
  frames — DKR loads around forty per frame — then everything is refused for
  want of a free slot.
- **Worse than the refusal**: while the slot lived, it pointed at memory the
  allocator had reassigned. That is word for word the symptom `tex_required`'s
  comment fears twenty lines higher — a piece of scenery carrying another's
  pattern, at a place that depends on load order. The refusal was the visible,
  benign face; the other would have given a plausible and wrong image.

The allocator stays sole judge of what resides where; the table now merely
follows it.

### 7. All the refusals came from the dimensions, none from memory

The figure said "21,211 loads refused" and nothing else. Four distinct causes
produced the same zero. I fixed slot exhaustion on the assumption it was guilty,
and the figure did not move by a single unit — 21,195 before, 21,211 after.

Four integers later:

```
refusal detail: proportions=21084 size=0 slots=0 tmu-memory=0
```

**All** of them were dimensions. The previous fix aimed at a cause that never
occurred — it did close a real latent risk, but it could not change anything, and
a three-line measurement would have said so beforehand.

The cause: the RDP samples any dimensions, the Voodoo demands **powers of two**,
a side of at most 256 and a ratio of at most 8:1. We now pad up to the next power
of two **by repeating the pattern** rather than leaving it blank: DKR wraps its
textures eighteen times against four clamps, and blank padding would show at the
seams of every repeated texture.

### 8. The texture coordinates were not normalised

The decoder read raw `s` and `t` from the display list and handed them straight
to `dkr_clip_project`, whose comment says it expects [0,1] before applying
Glide's scale of 256. The microcode gives **10.5 fixed point** — thirty-two steps
per texel. The division by 32 and by the texture's width was missing.

The order of magnitude says why nothing sampled: for a 32-texel texture, a
right-hand corner is 1024 raw, hence **262,144** after Glide's scale instead of
256. It was not a shifted texture, it was a texture outside everything.

Two subtleties, each with its own way of going wrong: one normalises on the
**padded** width, not the real one, since the texture occupies only the top-left
corner — normalising on the real width would stretch the pattern by up to a
factor of two, a discreet defect one would blame on dimension decoding. And the
default scale is not zero: without a bound texture the coordinates are unused,
but zero would collapse them onto a point, which looks like a transformation
defect rather than an absence.

### 9. The texture handle was overwritten by the state translation

Three figures, separated, left one explanation:

```
textures loaded                          45,773
triangles emitted with a texel combiner  246,707
triangles emitted with a texture bound         0
```

`dkr_rdp_to_render_state` fills **the whole** state block from the RDP state, and
the RDP state knows nothing of our handles. The `texture` field the load had just
placed there was overwritten at every application — that is, before every
triangle.

The handle now lives in the decoder's context, which is its proper place: it is a
resource of the decoder, not a mode of the RDP.

Gathered under a single "emitted" counter, those three figures left no
explanation at all, and the combiner or the sampling would have been fixed at
random. That is the third time in this session that an over-aggregated counter
hid the cause.

### 10. `RDPSETOTHERMODE` was skipped, and it carries the whole word

`G_RDPSETOTHERMODE` (`0xEF`) replaces both halves of the mode word at once, where
`SETOTHERMODE_H` and `_L` write one field each. It was skipped along with the
syncs, for want of looking at what it did. The mode therefore stayed frozen on
the last partial setting — in practice the full-screen fills'.

**The opcode histogram carried the answer from the start**: `EF:1798`, in the top
eight, recorded several cycles earlier. It read as RDP noise because it sat next
to the syncs.

Three effects, same ROM:

```
cycle mode     FILL everywhere   ->  2 cycles (correct)
depth          0 triangles tested ->  281,110 of 294,042
coordinates    s=[-63..62]        ->  s=[-7.6..9.4]
```

The depth test in particular: the `Z_CMP` and `Z_UPD` bits travel in that word. I
had written that "we were correctly decoding an absence" — that is false, and my
own measurement corrects it: the game was asking for them, we were not reading
them. The earlier conclusion was right about the sample of `SETOTHERMODE_L`
examined and wrong in substance, because it was looking at the wrong command.

**And the screen went black**, which is worth saying rather than burying behind
three improving figures.

### 11. Blending was hard-coded

`dkr_rdp_to_render_state` did `out->blend = DKR_BLEND_ALPHA;` unconditionally.
The blender word was decoded into `rdp->render_mode` two lines above and never
consulted: every surface in the game was blended, opaque ones included.

The defect stayed hidden as long as depth was inactive — with no test, each
triangle covered the previous one and one saw the last. It surfaced when
`G_RDPSETOTHERMODE` was wired, which made it look like a symptom of *that* fix.
Two defects, one masking the other, and it is the second one that gets blamed.

Now derived from `FORCE_BL` and cycle 2's B factor, **with the positions shifted
by three** — `render_mode` is the shifted low word, so `FORCE_BL` is at bit 11
and not 14. Writing the full word's positions would have read neighbouring fields
and given a plausible result: a blend mode chosen, different per surface, and
wrong.

The result validates itself through a correlation nothing imposed:

```
blend:  0:175575  1:108003  2:10485
depth:  mode1=105665  mode2=175575
```

**175,575 opaque surfaces and exactly 175,575 in depth test-and-write.** Two
independent derivations — one from the blender, one from the Z bits — land on the
same number: opaque surfaces write depth, translucent ones only test it. That is
what a game does, and no shift error produces that agreement.

## Where the game actually is

No rendering measurement could answer this: they all described what the game
draws, never **where it is**.

`gGameMode` is the variable DKR itself consults. Its address comes from the
decomp's symbol map — `0x801234EC` — and the read is native, with no byte swap:
for an aligned 32-bit word, librecomp's XOR-3 interleaving and the host's
little-endianness cancel out.

```
[game] gGameMode=-1 (INTRO) loading=0 level=0x801FB780
[game] gGameMode=1  (MENU)  loading=0 level=0x801FB780
[game] gGameMode=1  (MENU)  loading=0 level=0x801FB890
```

**The game starts, plays its intro, reaches the menu and loads a level there.**
It is not stuck, and above all it is not in `GAMEMODE_LOCKUP` — the state DKR
gives itself when `get_lockup_status()` returns true, and where no rendering fix
would have changed anything.

The game's logic is therefore no longer in question. What remains is rendering,
and this time on a sure footing: what we are looking at is the menu DKR intends
to display.

## The decoder side is entirely cleared

The screen is black and the combiner computes `texel * shade`. Six inputs,
measured one by one rather than suspected:

```
shade-max=255 alpha-max=255            the vertex colour is bright
texels: black=0 with-content=779       no texture is empty
emitted: with-texture=293040           the texture is bound to the draw
emitted: texel*shade+a=267403          the chosen combiner reads the texel
blend: 0:175575 (opaque)               blending masks nothing
```

Six measurements, six eliminations. No input of the computation is null, and the
result is. The defect is therefore no longer in the decoder nor in the state
translation: it is in what is actually programmed on the card.

## The probe, and what it establishes

The clean move at that point is to reproduce the faulty state in a witness rather
than go on instrumenting the game — inside the game you observe what you send,
never what comes out. `state_probe.c` (`TEST.EXE`) sets the state the game
produces, draws a known triangle, and reads the frame buffer back. Six cases,
degrading from closest-to-the-game down to the simplest draw.

| case | sampled colours | |
|---|---|---|
| the game's state, as is | in=0x000042 out=0x000042 | |
| without depth | in=0xFFFFFF out=0x000042 | painted |
| oow of 1 instead of 0.001 | in=0xFFFFFF out=0x000042 | painted |
| texel-only combiner | in=0xFFFFFF out=0x000042 | painted |
| no texture bound | in=0xFFFFFF out=0x000042 | painted |
| vertex colour only | in=0xFFFFFF out=0x000042 | painted |

The first case does not paint; the second, which differs from it only by the
depth test, paints. The conclusion drawn at the time — *the depth test erases the
triangle* — **is wrong**, and the section after next says why: the bisection
draws each state once, so it cannot tell a property of the state from a property
of the position, and here it is the position that matters.

That also corrects an earlier assertion of mine. I had written "depth is out of
the question", resting on a run of the game with `DKR_NO_DEPTH=1` that stayed
black. The assertion was too strong: that run showed there is **another** cause in
the game, not that depth is not one. Both facts hold together; it was my wording
that was false. A negative test rules out what it measures, never more.

### The probe also corrected a measurement of mine

Its first version counted pixels differing from the clear colour it had asked
for, and returned 307,200 out of 307,200 for all six cases. **A saturated count
does not mean "everything is painted"**: it means the reference colour is wrong,
the card not reading back in the assumed format. Two sampled points, read and
printed, cannot saturate.

## The depth conclusion was wrong — measured 17 August 2026

The bisection above never varied depth and `oow` independently *with the test
enabled*: case 1 is depth on at `oow = 0.001`, case 2 depth off at the same
`oow`, case 3 depth off at `oow = 1`. It could therefore not tell a property of
the state from a property of the position, and it reported the wrong one.

Two additional passes were added to `TEST.EXE` and answer it outright.

**The sweep** holds the depth test enabled and moves `oow` alone across the
range the game produces:

```
oow            w        sampled colours
2.000000       0        in=0xFFFFFF out=0x000042 <-- painted
1.000000       1        in=0xFFFFFF out=0x000042 <-- painted
...
0.001000       1000     in=0xFFFFFF out=0x000042 <-- painted
0.000100       10000    in=0xFFFFFF out=0x000042 <-- painted
0.000096       10417    in=0xFFFFFF out=0x000042 <-- painted
```

All eleven rows paint. That rules out both depth explanations at once — a
misconfigured test would erase at every distance, and saturation against the
cleared `GR_WDEPTHVALUE_FARTHEST` would erase the far rows only. And the row at
`oow = 0.001` is **byte for byte the state of case 1**, which does not paint.

**The repeat** draws that state four times in a row, from a cold start, reading
the back buffer before presenting and the front buffer after:

```
pass 1: back in=0x000042 out=0x000042             front in=0x000042 out=0x000042
pass 2: back in=0xFFFFFF out=0x000042 <-- painted front in=0xFFFFFF out=0x000042 <-- painted
pass 3: back in=0xFFFFFF out=0x000042 <-- painted front in=0xFFFFFF out=0x000042 <-- painted
pass 4: back in=0xFFFFFF out=0x000042 <-- painted front in=0xFFFFFF out=0x000042 <-- painted
```

**The first draw of a run does not rasterise, whatever its state.** Depth was
merely the state that happened to be attached to it. With the repeat placed ahead
of the bisection, case 1 paints where it used to stay black — the same code, the
same state, a different position.

### Three explanations tested and refuted

Naming them matters, because each was plausible enough to have been committed as
a fix:

| hypothesis | how it was refuted |
|---|---|
| the depth test is misconfigured | the sweep paints at every distance |
| far `w` saturates against the cleared FARTHEST | `w = 10417` paints |
| the read-back lags the retrace-scheduled swap | the back buffer, read *before* presenting, is black on pass 1 too |

The third was the strongest of the three: `dkr_glide_swap` calls
`grBufferSwap(1)`, which schedules the flip for the next vertical retrace, so a
front-buffer read that follows a present too closely really can return the
previous frame. `grSstIdle` was added before the lock and changed nothing — with
the probe printing `grSstIdle resolves: yes`, because "the fix did nothing" and
"the fix was never connected" look identical in a log and are not the same
statement. `dkr_glide_read_backbuffer` then settled it: back and front agree on
all four passes.

That function stays, though it fixed nothing. It is the control that ruled the
timing out, and reading what the card drew without depending on when the flip
happens is the right call for a measurement.

### Two traps of the environment, met again

**A zero-byte log after every line was flushed.** `fflush` hands the bytes to
the OS; Windows 95 commits the directory entry — size *and* first cluster — only
at close. A run that does not reach `fclose` therefore leaves no trace at all,
not a truncated one: the recovered directory entry read `start_cluster=0
size=0`. `say()` now calls `_commit` per line, exactly as `dkr_diag_commit`
already does in the game.

**And I read that empty log as a hang in the back-buffer lock.** It was not: the
wait heuristic driving the machine fired early and the run was stopped mid-flight.
The lock reads its 307,200 pixels. An absence of output was taken for evidence
about the thing being measured, which is the same error this report already
records three times over.

### What remains

Why the first draw produces nothing. The next cut is one variable wide: the same
repeat, once with a texture bound and once without. If the untextured first draw
paints, the texture upload that precedes it is implicated; if it does not, the
cause is in the context or in the first frame itself.

Its practical weight is small — one frame in thousands, invisible in play — but
it has already cost one wrong conclusion and would cost more, since every
witness in this repository measures by drawing a scene and reading it back.

## Seeing the screen at last — 17 August 2026

Every measurement above is a counter, and counters say what was sent, never what
came out. A passthrough Voodoo drives the monitor through an analogue relay, so
its output appears in no capture the emulator can take: until now **nobody had
seen what this port draws**.

`DKR_DUMP_FRAME=<n>` writes display list *n* to `D:\FRAME.BMP`, read from the
back buffer before the swap. At list 400, with the game at its menu:

```
[gfx] frame dump: D:\FRAME.BMP 640x480 corner=313429 differing=299239/307200
```

**The screen is not black.** It is uniform: six distinct colours, all within a
few units of `#393839`, a dark grey-green, with faint horizontal banding across
the top 40%.

### The metric was wrong, and it is the trap this file already records

`differing=299239/307200` reads as "97 % of the screen is painted". It is not:
almost every pixel differs from the corner by one or two units in a single
channel. Counting pixels that differ from an assumed background is precisely
what `state_probe.c`'s own comment warns against — "a saturated count does not
mean everything is painted" — and it was reproduced here the same day, in a
metric written after that comment.

The honest measure for "is there an image" is the **distinct-colour count**: six.

### What the counters say at the same moment

All of them healthy, which is the point:

```
textures: uploaded=39423 reused=872 refused-tmu=0 unknown-format=0
emitted:  textured=269302 | shade=3177 texel=21123 texel*shade+a=245494
texels:   black=0 with-content=685      shade-max=255 alpha-max=255
depth:    mode0=11173 mode1=101964 mode2=156657
blend:    0:156657 1:104366 2:8771
```

Textures reach the TMU without a single refusal, a quarter of a million triangles
go out with a texture bound, the textures have content, the vertex colour is at
full brightness. And the result is one flat colour.

### The catalogue is found and not used

One line settles where to look next:

```
state:     applied=26698 approximate=26270
combiners: catalogued=26270 unknown=428
```

`approximate` and `catalogued` are **the same number**. Every configuration the
catalogue now identifies is still being translated approximately, because
`dkr_rdp_to_render_state` never calls `dkr_cc_lookup` — it does its own coarse
reasoning and picks one of four `DKR_COMBINE_*` modes. The 29 entries each carry
a `dkr_cc_setup`, the actual Glide combine settings, and nothing reads it.

So the work of E05-S03 stands as follows: the mapping exists, it is correct, it
is now looked up correctly — and the card is still programmed from a four-way
approximation. Carrying `dkr_cc_setup` through the backend interface is what
remains.

Whether that is also what flattens the image is **not** established, and is not
claimed here. `texel*shade+a` on a texture with content should show the texture,
approximation or not.

### The bisection: the texel is a constant

`DKR_FORCE_COMBINE=shade|texel|texel_shade|texel_shade_a` forces every draw to
one mode. Three points, and they are unambiguous:

| combine | distinct colours | frame |
|---|---:|---|
| normal, `texel*shade+a` | 6 | dark grey `#393839`, faint banding (list 400) |
| `shade` | 1 | `#000000` (list 400) |
| `texel` | 3 | flat mint `#84FFCE` (list 200) |

**The texture path writes pixels, and it writes one colour.** ~~Every sample
across the whole screen returns the same texel. That is the defect to chase.~~

> **Withdrawn on 18 August 2026, and the conclusion was wrong.** The screen is
> one triangle. See "The flat frame had no defect behind it" below. What the
> three points above establish is that the *visible* colour comes from the texel
> and not from the shade — which is true, and much weaker than what was written.

Two reservations, both mine to state rather than to leave implicit:

- **The `shade` frame does not prove nothing was drawn.** It proves the result
  equals the clear colour, which is black. Geometry drawn in black over black is
  indistinguishable from geometry not drawn. Since the same geometry visibly
  rasterises under `texel`, the likelier reading is that the vertex colour is
  near zero at this point in the frame — and `shade-max=255` does not contradict
  that, being a maximum over the whole run rather than a current value. My first
  wording, "with the vertex colour alone nothing is written at all", claimed more
  than the measurement supports.
- **The three frames are not all from the same list**, 400 for two of them and
  200 for the third, because the run that would have dumped at 400 was slowed by
  an instrument. A flat colour at either list is still flat, so the conclusion
  holds; an exact comparison of hues would not.

### Three instruments, three anomalies that were not the port's

Worth listing together, because they are the same error wearing different hats,
and because this repository already documents two of the three patterns in its
own comments:

| instrument | symptom | cause |
|---|---|---|
| pixel count | "97 % painted" on a flat grey | counted against an assumed background |
| the log | zero bytes | `DKR_TRACE_SP` dropped, and it is what drives `dkr_diag_commit` |
| the forced-mode announcement | no frame at all | printed per display list, slowing the game tenfold so the dump never came up |

The last is the one to remember: a diagnostic whose only visible symptom was a
missing file, and whose real effect was to change the thing it measured.

**No combiner configuration was recognised — fixed 17 August 2026.** 32,411
applications, five distinct keys, zero found in the table.

There were two faults here, one behind the other, and the first diagnosis in this
report named neither correctly. It said "the table is written in the semantic
encoding and the decoder produces the raw encoding"; that was true of the
*hand-written* table and false of the generated one, and the correction that
followed — "both are raw" — was true of the generated table and false of the one
the game actually consulted. The accurate statement is below, in two parts.

**First fault: the key did not normalise.** For the generated table, both sides
were in the *raw* encoding; they simply used different representatives of the
same value. The RGB mux fields are wider than the list of
inputs they select — in a 4-bit `a` or `b` every value from 8 to 15 means zero,
in the 5-bit `c` every value from 16 to 31 does:

```
the table writes   G_CC_MODULATEIA_PRIM as {1, 8,3,7}
the machine reads                          {1,15,3,7}
```

The same combiner, and the key called them different. `dkr_rdp_combiner_key`
masked each field and packed it as it stood.

Measured both ways on the two configurations recorded from the machine:

| | resolve |
|---|---|
| without the normalisation | **0 of 2** |
| with it | **2 of 2**, by name — `G_CC_MODULATEIDECALA + G_CC_BLENDI_ENV_ALPHA_PRIM2` and `G_CC_MODULATEIA_PRIM + G_CC_BLEND_ENV_ALPHA2` |

### Second fault: the game still said `catalogued=0`

That fix landed and changed nothing in the running game. The keys, however,
moved — `13FFFE41` became `13FFF041`, which is the normalisation working. **A
correct key that finds nothing means the lookup is asking something else**, and
it was. There were two catalogues:

| | entries | encoding | consulted by |
|---|---:|---|---|
| `CC_TABLE`, in `combiner_table.h` | 29 | raw, generated from the game's source | nobody |
| `KNOWN`, in `rdp_state.c` | 8 | hand-written shorthand where `0` means zero | the decoder's safety net |

`KNOWN` wrote `G_CC_MODULATEIA` as `{1,0,4,0}`. A literal `0` in `b` or `d` means
`COMBINED`, not zero. Every one of its entries therefore described a different
combiner from the one it was named after, and none could match a decoded
configuration — a safety net that could only ever report failure, which is worse
than no net at all. It is exactly the failure mode `test_rdp_state`'s own header
warns about: "a transcription goes wrong silently, and the test would then share
the error of the code it checks."

It was removed rather than repaired, and the decoder now calls `dkr_cc_lookup`.
Measured on the machine, same ROM, same three-minute window:

| | catalogued | unknown |
|---|---:|---:|
| before | **0** | 24,286 |
| after | **20,986** | 424 |

Two keys remain, both in one-cycle mode, and both decode cleanly:

```
09FF9108  rgb (0,0,0,SHADE)   alpha (0,0,0,SHADE_A)   = G_CC_SHADE
0EF93108  rgb (0,0,0,TEXEL0)  alpha TEXEL0_A * PRIM_A
```

They are absent because the inventory the generator reads covers what the game
declares in its *static tables*, and these two do not appear there. They are left
to the fallback and recorded here rather than hand-added to a generated table —
which is the mistake this whole section is about.

The normalisation lives in the key, not in the decoder: the decoded structure
keeps what the game sent, which is what a trace should show, and only the
comparison needs the equivalence class. `dkr_combiner_eval` was checked rather
than assumed — `rgb_a` and `rgb_b` fall through to zero across the whole
out-of-range span, so evaluation is unaffected either way.

**The collision test had to change with it**, and that is the part worth
remembering. It asserted that two configurations with different bytes must have
different keys. Normalisation makes that premise false — two spellings of the
same combiner now share a key, correctly — so a `memcmp` would have reported the
fix as a regression. It now asks `dkr_combiner_eval` whether the two compute
different things: an arbiter that is not circular, normalisation being what is
under test.

Until this, the approximate fallback — texture modulated by the vertex colour —
was drawing every surface in the game, which is why the shading looked right and
the missing exact match was never felt.

**The render exceeds DKR's watchdog.** With a real Glide rendering,
`worst=270 ms, over-budget(>167ms)=1` in 660 lists. When the measurement was made
on the null renderer the worst case was 60 ms, and the reservation written at the
time — "for a real Glide rendering it says nothing, and the budget will become an
open question again" — has come true. That is what makes the DP-edge fix
(publishing before rendering rather than after) a structure and not a palliative:
it is what keeps that overrun from crashing the game again.

## What this session cost, in method

Three times the same mistake, and it is worth naming because it is cheap to
avoid: **an over-aggregated counter hides its cause**. "21,211 refusals" over four
causes, "emitted" over three questions, "absent from the top eight" read as
"rare". Each time the fix went to the wrong place first, and each time three lines
of separated counters would have said so beforehand.

And twice the opposite mistake, which is worse: a wrong instrument that answers
regularly. The blend counters accumulated **inside** the block that prints one
list in sixty, so they saw a sixtieth of the frames — coherently. 5,731 against
380,000 emitted reads as "blending is almost never set" rather than as a sampling
error.

## References

- `platform/render/f3ddkr.c`, `rdp_state.c`, `texture.c`, `glide_backend.c`
- `tools/win95/witnesses/state_probe.c` — the bisection, `TEST.EXE`
- [`win95-first-game-crash.md`](win95-first-game-crash.md) — the watchdog and the
  DP edge
- [`win95-glide-states.md`](win95-glide-states.md) — the W buffer's comparison
  direction
- E04-S06 RDP state · E04-S07 textures · E05-S02 TMU · E05-S03 combiner

## The flat frame had no defect behind it — 18 August 2026

The conclusion above, "every sample returns the same texel", is **wrong**, and
the measurement that overturns it is one number:

```
[gfx] frame dump: areas <1px=17 <100=97 <10k=7 >=10k=16
[gfx] frame dump: largest triangle=734776 px of 307200
```

**A single triangle covers two and a half times the screen.** Whatever texture it
wears is what the whole frame shows. A flat image is exactly what one expects,
and nothing about texture sampling follows from it.

### How the mistake was made, and how it held for a day

The bisection was sound and its three points still hold: forcing the combiner to
the vertex colour gives the clear colour, forcing it to the texel gives a
coloured frame. What that establishes is that **the visible colour comes from the
texel rather than from the shade** — true, and far weaker than "every sample
returns the same texel".

The step from one to the other assumed the screen showed many surfaces. Nobody
had counted. The question "is this a scene or one quad?" was never asked, because
every instrument in place answered "what is drawn", and none answered "how much
of the screen does one triangle own".

What kept it alive is that each new measurement kept confirming health elsewhere
— `skipped-dead=0`, `changed=3424`, 82 % of triangles spanning texture space —
and every confirmation read as "the defect is further in" rather than as "there
may be no defect".

### Three aggregates, three wrong turns

The pattern is consistent enough to be a habit rather than three accidents. Every
counter here that summarised before it was read cost a wrong turn:

| aggregate | what it hid |
|---|---|
| pixels differing from the corner | "97 % painted" on a six-shade frame |
| run-wide `s_min`/`s_max` | wide extremes are compatible with every triangle sampling one point |
| the `>=10k` area bucket | ten thousand pixels and 734,776 share a bin |

Each was replaced by something that cannot saturate: a distinct-colour count, a
per-triangle test, an actual maximum.

### What is actually established

- The chain draws, on the card, with textures resident and bound, and the visible
  colour comes from the texel.
- At display list 200 the screen is one enormous triangle. **Whether that is
  right is not known**: a background quad extending past the viewport is normal
  and gets scissored, and E04-S05's guard band bounds coordinates at four
  half-screens, so 734,776 pixels is not by itself absurd.
- Nothing is established about texture sampling, in either direction.

### What to measure next, and it is not a texture question

Dump several lists in one run rather than one. If every list is dominated by a
single huge triangle, the question becomes why the game is drawing that and not
its menu — and that is geometry, not sampling. If later lists show many
comparable surfaces coming out in three colours, the sampling question returns,
this time properly founded.

A second, cheaper reading is available from the same runs: the per-list areas
differed between two runs of the same list — `2/75/12/17` and `17/97/7/16` —
so list 200 is not identical from one run to the next. That is worth knowing
before treating any single list as a reference.

## The geometry is projected hundreds of times too large — 18 August 2026

The normalised device coordinates, taken before any clipping, over six lists of
one run:

| list | ndc x | ndc y |
|---|---|---|
| 100 | −30.6 … 209.4 | −20.9 … 175.0 |
| 220 | −189 … 192 | −40 … 448 |
| 340 | −1773.8 … 319.4 | −136.9 … 464 |
| 460 | −81.6 … 283.8 | −64.5 … 340.8 |
| 580 | −461.5 … 117 | −20 … 551 |
| 700 | −14.5 … 777.5 | −31.5 … 389.2 |

**On-screen geometry belongs in [−1, 1].** This is two to three orders of
magnitude out, and the factor varies from list to list rather than being a
constant scale.

That single fact accounts for everything the last two days measured:

- the guard band saturating on every list, at exactly ±2 screens;
- a largest triangle of 2,457,600 px, exactly eight screens, which is half the
  clamp box — a triangle spanning it corner to corner;
- the flat frames: one triangle covering the screen, so the image is whatever
  texture it happens to wear;
- the black frames: geometry projected so far out that nothing lands inside the
  scissor.

And it **exonerates the texture path completely**. Textures are resident, bound,
downloaded, with correct coordinates spanning texture space on 82 % of triangles.
Nothing was ever wrong there.

### Why this took so long to see

Every instrument between the projection and the screen bounds what passes
through it, so each one reported its own limit rather than the quantity asked
for. Four in a row:

| instrument | reported | actually meant |
|---|---|---|
| pixels differing from the corner | 97 % painted | six shades of one grey |
| run-wide `s_min`/`s_max` | a healthy spread | compatible with one point per triangle |
| the `>=10k` area bucket | 17 large triangles | 10,000 px and 734,776 share a bin |
| projected x,y extremes | −960…1600 on every list | the guard band's own boundary |

The last is the sharpest lesson: **six independent lists agreeing to the pixel is
not a measurement, it is a constant.** `clip.h` documents that exact range in
prose — "At 4, a 640-pixel screen tolerates coordinates from −960 to 1600" — and
I read the number back out of my own log without recognising it.

The rule that would have caught all four: before reading a figure, ask what it
would print if the thing were broken in the most obvious way. A saturated count,
a clamped range and a bucket at its ceiling all print "fine".

### One more, in the instrument rather than the pipeline

The first version of this measurement used `%.2f` and printed **nothing at all**
— the call is reached, the format string is in the binary, no line appears. `%f`
is unusable on this target. The `coords:` line has always scaled to thousandths
for that reason; the workaround was in the code and in no comment, so it cost a
run to rediscover.

### What to look at next

`ndc = x / w`, so either the transformed x is far too large or w is far too
small. The candidates, in the order they can be separated:

1. the model-view-projection matrix the transform receives — E04-S03 verified the
   16.16 fixed-point conversion against hand-computed values, but not the
   composition that feeds it;
2. `MOVEWORD` index `0x0A`, which F3DDKR uses to hand over an MVP directly, and
   which the decoder may or may not be honouring;
3. the vertex scale, if object coordinates are being read at the wrong fixed-point
   exponent.

The measurement that separates them is the range of `w` itself, alongside the
matrix as loaded — printed once per list, not per vertex.

## The 2D content is decoded and skipped — 18 August 2026

`G_TEXRECT`, opcode **`0xE4`**, is the RDP command that draws a textured
rectangle: on DKR that is the menus, the HUD, the text, the icons, the balloons —
every 2D element the player looks at. `G_TEXRECTFLIP` is `0xE5`.

Both fall inside the `0xE4..0xFF` range that `opcode_effect_deferred()`
recognises and **skips**. They are counted in the opcode histogram and never
handed to the backend:

```
draw: vertices=81678 triangles=81068 texrect=8758 texrectflip=0 fillrect=1667
      | handed-over=1667
```

Eight thousand seven hundred and fifty-eight textured rectangles decoded, and
`handed-over` counts only the filled ones. The whole 2D path is missing.

That is why the frames are a flat colour or black. What renders is the 3D
background — a full-screen quad, correctly clipped, wearing a near-uniform
texture — and nothing else. It is not a sampling defect, not a projection
defect, and not a combiner defect. **It is an absence.**

### How it hid for two days

The range was enumerated by its bounds, and the comment that documents it lists
the family as "RDP — synchronisations, scissor, tiles, colours, combiner". It
never mentions that the range **begins** with the two drawing commands. Skipping
was the right decision for the synchronisations and the state writes that make up
the rest of it, and the two exceptions sat at the very start where the prose
stopped looking.

Nothing in the counters contradicted it either, because `texrect` was counted
from the raw opcode histogram — a number that rises whether or not the command
does anything. `fillrect` sits beside it with a `handed-over` count; `texrect`
has none, so "decoded" and "drawn" were never separated for it.

### What this closes

Everything measured over the two previous days is consistent with a correct
renderer missing its 2D path:

- textures resident, bound and downloaded, with coordinates spanning texture
  space — the 3D background needs them and uses them properly;
- both matrices plausible, the menu's orthographic and the scene's a genuine MVP;
- triangles reaching the guard band, which is a full-screen background quad
  clipped exactly as designed — a triangle spanning the band has area
  (2560 × 1920)/2 = 2,457,600, the figure measured on four lists out of six;
- the flat colour: one background quad and nothing over it;
- the black frames: lists that carry only 2D content, all of it skipped.

### What remains

Implementing `G_TEXRECT` is E05-S07's remaining scope — the ticket already has
the filled rectangle, the half-texel offset and the fill rule measured, and
names the textured rectangle as the piece the game's text depends on. The command
carries its coordinates across the following `RDPHALF` words, which the decoder
also currently skips, so the two go together.

The counter to add alongside it is the one whose absence hid this: a
`handed-over` figure for textured rectangles, so that "decoded" and "drawn"
cannot be read as the same number again.

## `G_TEXRECT`, and what a reference capture settled — 18 August 2026

Implementing the command took an afternoon. Establishing that one line of it was
wrong took a reference, and nothing inside the port could have done it.

### The command

```
w0   opcode<<24 | xh<<12 | yh      lower-right, 10.2
w1   tile<<24   | xl<<12 | yl      upper-left,  10.2
then RDPHALF_1   s<<16 | t         10.5
then RDPHALF_2   dsdx<<16 | dtdy   5.10
```

Read from `gsSPTextureRectangle` in the decompilation's `gbi.h`, not recited.
`w0` carries the **lower-right** corner and `w1` the upper-left, which is the
reverse of reading order.

**The half-words are captured by position.** `gbi.h` computes
`G_RDPHALF_1 = G_IMMFIRST - 12 = 0xB3`; this decoder's own comment claimed the
machine answered `0xB4`. The macro emits them adjacently by construction, so
position is reliable where numbering is disputed — and the log settles it:
`halves=B3,B2`. `gbi.h` was right. The code would have worked either way, and
the measurement came free.

### Four hypotheses about the crowded glyphs, and how each died

The text appeared immediately and legibly, and immediately looked crowded. Four
candidates, each costing a run:

| candidate | verdict |
|---|---|
| the `+1` on the lower-right corner | *apparently* refuted: the game's own rectangles already overlap by 2–3 px |
| the blend state | measured correct: `blend=1` is `BLEND_ALPHA`, `combine=1` takes the texel's alpha |
| intensity textures forcing alpha opaque | real defect, fixed, **changed nothing here** — the glyphs are not I4/I8 |
| the texture span overrunning into an atlas | refuted: each glyph is its own 20×28 texture, `s` sampled within [1, 19] |

Every internal measurement answered "correct", because from the inside
everything *was* correct.

### The reference

The neighbouring native port extracts the vanilla assets from the ROM, fonts
included. `BigFont`'s glyphs are 28 tall — the height measured on the machine —
and its metadata gives, per character, the advance and the texture width:

| letter | `char-width` | `tex width` | rect measured | advance | width if `lrx` exclusive |
|---|---:|---:|---|---:|---:|
| D | 15 | 17 | 89..106 | **15** | **17** |
| R | 17 | 19 | 104..123 | **17** | **19** |
| U | 16 | 18 | 121..139 | **16** | **18** |
| M | 24 | 26 | 137..163 | **24** | **26** |
| S | 14 | 16 | 161..177 | **14** | **16** |

Five advances out of five match `char-width`. Five widths out of five match
`tex-size.width` — **only if `lrx` is exclusive**.

So `G_TEXRECT` does not share `G_FILLRECT`'s convention, and the `+1` copied
from it made every glyph one texel too wide, two screen pixels once scaled.

### Why the first refutation was wrong

The rectangle coordinates showed the game's glyphs overlapping by two or three
pixels, and I read that as clearing the `+1`. The observation was true and the
inference was not: DKR overlaps its glyph boxes **by design**, the advance being
exactly two less than the texture width for every letter. An extra pixel hides
inside an overlap that is already there.

Separating a deliberate overlap from an accidental one is not possible from
inside the renderer — both produce rectangles that overlap. It takes a source
that says what the overlap *should* be. That is what E09-S02's comparison harness
is for, and this is the first time the project has needed it in earnest.

## The machine was the slowdown, not the 2D path — 18 August 2026

Runs got progressively shorter through the afternoon: 580 display lists, then
460, 340, 220, and finally **3 in nine minutes**, with the binary barely changed
between the last two. I attributed it to `G_TEXRECT`, on the reasoning that eight
thousand rectangles per run had gone from a skipped opcode to two triangles each
with a texture bind and a state application. That reasoning was plausible and
wrong.

Restoring the reference image — `Run-Win95-VM.sh --restore`, a snapshot from
11 August — put the same binary at **list 1080 in seven minutes**, better than
any run before `G_TEXRECT` existed.

So the collapse was the guest, worn down by some fifteen abrupt stops, several of
them mid-write, on a machine where `AutoScan=0` means Windows never repairs
itself. The transfer disk had accumulated 515 lost clusters against 63 that
morning. The worn image is kept as `win95.img.worn-18aug`: discarding it would
have made the question unanswerable.

**The measurement to retract**: "TEXRECT costs a factor of three". Nothing is
known about the 2D path's cost, and E08-S01 still has no figure for it.

That is the sixth instrument failure of the week, and the first where the
instrument was the machine itself rather than a counter. The pattern holds: every
one of them reported something plausible, and every one of them was believed
until a control was run against it.

## The doubled glyphs: the same rectangle, drawn five times — 19 August 2026

Capturing sixteen rectangles instead of six answered it in one line each:

```
rect0-5    111, 129, 146, 162, 179, 193
rect6-11   111, 129, 146, 162, 179, 193
rect12-15  111, 129, 146, 162
```

**Exactly the same coordinates, repeated.** No offset, so it is not a drop
shadow — the six glyphs of the name are drawn three to five times in the same
place. `texrect seen=30` for a six-letter name is five passes.

That is normal on the N64: DKR composes its text in several passes — outline,
fill, gradient — each pass changing the **primitive colour** that the combiner
mixes with the texel. Five identical passes stack into one thick smear; five
differently-coloured passes make a styled letter.

### And the primitive colour is not decoded

`G_SETPRIMCOLOR` is `0xFA`, `G_SETENVCOLOR` is `0xFB`. Both sit in the
`0xE4..0xFF` range that `opcode_effect_deferred` skips — the same range that hid
`G_TEXRECT`, and whose comment still describes the family as
"synchronisations, scissor, tiles, colours, combiner" without noting which of
those actually affect what is drawn.

So every pass reaches the card with the same state, `combine=1`
(`DKR_COMBINE_TEXTURE`, the texel alone, vertex and primitive colour ignored),
and draws the identical thing. The passes cannot differ because the value that
would differentiate them is never read.

### The audit this calls for

Of the RDP commands the game actually emits, these are still decoded and skipped
while affecting the image:

| opcode | command | effect |
|---|---|---|
| `0xFA` | `G_SETPRIMCOLOR` | the per-pass colour of text and many surfaces |
| `0xFB` | `G_SETENVCOLOR` | the second constant the combiner mixes |
| `0xED` | `G_SETSCISSOR` | clipping rectangle, needed for split screen |
| `0xF8` | `G_SETFOGCOLOR` | fog colour, E05-S06 |
| `0xF9` | `G_SETBLENDCOLOR` | blend constant |
| `0xF5` | `G_SETTILE` | **the clamp/mirror/wrap modes**, format, mask and shift |

**Closed on 22 August 2026.** All five are decoded. `0xFA` and `0xFB` on
19 August, `0xED`, `0xF5`, `0xF8` and `0xF9` since. The scissor is decoded and
its effect is behind `DKR_SCISSOR=1` — see below — and nothing reads the blend
colour yet; the rest reach the card. What the range still defers is
synchronisation, which is what its comment always claimed the whole of it was.

Enumerating a range by its bounds was right for the synchronisations that make up
most of it, and wrong twice now for the drawing commands that sit inside it. The
lesson is not "check `0xE4`" but that a range skipped wholesale needs its
members listed once, against what each one does.

### This is the same gap E05-S03 already has

`approximate` equals `catalogued` because `dkr_rdp_to_render_state` never calls
`dkr_cc_lookup`: the 29 catalogued entries each carry a `dkr_cc_setup`, the real
Glide settings, and nothing reads it. Decoding the primitive colour without
carrying the exact combiner setup would give the passes a colour they still
could not use. The two go together, and they are E05-S03's remaining scope.

## The narrow axis repeated: Glide addresses over the larger side — 20 August 2026

With `G_SETPRIMCOLOR` and `G_SETENVCOLOR` reaching the card, and the dump
anchored on `gGameMode` so the same screen comes back on every run, the menu was
finally comparable to itself. The letters were legible — and each one was drawn
**four times across its own rectangle**, a horizontal smear inside a box that
held a single glyph.

The frame's own report named the cause without a further measurement:

```
[gfx] frame dump: rect tex fmt=0 16x64 padded 16x64 s=[0..16000]/1000 texels
[gfx] frame dump: rect0 85,88..101,152
[gfx] frame dump: rect1 100,88..116,152
```

Sixteen texels sampled across a sixteen-pixel rectangle, out of a texture sixteen
wide. Everything the port could check said "correct", and the screen showed four
copies. **The repeat count, four, is the texture's aspect ratio, 64/16.** That is
what identified it: a defect whose magnitude equals a ratio is a scaling error on
the axis that ratio describes.

### What Glide's coordinate space actually is

Glide does not know a texture's two dimensions. It knows its LOD — the larger
side — and an aspect ratio, which is exactly what `lod_and_aspect` computes
before every download. Its coordinate space follows: **0..256 spans the larger
side, and the smaller side spans only 256/ratio.** On a 16x64 texture, `s` runs
0..64 while `t` runs 0..256.

The port divided each axis by its own dimension, sending 0..256 down both. The
narrow axis was addressed four times too far, and under `GR_TEXTURECLAMP_WRAP`
that is four repeats.

```c
/* before */                              /* after */
tex_scale_s = 1/(32 * padded_width);      big = max(padded_width, padded_height);
tex_scale_t = 1/(32 * padded_height);     tex_scale_s = tex_scale_t = 1/(32 * big);
```

### Why nothing caught it for four months

`glide_texture_probe.c` measured this space on the machine, and its conclusion —
256, not 64, 128 or 512 — is right. It measured it on a **64x64 checkerboard**,
where "divide by the larger side" and "divide by its own side" are the same
division. The probe could not distinguish the two hypotheses because its subject
made them identical.

The host suite had the same blind spot for the same reason: every texture it used
was square, or a single 256x1 row whose `t` was zero everywhere. `test_software.c`
now carries a 16x64 case, with a negative control confirming it reports 40 and 56
instead of 136 and 248 under the old rule.

> **An answer that is right and incomplete is worse than one that is wrong.**
> A wrong answer gets contradicted by the next measurement. This one was correct,
> was documented with its evidence, and was quietly silent about the case that
> mattered — so it was cited three times as settled. What was missing from the
> probe was not rigour but a second subject: the same question asked of a
> non-square texture would have answered it in the same run.

## `G_SETTILE` is deferred too, and it carries the wrap modes — 20 August 2026

The run's own trace names it:

```
[gfx][f3d] deferred 0xF5 w0=0xF5102000 w1=0x00080200
[gfx][f3d] deferred 0xF5 w0=0xF5100000 w1=0x07080200
```

`0xF5` is `G_SETTILE`. Decoding the second word by hand: the first has tile 0 —
`G_TX_RENDERTILE` — with `cms = 2` and `cmt = 2`, which is `G_TX_CLAMP` on both
axes; the second has tile 7, `G_TX_LOADTILE`, the descriptor used to bring the
texture into TMEM rather than to sample it.

**The game asks for clamping, and the port never hears it.** Nothing sets
`wrap_s` or `wrap_t` from the display list, so every texture is sampled under
whatever the render state was initialised with. That is the fourth drawing
command found inside the range enumerated by its bounds, and it belongs in the
audit table above rather than in a fifth rediscovery.

It is not the cause of the flat 3D frames — that is a separate measurement in
progress — and it was deliberately not fixed in the same build. The dump is
anchored on `gGameMode` so that two runs give the same screen; a run that both
adds an instrument and changes the image gives back a comparison worth nothing.

## The flat 3D frames: every triangle at the same depth — 20 August 2026

The 3D lists come back as one colour over the whole screen. Two candidates could
do that, and both were recorded verbatim in the same run rather than tested one
per run:

```
[gfx] frame dump: fills=4
[gfx] frame dump: fill0 0,0..640,480 rgb=FFFFF7
[gfx] frame dump: fill1 0,0..640,480 rgb=000000
[gfx] frame dump: fill2 0,80..640,394 rgb=000000
[gfx] frame dump: fill3 0,0..642,482 rgb=000000
[gfx] frame dump: big-tri (-959,114) (509,-723) (-959,-723) rgb=FFFFFF combine=3 blend=0 depth=2
```

**The fills are eliminated.** All four are full-screen and all four are black or
white, and the screen is 0x393831, 0x635D10, 0x7BFBC6 depending on the list —
never either. The largest triangle is eliminated too: at frame 1 it spans
(-959,114) to (509,-723), which touches only the top-left of the viewport.

So neither. What remains is the matrix, which the same dump prints:

```
mvp[0] 1000 0    0     0
mvp[1] 0    1199 0     0
mvp[2] 0    0    0     0
mvp[3] 0    0    0     160000   /1000
```

Row 2 is entirely zero and row 3 carries only `w`. **Every vertex therefore comes
out with z = 0 and w = 160**: a 320-wide orthographic projection, which is a
perfectly ordinary thing for DKR to send, and which gives every triangle in the
list the same depth.

The backend then does exactly what it is told. It is in W-buffer mode with
`GR_CMP_LESS` — the direction settled by read-back on 14 August — and 79 % of
emitted triangles ask for `DKR_DEPTH_TEST_AND_WRITE`:

```
[gfx]   depth: mode0=2779 mode1=14803 mode2=67973 mode3=0
```

With a strict `LESS` and one single depth value, **the first triangle written to
a pixel wins and every later one is rejected**. The screen shows the first large
quad of the list, flat, and the several hundred triangles behind it are
correctly transformed, correctly textured, and thrown away by a comparison that
can never succeed.

That also explains why the title screen at list 29 is right: its matrix is a real
perspective one — `mvp[2] = (-0.001, 0, -1.001, -1)` — so its `w` varies.

The counter-check is one run and no code: `DKR_NO_DEPTH=1` already exists as a
diagnostic switch. What it cannot say is what the *fix* is — the N64 sorts these
surfaces by submission order, with the RDP's z-compare off, and that is what the
render mode carries. Reading the render mode is E05-S05's business.

> **Three instruments, and the one that answered was the matrix.** The area
> histogram, the fill count and the projected extremes had all been consulted
> across three sessions and each returned a figure that was true and unhelpful.
> What settled it was printing sixteen numbers that nobody had looked at, next to
> the image they produced.

### The counter-check refused it — same day

`DKR_NO_DEPTH=1`, same build, same `gGameMode` anchor, so the same six screens:

| list | with the depth test | without it |
|---|---|---|
| 59  | grey `393831`, 288,000 px differing | black, **144** px differing |
| 89  | grey `393831`, 288,000 | black, 59 |
| 119 | olive `635D10`, 211,200 | yellow `F7F300`, 268,800 |
| 149 | mint `7BFBC6`, 288,000 | black, **0** |
| 179 | mint `7BFBC6`, 288,000 | black, 0 |

Removing the test does not reveal a scene. It reveals **less**: two of the six
frames come back a uniform black, and frame 59 keeps a single 30x34 patch of grey
pixels near the middle and nothing else.

So the reading above is half right and its conclusion is wrong. Every triangle
does land at the same depth — row 2 of the matrix is zero and `w` is a constant
160, both measured — but that is not what stands between the geometry and the
screen. With the test on, the first triangle to reach a pixel keeps it; with the
test off, the last one does. **Both of those are a single flat quad**, which
means the scene is being painted over, not hidden behind a comparison.

That is an order, and every instrument in this report summarises over the frame.
Hence the next one: follow one pixel rather than the frame, and print the last
sixteen triangles that covered it, in the order the card received them.

> The switch cost nothing — it had been in the code since 16 August — and it
> refuted in one run a conclusion that read as settled. It is worth noticing
> which of the two took less effort.

## The reference machine ran ScanDisk at every boot — 20 August 2026

Restoring the reference to rule the guest out, as on 18 August, made things
worse: the game reached the menu, then produced eight SP tasks in seventeen
minutes and no display list at all. The obvious reading — "the restore did not
help, so the guest is not the problem" — is wrong twice over.

The reference snapshot dates from 11 August. It is therefore **older than
`AutoScan=0`**, the MSDOS.SYS setting added on 15 August, and its C: volume was
frozen with the **dirty flag set**. So every boot from it runs a full ScanDisk
over a gigabyte, which then keeps running while the game starts.

```
  volume       : FAT16, 65501 clusters of 16 KB
  clean flag   : DIRTY
```

Clearing the flag and setting `AutoScan=0` — at constant file size, nine
characters taken back from the padding block MSDOS.SYS needs to stay above 1024
bytes — put the same binary at the menu in **a hundred seconds**. The repaired
image is now the reference, `--snapshot` having been run over it.

> The restore was the right move and it was the wrong instrument, because the
> thing being restored carried a defect that the working image had been fixed of.
> **A reference is only a control if it is at least as healthy as the subject.**
> This one was five days behind on a setting whose whole purpose is to stop the
> machine wasting its boots.

There remains an intermittent stall that this does not explain: roughly one run
in two ends after eight SP tasks with `Glide opened at 640x480` as the only
graphics line, and one crashed outright on an invalid read of guest address
`0x024C0010`, far outside the eight megabytes of RDRAM. It is not the renderer —
it happens before the first display list — and it doubles the cost of every
measurement. It is not diagnosed.

## What ends every 3D list: a fade rectangle, and a depth clear — 21 August 2026

Following one pixel rather than the frame is what broke it open. The centre-pixel
stack, printed in submission order with the fills folded into it, reads:

```
centre0 FILL after tri=0   area=307200 rgb=FFFFF7
centre1 FILL after tri=0   area=307200 rgb=000000
centre2 FILL after tri=0   area=200960 rgb=000000
centre3 tri=172 area=2054   rgb=FFFFFF combine=3 blend=0 depth=2 tex=1
centre4 tri=178 area=1928   rgb=FFFFFF combine=3 blend=0 depth=2 tex=1
centre5 tri=227 area=245909 rgb=FFFFFF combine=3 blend=0 depth=2 tex=1
centre6 tri=241 area=36     rgb=FFFFFF combine=3 blend=1 depth=1 tex=1
centre7 FILL after tri=246 area=309444 rgb=000000
```

**A full-screen fill after the last triangle.** Every aggregate the decoder
carries had counted it with the three at the start, where it reads as a
background; in sequence it reads as an erasure. Tracing list 59 in full then gave
the two commands behind it, and both were being executed as something they are
not.

### The first fill is a depth-buffer clear, painted white

```
SetColorImage width=320
SetFillColor raw=0xFFFCFFFC -> 0xFFFFF7
FillRect 0,0..640,480 colour=0xFFFFF7
```

`0xFFFCFFFC` is two halves of the depth far value, not a colour. This is the N64
idiom for clearing z — point the colour image at the depth buffer, fill it, point
it back — and drawn on the visible frame it is a full-screen white flash.

The address was there all along and `RDRAM_MASK` destroyed it: DKR's colour image
is `0x01000000` and its depth image `0x02000000`, so masking to twenty-four bits
made both read `0x000000` and every fill look like a frame clear. Kept raw, and
with `G_SETZIMG` (`0xFE`) decoded rather than deferred, the comparison is exact.

### The last is a screen fade, and it is not a fill at all

`src/fade_transition.c` in the decompilation:

```c
gSPDisplayList(dTransitionFadeSettings);        // G_CYC_1CYCLE, G_RM_CLD_SURF
gDPSetPrimColor(0, 0, r, g, b, gCurFadeAlpha);
gDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE);
gDPFillRectangle(0, 0, width, height);
```

A `G_FILLRECT` **in one-cycle mode**: its colour is the primitive colour, its
alpha is the fade's alpha, and it goes through the blender. The port painted it
with the fill-colour register and opaque — a black sheet over the finished frame.

`fills_wrong_cycle` had been counting these for days, 353 a run, and the figure
was read as an alarm about the mode word. It was a census of a second kind of
rectangle nobody had looked for.

Drawn through the combiner instead, the measured frames go from **4 distinct
colours to 226**, and the trailing rectangle's own numbers say what it is:

```
fill2 0,0..642,482 rgb=00000000 target=blend after-tri=245
```

Primitive colour `0x00000000` — black, **alpha zero**. A fade that is not fading,
and it should be invisible.

### What this did not fix

The frames are still mostly black: 1,494 non-black pixels in a 103x74 patch. The
alpha-zero rectangle is still covering, so the blender is not doing with it what
`G_RM_CLD_SURF` asks. That is E05-S05's ground and it is the next thing to
measure.

> **Two ordering mistakes, one of them mine.** The first: reading a sequence off
> a set of totals. The second, an hour later: `blend_rect_emit` set `texture` and
> `combine` and *then* called `apply_state`, which fills the whole block from the
> RDP state — the comment saying exactly that sits thirty lines above, and was
> written after the same mistake was made on the texture handle. The fade went
> out unchanged and the run was wasted.

### `G_SETSCISSOR` is decoded and switched off

`0xED` is the fourth drawing command that the `0xE4..0xFF` range has hidden.
Measured in list 59, `(0,40)..(319,196)` — DKR's letterbox window — and
`(0,0)..(319,239)`.

Honouring it took the six frames from 226 distinct colours to **one**, pure
black. Two causes are plausible and neither is measured: `screen_scale` returns 1
until `SETCOLORIMAGE` has been seen, so the first window of a list can be laid
down at half size, and nothing resets the card's clip window between lists. So
the command is decoded and counted, and the effect is behind `DKR_SCISSOR=1`.
Leaving a change in the default build that is known to make the image worse would
be trading a measurement for a feature.

## The geometry rasterises; there is almost none of it — 21 August 2026

With the covering sheet gone, the 3D lists show one small object and black. Four
hypotheses were put and three refuted, each by a switch that costs a run and no
code.

| switch | what it tests | frame 59 |
|---|---|---|
| — | as built | 1,687 px painted |
| `DKR_FORCE_COMBINE=shade` | the texture and combiner | 1,788 |
| `DKR_NO_DEPTH=1` | the depth test | 292 |
| `DKR_FLATTEN_W=1` | `oow`/`z`/`ooz` | 0 |
| `DKR_PAINT_WHITE=1` | the colour reaching the card | 2,769 |

**`DKR_PAINT_WHITE` is the one that admits no third reading.** `differing` counts
pixels unlike the corner and the corner is the black clear, so a triangle that
rasterises perfectly and paints black looks exactly like one that does not
rasterise. `shade_max` cannot separate them either — it is a maximum over the
run, the extremes-against-distribution trap for the third time in this file.
Opaque white on black settles it, and it painted 2,769 pixels out of 307,200.

Looking at those pixels settles the rest: they form **a coherent object** with a
clean silhouette, about 148x80, not slivers. So rasterisation is not broken. The
port draws one model and nothing else.

What is still unexplained, and measured twice: three triangles of areas 622,
2,978 and 87,607 cover the screen's centre, are handed over opaque white with the
depth test satisfiable, and **the centre pixel stays black**.

### And a caution about every small number above

Two runs of the same build at the same list gave 2,731 and 292 painted pixels.
The `gGameMode` anchor fixes the list index after the menu is reached; it does
not fix the animation, and the machine's speed varies between runs. **Differences
of a few hundred pixels between runs are not measurements.** Only the large,
repeated facts survive: the object paints, the big triangles do not, and forcing
white changes the count by a factor rather than an order of magnitude.

### `DKR_CLIP_GUARD` was tried and put back

Narrowing the band from 4 to 1.05 does what it should — every centroid then lands
on screen and the over-ten-thousand-pixel bucket falls from 17 triangles to 6 —
and changes the painted count not at all. The header's open question, what value
Glide actually accepts, is still open, and 4.0 was chosen for a precision reason
that still holds. It is back at 4.0.

The clip test had `x = 4` written into it in three places, so narrowing the band
failed a check about attribute interpolation, which has nothing to do with where
the plane sits. It is now written against `DKR_CLIP_GUARD`. A test that breaks
when the thing it is not testing changes is a test that gets edited under
pressure.

## The chain is intact end to end, and the large triangles still do not paint

`submitted=243` — the Glide backend's own count of calls to `grDrawTriangle`,
which had existed since E05-S01 and had never been printed. It matches the
decoder's `emitted`. So there is no gap between "the decoder emitted it" and
"the card was asked to draw it": the last unmeasured link in the chain is
measured, and it is sound.

Against that, `area-in-viewport=429,306` and 2,857 pixels painted.

That figure took two attempts to make honest, and both failures were the same
mistake in different clothes:

1. the area histogram counts the **whole** triangle, guard band included — a
   triangle from -959 to 509 counts 600,000 pixels and puts 29,000 on screen;
2. the first replacement summed the bounding box clipped to the viewport, which
   for a long thin triangle is a hundred times its area.

Bounded by the smaller of the true area and the clipped box, the sum barely
moved — 428,513 against 451,267 — so the geometry is not slivers. It is large
and it is on screen.

### What the histogram says about which triangles fail

```
areas <1px=9 <100=162 <10k=58 >=10k=17
```

The 162 small ones make the object that appears; the 17 large ones dominate the
sum and paint nothing. Every candidate has been eliminated by a switch that costs
a run and no code — the texture and combiner (`DKR_FORCE_COMBINE=shade`), the
depth test (`DKR_NO_DEPTH=1`, which paints *less*), the colour reaching the card
(`DKR_PAINT_WHITE=1`), the guard band (`DKR_CLIP_GUARD` at 1.05, which moves the
big bucket from 17 triangles to 6 and the painted count not at all), and culling,
which the backend disables on the card outright.

The sharpest remaining fact, and it is reproducible: **`DKR_FLATTEN_W=1` paints
nothing at all** — not even the object, which every other configuration draws.
Giving a triangle the `oow`, `z` and `ooz` a textured rectangle carries, on a
card that draws those rectangles happily in the same run, empties the frame.
Whatever is refusing the large triangles is reached by that switch too, and it is
the thread to pull next.

## The canary, and what it narrowed the black frames to — 21 August 2026

Two hard-coded triangles drawn into the game's own frame, immediately before the
read-back, identical except for `oow` — 1 for one, 0.00625 for the other, the
value the menu's orthographic lists give every vertex.

**Both painted 4,950 pixels**, exactly the right-angled triangle they describe.
So in the state that list leaves the card in, the card draws, and `oow` has
nothing to do with it. That is the ground truth no counter could supply, and it
cost one run.

The canary and the game's geometry differ in two things: the vertices, and the
state block pushed before them. `DKR_FORCE_STATE=1` gives the game's triangles
the canary's block — a `memset` with shade, opaque, no depth, no cull — and the
screen comes back **entirely white**. So it is the state, not the vertices.

### It is not depth, and the obvious repair is not the repair

`DKR_FORCE_STATE=2` keeps the depth mode the game asked for and pushes everything
else plain: **113,485 to 147,572 pixels painted**, a third to a half of the
screen, against 1,700. Depth is innocent.

Per frame — and per frame matters; a run-wide census mixing the title screen's
rectangles in with the menu's geometry is what let three candidates be argued
away on figures that did not describe the frame in hand:

```
emitted combine=235/0/0/0/0 blend=218/17/0 depth=0/17/218 alpha-test=0 ref=0
```

All shade, 218 of 235 opaque, alpha test off. Each of combine, blend, depth, fog
(`fogged=12` of 254) and the vertex colour has now been eliminated by a switch.

The obvious reading of `DKR_FORCE_STATE` — "the state is not pushed often enough
and `state_dirty` is missing changes" — was tried: hand the block to the backend
on every triangle and let its `memcmp` dedupe. **1,413 pixels.** No change. So
what `DKR_FORCE_STATE` does is not push more often; it pushes a block that
*differs*, so `gl_set_state` reprograms the card in full rather than
short-circuiting.

That leaves a narrow and precise question, which is where this stands: **which
register does a full reprogramming set that a short-circuited one leaves
hostile?** Every field of the block has been eliminated as a *value*; what has
not been eliminated is the act of writing them.

> Three instruments were built today that each answered "not this", and that is
> their worth. The canary is the one to keep: it is the only measurement in this
> file that asks the card a question whose right answer is known in advance.

## It was fog — 22 August 2026

`DKR_NEUTRAL=8`. One field of the render state neutralised, everything else left
exactly as the game asks — textures bound, real combiners, real blending, real
depth — and the menu's 3D lists go from 2,800 painted pixels to **306,873**, with
2,355 distinct colours and a blue sky.

Every black 3D frame this port has produced was fog, and nothing else.

Two faults compose, and either alone would do it:

- `apply_fog` selects `GR_FOG_WITH_ITERATED_ALPHA`, whose blend factor is the
  **vertex alpha**. On the N64 that is the fog coefficient only when the geometry
  mode carries `G_FOG` and the microcode has overwritten the alpha with it. DKR's
  vertices carry opacity, so an opaque surface asks for maximum fog.
- `G_SETFOGCOLOR` (`0xF8`) is one of the commands the `0xE4..0xFF` range still
  defers, so the colour that maximum fog resolves to is zero. Black.

Fog is therefore off until E05-S06 sources the coefficient properly, behind
`DKR_FOG=1` for whoever does that work. Rendering a scene without its fog is a
known, bounded loss; rendering it black is not.

### The switch that could not act

The same measurement was attempted on 21 August. `DKR_NO_FOG=1` was written into
the **emission loop**, after `apply_state` had already returned — so it cleared
the flag in the decoder's copy of the block and never pushed it. The card went on
fogging. It reported *fog is innocent*, and that was believed for a day, through
five further runs that eliminated depth, the combiner, the guard band, `oow` and
the vertex colour.

What broke it open was refusing to trust any single switch again: `DKR_NEUTRAL`
neutralises a field **and pushes the block**, and the full mask reproducing the
plain block's result — 113,568 pixels — is what proved the mask itself sound
before any single bit was read.

> **A switch that cannot act is worse than no switch at all, because it answers.**
> An absent measurement leaves a question open; a broken one closes it wrongly and
> takes the next five with it. Every diagnostic in this file that modifies state
> now does so where the state is handed over, and the mask is validated against a
> known result before its bits are believed.

## E05-S03's catalogue is wired up, and it changes almost nothing — 22 August 2026

`dkr_cc_lookup` was being called to *count* matches and its answer discarded, so
`approximate` equalled `catalogued` twelve thousand times a run and not one of
the table's twenty-nine Glide setups was ever applied. The index now travels in
the render state — an index and not a pointer, the block being compared with
`memcmp` — and `gl_set_state` applies the setup where there is one.

`catalogued=578 unknown=341` on the menu's 3D lists, so nearly two thirds now go
through the table. **The picture is unchanged**: 1,641 distinct colours against
1,790, the same sky, the same shapes.

That is worth stating plainly rather than quietly. The four-mode shorthand was
already adequate for what these lists draw, and the gap E05-S03 has carried since
it was written was a real gap in the code and not, here, a defect on screen. It
will matter where the shorthand is wrong — the table exists for the
configurations it cannot express — and closing it now means the next wrong colour
is not attributable to it.

The frame's own accounting is consistent for the first time: 431,928 pixels of
on-screen surface handed over, 306,895 painted, `fogged=0`.

### What is wrong now, and the next lead

A third of the frame is black, and the sky's clouds are magenta where they should
be white. `texture.c` produces **RGBA5551 with alpha in bit 0** — "the layout
E05-S02 measured on the card" — and `gl_texture_upload` declares
`GR_TEXFMT_ARGB_1555`, which is `a rrrrr ggggg bbbbb`, alpha in bit **15**. On
paper those disagree by a one-bit rotation.

The measurement may still be right; what makes it worth re-opening is that a
one-bit rotation is **invisible on a grey**, since `grey_to_5551` writes the same
five bits into all three channels. If E05-S02's probe used greys or a symmetric
pattern, it could not have distinguished the two layouts — which is exactly how
the 64x64 texture probe missed the aspect-ratio rule a week ago.

## The texel layout was rotated by one bit — 22 August 2026

`texture.c` produced the N64's `rrrrr ggggg bbbbb a`, alpha in bit 0, and called
it "the layout E05-S02 measured on the card". **E05-S02 measured no such thing.**
Its probe measured texture *memory sizes* — that ARGB 1555 costs two bytes a
texel — and the bit order never entered into it. A sentence in a comment turned
that into a measurement it never was, and `gl_texture_upload` has been declaring
`GR_TEXFMT_ARGB_1555`, alpha in bit **15**, since the day it was written.

What settles it is an arithmetic identity on a pixel actually observed. The
menu's sky came back with clouds in magenta, `(140, 8, 239)`, which is
`(17, 1, 29)` in five bits:

```
0xC43D read as `a rrrrr ggggg bbbbb`  ->  a=1  r=17  g=1   b=29    the magenta
0xC43D read as `rrrrr ggggg bbbbb a`  ->  r=24 g=16  b=30  a=1     a pale blue
```

A pale blue cloud is what the game draws. The card was reading ARGB 1555 and this
file was writing RGBA 5551.

### Why four months did not catch it

The rotation is nearly invisible on the colours one probes with. Every channel
keeps its position to within one bit, so pure red, pure green, pure blue, black
and white all come through recognisably — `0xFFFF` is `0xFFFF` under either
reading. It shows only on **mixed** colours, and worst where red is bright, red's
top bit being the one that becomes alpha.

And a grey ramp cannot see it at all, because `grey_to_5551` writes the same five
bits into all three channels. The test that guarded this path asserted that
RGBA16 "returns the texels unchanged — it is already 5551", on white, pure red,
black and pure green: four values chosen so that every one of them survives the
rotation. It now checks the pale blue that named the defect, whose green channel
of 16 is precisely what the rotation destroys.

### What it changes

| | before | after |
|---|---:|---:|
| Title screen, distinct colours | 1,857 | **2,993** |
| Menu sky, corner | `2982FF` | `94BAFF` |

The title screen is now the game's own logo: red on gold with the star in the O,
`RACING` in its blue-green-yellow gradient, the trademark, on black. The purple
box that had been sitting behind it was the rotation. The menu's sky is a pale
blue with soft white clouds and no magenta anywhere.

> **Third instance of the same failure in one week**, and the pattern is now hard
> to miss: the 64x64 checkerboard that could not see an aspect ratio, the grey
> probe that could not see a bit rotation, the switch that modified a copy and
> could not act. Each was a measurement whose *subject* made the wrong answer
> indistinguishable from the right one. The question to ask of an instrument is
> not "is it correct" but **"what would it print if the thing were broken?"**

## `G_SETTILE`, and the audit closes — 22 August 2026

The last drawing command the `0xE4..0xFF` range was hiding. `cms` and `cmt` carry
the wrap modes, and `dkr_rdp_to_render_state` had been writing
`DKR_WRAP_REPEAT` into both unconditionally because nothing decoded the command —
while the game asks for `G_TX_CLAMP` on both axes of the render tile:

```
0xF5102000 w1=0x00080200  ->  tile 0, cms=2, cmt=2
```

A third of the triangle corners sample outside [0,1] — 432 of 1,269, per frame —
so this is not a subtlety: repeat sends them to the far side of the texture where
clamping holds them at the edge.

Honouring it moved the frame by 6,000 black pixels out of 307,200 and did not
touch the defect it was tried against. It is kept because the decode is right
against the reference, and because the next wrong texel is no longer attributable
to a wrap mode nobody had read.

`G_SETFOGCOLOR` and `G_SETBLENDCOLOR` went in with it, two shifts each. The fog
colour is not used while fog is off — and fog is off because the *coefficient* is
wrong, not because the colour was missing — but whoever fixes the coefficient
will now find the colour already there instead of a second fault behind the
first.

### What is left, and where it points

The large object at the centre of the menu is black under `DKR_FORCE_COMBINE=texel`
as well as under the real combiner, so it is the texel and not the shade. And yet:

```
textures: uploaded=1067 reused=15 refused-tmu=0 unknown-format=0 outside-rdram=0
refusal-detail: aspect=0 size=0 slots=0 tmu-memory=0
texels: black=0 with-content=34
```

Every texture uploads, none is refused, none is black. So the object samples a
black *part* of a texture that has content — which points at **where** we sample
from, not at what we sampled. DKR's own texture-loading base lives in
`state.texture_offset`, `texture_shift` and `texture_count`, set by a command
whose `w1` this decoder once read as two 16-bit s and t offsets before the
neighbouring port corrected it. That machinery is decoded and, like
`dkr_cc_lookup` before it, may well not be read by anything.

## The texture-offset indirection, implemented and inert here — 22 August 2026

`TextureOffset` (`0x02`) sets `texture_offset` to an RDRAM address, and that
address is **a table of sixteen-bit shifts** indexed by `texture_count`. Each
`SetTextureImage` reads its own entry and adds it to the image address; each
`LoadBlock` advances the index, or abandons the table when the shift does not
land on a block boundary. The game packs many textures into one region and walks
them this way. `f3ddkr_rt64.cpp` carries all of it.

The base was decoded on 18 August and then **read by nobody**: `texture_offset`
was assigned, `texture_shift` and `texture_count` only ever zeroed. So every
surface drawn after a `TextureOffset` sampled the head of the region instead of
its own texture — which is exactly the shape of the defect being chased, a
texture that uploads, carries content, and comes out black.

It is now implemented, and on the menu's lists it **never fires**:

```
tex-shifts=0 offset-dropped=0 tiles=35
```

`texture_offset` is zero throughout, the command not being emitted here. So the
mechanism is right against the reference and is not this defect. It will matter
where the game uses it, and the counters will say when.

> **Third value in a week that was decoded and reached no one** — `dkr_cc_lookup`'s
> answer, `state.billboard`, and now this. That is a habit worth naming: **a field
> that is written and never read is a defect, whatever the comment above it
> says.** `grep` for the assignment and then for a use; the second search is the
> one nobody runs.

### And a counter of my own to distrust

`st inside=837 outside=432` measures whether a corner's `s` and `t` fall in
[0,1] — but `tex_scale_s` normalises over the texture's **larger** side, since
that is Glide's convention. On a 64x32 texture a `t` of 1.0 means sixty-four
texels, twice the height, so "outside [0,1]" is not the question I meant to ask
for anything that is not square. The figure is not wrong; it is not measuring
what its name claims.

## The black object is not one thing, and the framing was wrong — 22 August 2026

Three candidates eliminated and one instrument to distrust.

**`texEnabled` is decoded.** `gSPPolygon` packs it at bit 16 beside the triangle
count, `TRIN_DISABLE_TEXTURE` against `TRIN_ENABLE_TEXTURE`, and this decoder read
the count and dropped the flag. An untextured batch carries no texture
coordinates, so texturing it anyway samples texel (0,0) everywhere. Honouring the
flag changed nothing on screen: the menu's batches all declare themselves
textured.

**The tile origin is innocent.** `uls`/`ult` are the tile's upper-left corner
inside the texture image, and `cmd_set_tile_size` subtracted them to get a size
and never used them as an origin. Measured: `tile=(0,0)` on every triangle
covering the centre.

**The coordinates really are zero**, and the reference reads them from the same
offsets — `s` at 4, 8, 12 and `t` at 6, 10, 14 of the sixteen-byte `Triangle`.
So the N64 samples one texel for those faces too, and a flat face is what the
game asks for.

And the pixels are **not uniformly black**. At x = 280 the object is
`(0, 0, 189)`, a strong blue, which is exactly texel `801C` expanded — so that
triangle samples correctly and paints correctly. Of 116,027 black pixels, 72,703
lie to the right of x = 440, which is past the edge of the sky quad: unpainted
background, not a defect of the object at all. The object itself accounts for
38,394, in flat faces of which several carry their proper blue, gold and green.

> **"The object is black" was never a measurement.** It was a description of a
> picture, and it survived four runs because nothing forced it to be either true
> or false of a specific pixel. The moment one pixel was named, it came apart:
> the centre is black, x = 280 is blue, and the right-hand third is not the
> object.

### And the newest instrument is already suspect

`center_texel0` reads `c->texels[0]` — the decoder's **staging buffer**, which
holds the last texture *converted*, not the one *bound*. A triangle drawing on a
cache hit leaves an older texture in it, and nothing in the printed line says
which case one is looking at. It did rule out `uls`/`ult`, and it is flagged in
place rather than trusted.

## The dark faces: five more eliminations, and no cause — 22 August 2026

A single triangle covers the probe at (560, 240) — `DKR_PROBE=x,y` now moves the
paint stack off the screen centre, so no ordering and no depth contest can be
invoked. It is opaque, `combine=3`, its shade is white, and the pixel is black.

Eliminated since, each by a measurement:

- **The catalogue's setups.** `DKR_FORCE_COMBINE=texel` disables the recipe and
  uses the four-mode shorthand: still black, 110,476 pixels against 116,027.
- **Mostly-black textures.** `textures_black` asks whether *every* texel is zero
  and `textures_uniform` whether they are all equal; neither asks whether a
  texture is black where a surface samples. A third counter says
  `mostly-black=0` — not one texture in the frame has three quarters of its
  texels at RGB zero.
- **A stale texture binding.** The render-state block names a texture by handle,
  and the handle survives a re-upload while the TMU address does not — so a
  short-circuited `gl_set_state` could leave `grTexSource` on the old address.
  With 1,067 uploads against 15 reuses in a run, that is the worst case for it.
  An upload now invalidates the cache. No change on screen, and the fix is kept:
  it is the same discipline `gl_fill_rect` was given, and the defect it closes is
  the one the descriptor table was already repaired for once.

What remains unexplained is exact and small: one opaque triangle, one white
shade, one texture that is neither black nor uniform, and a black pixel.

> The one reading not yet excluded is that `center_texel0` has been lying the
> whole time — it reads the decoder's staging buffer, which holds the last
> texture *converted* rather than the one *bound*, and it is on its word that the
> sampled texel "is a blue". Until that is read from the bound texture instead,
> the blue is a claim and not a measurement, and this section rests on it.

## The instrument had lost a divide, and two turns rested on it — 22 August 2026

`st=(0,0) (0,0) (0,0)` was not a measurement of the geometry. It was a
measurement of my arithmetic.

`dkr_clip_project` writes `s * 256 * oow` into `SOW` and `oow` into the TMU's own
slot; Glide recovers `s` by dividing one by the other. The probe printed
`SOW * big / 256` and left the division out, so every figure came back **a
hundred and sixty times too small** — `oow` being a constant 1/160 on the menu's
orthographic lists — and every texel count under one printed as zero.

With the divide restored, the same triangle reads:

```
tex 64x32 fmt=0 st=(63,31) (0,31) (64,0) texel0=A65F tile=(0,0)
                dark=0/2048 mean=21/31
```

Coordinates spanning the texture exactly, not a single black texel in it, and a
mean luminance of 21 out of 31 — a bright texture, sampled across its whole
extent, by a triangle of 210,227 pixels with a white shade and opaque blending.
The pixel is black.

### What is withdrawn

Everything built on the zeros, across two turns:

- that the faces were **flat by design**, each sampling one texel;
- that `texEnabled` might be stripping texture coordinates — the flag is decoded
  now and correct against the reference, and it was never the question;
- that the N64 would sample one texel too, so the black was the game's own.

None of that was measured. It followed from an instrument that had lost a
multiplication, and it survived because every consequence of it was consistent
with every other.

> **Three counters for one question, and each answered a narrower one than its
> name.** `textures_black` — is *every* texel zero? No. `textures_uniform` — are
> they all equal? No. `mostly-black` — are three quarters of them zero? No.
> `dark=0/2048` — is any of them exactly zero? No. And the texture is bright, as
> a **mean** said in one figure the first time it was asked. A count against a
> threshold can always answer no; a mean has nothing to tune.

### Where this stands

One triangle, one bright texture sampled over its full extent, white shade,
opaque blend, no depth contest — and a black pixel. The only thing that has ever
made this geometry paint is `DKR_FORCE_STATE`, which pushes a block that
*differs* so the card is reprogrammed in full; pushing the same block after
invalidating the cache does not. The values in the block matter and every field
of it has been eliminated individually.

The combiner enumeration values were checked against Glide 2.x on this pass and
are right. What has never been validated is the `grTexCombine` pair used when a
texture is bound, which the untextured path does not touch — and the untextured
path is the one that paints.

## Crossing the two switches: a table that constrains the answer — 22 August 2026

Neither switch alone reveals the geometry; together they do. Frame 59, painted
pixels of 307,200:

| combiner | state pushed | painted |
|---|---|---:|
| as the game asks (`combine=3`, textured) | when it changes | **1,769** |
| as the game asks | before every triangle (`DKR_FORCE_STATE=3`) | **2,792** |
| forced to shade, untextured | when it changes | **2,800** |
| forced to shade, untextured | before every triangle | **112,081** |

Two facts, and they are separate:

1. **The textured combiner never paints**, however often it is pushed. Pushing
   the game's own block before every triangle moves 1,769 to 2,792, which is
   noise beside 112,081.
2. **The untextured combiner paints only if it is re-pushed constantly.**
   Programmed once when the state changes, it gives 2,800; programmed before
   every triangle, 112,081 — a factor of forty for writing the same registers
   again.

The second is the stranger of the two, and it is not explained by anything in
this port's code: between two state changes nothing here touches
`grColorCombine`. The invalidation added to `gl_texture_upload` cannot be it —
the decoder already sets `state_dirty` after an upload, so the block is pushed
there anyway.

What is now excluded, each by its own run: depth, culling, the guard band, `oow`,
the vertex colour, the fog (which *was* the whole of the earlier black frames),
the catalogue's setups, the wrap modes, the tile origin, `texEnabled`, the
texture-offset indirection, a stale binding, and the texture's content — the
triangle at the probe samples a texture of mean luminance 21/31 across its full
extent, with a white shade and opaque blending, and paints black.

### Correction, same day: that table compared two different quantities

`differing` counts pixels **unlike the corner**, and the corner is not the same
colour in those runs. Three of the four rows carried `DKR_PAINT_WHITE=1` and one
did not, so the figures were not measuring the same thing at all — and where the
frame came back white, `differing` was counting the pixels that had *not* been
painted.

Read properly, with `distinct=2` in every white-forced run so the arithmetic is
exact:

| combiner | state pushed | corner | painted |
|---|---|---|---:|
| shade, texture bound | when it changes | black | **2,769** |
| the game's own | before every triangle | black | **2,792** |
| shade, texture bound | before every triangle | white | **193,919** |
| plain block (shade, no texture) | before every triangle | white | **193,715** |

The direction survives and the magnitude changes: a factor of seventy, not
forty. What also changes is the shape of the rule. It is not "the textured
combiner never paints" — the third row has a texture bound. It is:

> **`DKR_COMBINE_SHADE`, pushed before every triangle, paints. Every other
> combination does not.**

And the first row of the earlier table should never have been in it: taken from a
run without `DKR_PAINT_WHITE`, against a sky-coloured corner, its 1,769 was not a
count of painted pixels in any sense.

### And the canary refutes the decay reading

`DKR_CANARY=2` draws the two probe triangles **without pushing any state**,
inheriting whatever the list left on the card. Both paint, in pure white:
6,330 and 8,688 pixels in their boxes. So the state a list leaves behind is
perfectly drawable, and "the combiner decays between draws" — which the factor of
seventy seemed to demand — is wrong.

Two measurements that cannot both be accommodated by any story about the card
losing state. The next step is not another switch; it is to stop reading
`differing` as a coverage figure, which is the second time in two days that a
counter has been read as answering a question it was not asked.

## There was no factor of seventy, and no defect to find — 23 August 2026

A counter that answers the question actually asked: `painted`, the number of
pixels that are not the clear colour. The frame is cleared to black and the
game's first fill is black, so a non-black pixel is a pixel something drew,
whatever the corner turns out to be.

The build with **no switches at all**, frame 59:

```
corner=94BAFF distinct=597 differing=306135 painted=193905/307200
```

**193,905 of 307,200 — sixty-three per cent.** That is the same figure the
`DKR_FORCE_STATE` runs produced (193,919 and 193,715), which I had been reading
as seventy times better than the baseline. The baseline was never worse. It was
being measured against a sky-coloured corner while the others were measured
against white.

So the sequence that began with "429,306 pixels of on-screen surface handed over
and 2,857 painted" was comparing a bounded area estimate against a corner-relative
count, and every step after it inherited the error.

### What is left of it

Two things survive, and they are smaller than the story they were part of.

- **The port paints sixty-three per cent of the frame.** The rest is black. Some
  of it is background past the edge of the sky quad — 72,703 pixels beyond
  x = 440, measured — and the remainder is geometry whose colour is dark. Whether
  that is right is a question for E09-S02 and a reference capture, not a
  rendering failure to hunt.
- **`DKR_FORCE_COMBINE=shade` with `DKR_PAINT_WHITE` paints 2,769 pixels**, where
  the same geometry with the real combiner covers 193,905. Every vertex is opaque
  white and the combiner is the vertex colour alone, so every drawn pixel should
  be white. That anomaly is real, and it belongs to the diagnostic switches
  rather than to the game's rendering.

> **Three counters in two days, each read as answering a question it was not
> asked**: the probe that had lost its perspective divide, `differing` read as
> coverage, and `textures_black` read as "is it dark". The common shape is a
> figure whose name describes the intent and whose arithmetic describes something
> narrower. The habit that catches it is the one this file keeps rediscovering —
> ask what the number would print if the thing were working perfectly, and check
> that it differs from what it prints now.

## Which switch was lying, and the rule that follows — 23 August 2026

`DKR_PAINT_WHITE=1` alone, real combiner, frame 59: **painted=191,153**, against
the baseline's 193,905. Sound. With every vertex forced opaque white and the
combiner reading `texel x shade`, the output is the texel and the coverage is the
same — which is exactly what it should be, and it is the first time that switch
has been checked against a known answer rather than used to produce one.

`DKR_FORCE_COMBINE=shade` added on top: **2,769**. The combiner then reads the
vertex colour alone, every vertex is white, and the coverage should be unchanged.
It falls by seventy.

So the broken instrument is `DKR_FORCE_COMBINE=shade`, and it is not a small
matter: it was used to establish "the loss is geometric, not in the textures",
and every run that carried it has to be read as suspect. Those conclusions were
already withdrawn with `differing`; this says they cannot simply be re-derived
from the same runs either.

### The rule these three days have earned

Every diagnostic switch in this port now has to be **checked against an outcome
known in advance** before its answer is believed. The canary does that for the
card — two triangles whose painted area is 4,950 pixels by construction, drawn
into the game's own frame. `DKR_PAINT_WHITE` has now had the same treatment, by
running it alone where its expected effect is "no change in coverage".

The switches that have not had it: `DKR_FORCE_COMBINE`, whose shade mode is
demonstrably wrong; `DKR_FLATTEN_W`; `DKR_NEUTRAL`, whose full mask was checked
against the plain block but whose individual bits were not; `DKR_FORCE_STATE`;
`DKR_NO_DEPTH`; `DKR_SCISSOR`.

> An instrument that has never been shown a case whose answer is known is not an
> instrument. It is a second hypothesis, entangled with the first, and this file
> now records four occasions where the two were mistaken for one.

## Checking a switch against a known answer, and what it found — 23 August 2026

The rule applied to itself first. Forcing `texel_shade_a` — the mode the game
already uses on 230 of its 238 triangles — must change nothing, and it does not:
**193,931 painted against the baseline's 193,905**. So the force *mechanism* is
sound and only the `shade` *value* collapsed the frame.

The difference from the canary, which uses the same combiner and paints its 4,950
pixels exactly, is the texture handle. `apply_combine` returns early for
`DKR_COMBINE_SHADE` without calling `bind_texture`, so `grTexSource` is never
re-issued and keeps pointing at an address the TMU allocator may since have freed
and reassigned — while the block still names a texture. The switch was asking for
something incoherent: *read the vertex colour alone* and *a texture is bound*, in
the same state.

Clearing the handle restores it: **193,041**, the baseline.

### And this is not only the switch's business

The decoder can select `DKR_COMBINE_SHADE` on its own — `dkr_rdp_to_render_state`
does exactly that for any combiner that reads no texel. The frame that made this
visible simply had none: `emitted combine=0/2/0/230/6`, zero in the shade column.
So the rule is now in the translation rather than in the switch: **the texture
handle is laid down only for the modes that read a texel.** The baseline is
unchanged by it — 191,176 against 193,905, inside the run-to-run spread of the
animation — and a latent way of blanking a surface is gone.

The mechanism behind the card's refusal is a guess and is written down as one: a
texture source left outside texture memory, which the pixel pipeline fetches
whether or not the combiner reads it. What is measured is the rule.

> This is the first time in three days that checking an instrument against a
> known answer produced a **fix to the port** rather than a caveat on a
> measurement. That is worth noticing: the incoherent state the switch was
> asking for was one the decoder could ask for too.
