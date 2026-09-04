# Visual testing: capture, replay, compare

E09-S02. This describes how an image produced by the port is checked against a
reference, and it exists because the obvious method does not work.

## The problem this solves

The port produces images. Comparing two of them tells you nothing unless they
were produced from the same input, and **a running game is not the same input
twice**. Three questions about the image went unanswered in the week of 25 August
2026 for exactly that reason:

- did repacking the constant colour register change the sky?
- did the texture residency cache change anything?
- is the second texture unit what stopped the character names doubling?

Each failed the same way: the scene animates, two runs do not reach a display
list at the same moment of it, and comparing their pixels measures the animation.
The third was written down in `docs/research/win95-game-render.md` as
**unattributed** rather than claimed, which is the honest thing to do and not a
substitute for being able to answer it.

The answer is to freeze the input.

## The capture

A capture is what the decoder reads, written to a file: the address the display
list starts at, and RDRAM.

    D:\> set DKR_CAPTURE_LIST=400
    D:\> DKRR.EXE D:\DKR.Z64

The game runs normally and writes `D:\CAP0400.BIN` when it reaches display list
number 400, then carries on. The file names the list it holds.

**Several in one run**, separated by commas, up to eight:

    D:\> set DKR_CAPTURE_LIST=50,150,300,450,700,1000

That is not a convenience. A capture costs a boot, a launch and a wait; the
corpus below wants title, menus, a lap of each level, cutscenes, split screen and
results, and taking them one boot at a time is a day. Firing on several indices
in one run costs nothing but disk — and the transfer disk holds about fifty
captures, which is the bound the code enforces rather than filling the volume and
reporting it as a write failure at the least useful moment.

Each index fires **at or after** its list, once. On an exact match a run that
stopped short would simply produce nothing, and say nothing about why.

### The format

A 32-byte header, then the RDRAM image. Every field is a little-endian 32-bit
word, that being both machines' order — the capture is written on the Windows 95
machine and read on the development machine, and a format needing a byte swap
would be a second thing to get wrong.

| offset | field | meaning |
|---|---|---|
| 0 | `magic` | `0x314B5044`, the bytes `DPK1` |
| 4 | `version` | format version, currently 1 |
| 8 | `data_ptr` | the display list's start address, masked to 24 bits |
| 12 | `rdram_bytes` | how much RDRAM follows |
| 16 | `rdram_native` | 1 if the image is in librecomp's XOR-3 interleaved layout |
| 20 | `list_index` | which display list of the run this was |
| 24 | `screen_w` | the resolution it was captured at |
| 28 | `screen_h` | |

Then `rdram_bytes` bytes of RDRAM.

**The whole of RDRAM, eight mebibytes a capture.** The decoder reads at addresses
the list itself computes — vertices, matrices, textures, nested lists — so there
is no knowing in advance which bytes matter without running it. Storing all of
them is the cheap side of the trade: a capture missing a byte the list needs
replays a *different* image, which is the one failure the format exists to rule
out.

`rdram_native` is a property of the capture and not of the reader. Replaying a
native image as though it were plain gives plausible opcodes at absurd addresses,
which this project has already spent a day on.

The reader refuses a bad file with a message naming **which** check failed. Wrong
magic, wrong version and a truncated transfer are three problems with three
different answers, and `capture: invalid` would send the reader to the wrong one.

**The writer commits the file to the disk before closing it**, and that is not
belt and braces. On 4 September 2026 a run armed with six captures produced one
file: the other five had been written and closed, and were lost with Windows 95's
write-behind cache when the emulator was stopped. On a machine whose runs
normally end in a crash or a kill, `fclose` promises nothing — a capture that is
not on the platter is not a capture. The frame and comparison images are written
the same way, for the same reason.

## The replay

One program, `tools/render/replay.c`, built for both machines. The decoder, the
transform and the clipper are the same source on both paths; only the backend
differs. Two programs would let a divergence hide in the difference between them,
and it would be charged to the card.

### On the development machine

    tools/render/build-host-tools.sh
    build/render-tools/replay CAP0400.BIN out.bmp

This renders through E04-S08's software rasteriser — the oracle. It implements
the RDP's combiner without Glide's constraints, so when its image is right and
the card's is wrong, the decoder is out of the question and the fault is in the
backend. That isolation is what E05-S03 and E05-S04 were missing.

The replay is deterministic, and that was checked rather than assumed: two runs
of the same capture produce byte-identical files.

### On the Windows 95 machine

    D:\> REPLAY.EXE --both --log D:\REPLAY.TXT D:\CAP0400.BIN

Renders the same capture twice, once through the oracle and once through the
Voodoo, reads the card's frame buffer back, and compares. It writes:

| file | what it is |
|---|---|
| `D:\RPLSOFT.BMP` | the oracle's image |
| `D:\RPLCARD.BMP` | the card's image |
| `D:\RPLDIFF.BMP` | where they disagree |
| `D:\REPLAY.TXT` | the counts and the metrics |

