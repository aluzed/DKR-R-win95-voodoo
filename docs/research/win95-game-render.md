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