`--single-tmu` forces the card down the multipass path, which is what a
single-TMU board takes and what **79.7 %** of the game's triangles take on this
one. Comparing the two runs isolates the second texture unit — the question that
could not be answered before this existed.

The log is written a line at a time and flushed: a run that faults still leaves
behind what it had measured, which is usually the interesting part.

## The comparison

    build/render-tools/compare reference.bmp candidate.bmp [difference.bmp]

The metric lives in `platform/render/imagecmp.c` and is compiled into both the
host tool and the target's `REPLAY.EXE` and `COMPARE.EXE`. One copy, deliberately:
a second would drift, and the drift would read as the card disagreeing with the
oracle.

**The reference is the first argument, and that is not arbitrary.** It is
quantised to 565 before anything is subtracted, because the card cannot store
more; doing it the other way round would count the oracle's extra bits as the
card's error.

Three gaps between the two renderings are structural and signal nothing:

- **the 565 quantisation** — five bits of red and blue, six of green, against the
  rasteriser's eight;
- **depth**, z over [0,1] on one side and an encoded w buffer on the other: the
  two order the same way and do not quantise the same way;
- **edges**, where a fill rule differing by half a pixel moves a whole column.

So edge pixels are counted in their own column rather than forgiven, and a pixel
counts as frankly different past a gap of 24 per channel. A single tight
threshold would fail every run for a good reason, and a measurement one stops
reading is worse than no measurement.

What comes out:

    painted surface: reference N, candidate M
    frankly different pixels: N out of 307200
    of which on an edge, counted separately: N
    worst per-channel gap over the whole image: N

The painted surface is the most robust of the four: it depends on neither
quantisation nor edges, and wrong geometry on either side moves it immediately.

**The difference map is not decoration.** Counting divergent pixels says how
many; only a map says *where*, and where is what distinguishes a column shifted
by a fill rule from a texture fetched wrong. It draws agreement as the reference
darkened, so the scene stays recognisable underneath, and disagreement in red
scaled from the threshold upwards.

### The floor of the metric

Comparing the oracle's own image against itself gives a worst gap of **7** and a
painted surface differing by one pixel. That is not a defect: the reference side
is quantised to 565 and the candidate side is not, so the self-comparison
measures the quantisation and nothing else. It is the floor below which no
comparison can go, and it is worth knowing before reading any other number.

## Asking what drew a pixel

    build/render-tools/replay --probe X,Y capture.bin out.bmp

The oracle rasterises, so at the moment it writes a pixel it holds the state that
asked for it. `--probe` records **every** draw that writes the chosen pixel, in
order: what was underneath, what was left, and the state.

    probe (318,437): 7 draw(s):
       3  0xFEDF00 -> 0xCBB22C  TEX*CONST const=0xFF0000FF ascale=51  ...
       4  0xCBB22C -> 0x796A73  TEX*CONST const=0xFF00FFFF ascale=102 ...
       ...

The coordinates are the frame buffer's — x from the left, y from the **top** —
which is what `compare` and `REPLAY.EXE` print in "worst gap off-edge at (x,y)",
so a divergence can be pasted straight into `--probe`.

The whole history and not merely the last writer: the first pixel this was pointed
at had been painted seven times, and the answer was in the ramp across the seven.

## The corpus, and checking it automatically

    tools/render/check-corpus.sh <corpus-dir>
    tools/render/check-corpus.sh <corpus-dir> --accept

**The corpus lives outside the repository.** A capture is eight mebibytes and has
to be; a dozen scenes is a hundred megabytes, which does not belong in git. What
is versioned is the script. Put the `.BIN` files in a directory of your own and
point the script at it.

For each capture it replays through the oracle and checks two things, in this
order:

1. **The decoder's counts** — commands, triangles, emitted, rejects, textures,
   against `<name>.counts`. First, because a count that moved is a decoder or
   determinism change, and an image difference downstream of one says nothing
   about rendering. It is also the check that works before any reference image
   exists.
2. **The image**, against `<name>.bmp`, with the metric above.

`--accept` writes what came out as the reference. Use it to start a corpus, and
to adopt a change you have looked at.

**The threshold is per scene**, in `<name>.threshold`: `<max-gap> <ppm>` on one
line, defaulting to 16 and 10000. Per scene because scenes are not equally close —
one that is all flat colour agrees to the bit, one full of gradients does not, and
a single threshold either passes the second or fails the first for ever.

A failure leaves `<name>.diff.bmp` beside the capture. A failing run one cannot
look at is a failing run one starts ignoring.

## Where the numbers so far come from

- `docs/research/win95-oracle-vs-card.md` — the synthetic scene, four triangles,
  0 divergent pixels out of 307,200, after three defects **all of them in the
  oracle**.
- `docs/research/win95-oracle-vs-card-capture.md` — a real frame of the game.
- `docs/research/win95-alpha-scale.md` — what the first real divergence turned out
  to be, and the probe that named it.
