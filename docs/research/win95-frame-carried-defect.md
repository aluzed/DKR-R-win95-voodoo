# A defect the card carries from one frame to the next

Opened and closed 30 September 2026. **Found and fixed**: the fill rectangle
drew with the last triangle's state (below, "The cause").

## What is seen

In a race, driven by hand (`scripts/Drive-To-Race.sh`), large surfaces near the
camera come out black on the card: the foot of the canyon wall on Ancient Lake,
the water in front of the dinosaur head. Seen in both snapshot modes, with the
whole RDRAM copy and with patch 0057's shorter one, and with a build from before
the E08 texture memos (`8bb246f^`, 24 September). It is **not** a regression of
the E08 work.

## How to catch it

`DKR_CAPTURE_KEY=1` arms F9 (`=<n>` for n presses). A capture now also writes
`D:\KFnnnn.BMP` beside `D:\CKEYnnnn.BIN` (first named `KEYFRAME.BMP`): the
card's own frame of the captured list, as the running renderer drew it. The pair
-- the capture replayed cold, and the frame the game showed -- is what separates
a defect in the list from a defect in what the renderer carried into it.

A hunt that drives until a fifth of the lower screen is black and presses F9 took
one to six cycles each time (`CKEY1516`, `CKEY1547`, `CKEY1070`, `CKEY1746`).

## What is established

| Test | Result |
|---|---|
| `CKEY1547` replayed cold, oracle | correct (the wall shaded brown) |
| the same, cold, on the card (`REPLAY.EXE --card`) | correct |
| the game's own frame of that list (`KEYFRAME.BMP`) | black wedge |
| `CKEY1746` on the card, `--frames 3` | frame 1 correct, **frame 3 black**, as in the game |
| the oracle, `--frames 3` | frame 3 identical to frame 1 |

So a replay of one list, drawn three times in a row, reproduces it on the card
and not in the oracle: the cause is state the **card** keeps from one frame to
the next. Ruled out, each on `--frames 3` on the card unless stated:

| Switch | Still black? | So it is not |
|---|---|---|
| `DKR_NO_STATE_SHADOW=1` -- every argument-level state write made | yes | the E08 state shadows |
| `DKR_FORGET_AT_FRAME=1` -- every remembered state dropped per frame | yes | any remembered state in the backend |
| `--no-texcache` -- every texture converted and uploaded | yes | texture residency or the memos |
| `--single-tmu` | yes | the second texture unit |
| `--no-multipass` | yes | the extra passes |
| `--no-stats` (oracle and card) | identical | the decoder's statistics switch |
| `DKR_NO_DEPTH=1`, in the game | yes | the depth test |
| the `8bb246f^` build | yes | anything E08 changed |

## What the pixels say

Frame 1 against frame 3 of `CKEY1746`, 640x480, sampled every fourth pixel: 46%
of the samples differ, in two bands -- the top 200 rows (sky) and the bottom 120
(water).

| Pixel | frame 1 | frame 3 |
|---|---|---|
| sky, (320,20) | 57, 255, 255 | 57, 109, 231 |
| sky, (100,60) | 57, 255, 255 | 57, 117, 231 |
| water, (50,440) | 0, 154, 214 | 0, 0, 0 |
| sand, (600,450) | 255, 251, 74 | unchanged |

The sky of frame 1 is saturated where frame 3's is a gradient, which for a
while made "frame 1 is right" look unsettled. It is settled below: both colours
are the fill rectangle's, frame 1's drawn as the fill colour, frame 3's through
the previous frame's last combiner.

## The cause

The probe settled it. At the water pixel (50,440) the oracle reports that no
triangle drew it: the blue is the **fill rectangle** the list lays down first,
in the RDP's fill cycle. `gl_fill_rect` drew that rectangle as two triangles
under `b.current` -- the block of the last triangle drawn -- with only its depth
turned off. The first frame after the card opens starts from a zeroed block:
shade only, no texture, opaque, so the rectangle comes out as its colour. Every
later frame starts from the end of the one before, and the background was drawn
with that triangle's combiner, textures and blend -- through an alpha of zero,
in this case, which is nothing at all.

None of the switches above touched it: they drop what the backend *remembers*,
and this was what the backend *programmed*, correctly, from the wrong block.

The rectangle now has its own state: vertex colour, no texture, no alpha test,
no fog, no depth, opaque -- what the RDP's fill cycle writes, and what the
oracle's `sw_fill_rect` does. (A first version blended when the fill colour's
alpha was below 255; the fill colour's alpha is often zero, and the background
disappeared on every frame.) Fades are not affected: a rectangle outside the
fill cycle never reaches `fill_rect`, the decoder draws it with its own state
(`blend_rect_emit`).

Verified on the card: `CKEY1746` and `CAP0400` replayed three times give frame 3
identical to frame 1 (`CAP0400`: 0 pixels differ), and the twelve captures of
the card corpus, drawn once, are **byte-identical** to the same run before the
fix. In the game, the black-surface hunt that found a case in one to six cycles
ran 21 cycles for one screen past its threshold -- a kart's dark underside --
and there the card and the oracle agree, except for one thing below.

## Then: the skid marks

In that frame the tyre tracks are red on the card and a dark translucent green
in the oracle. They were red before the fix too, so this is another defect.

**Diagnosed and fixed the same evening.** The oracle's probe at (62,470) of `CKEY2370` names the
track's draw: recipe 14, `G_CC_BLENDI_ENV_ALPHA` + `G_CC_MODULATEIA_PRIM2`,
two-cycle, catalogued `MULTIPASS`, primitive `0x203F3F3F`, alpha-blended.

    cycle 1   rgb (ENV - SHADE) x ENV_ALPHA + SHADE   env alpha 0 -> SHADE
    cycle 2   rgb COMBINED x PRIMITIVE                -> shade x 0x3F/255
              a   COMBINED x PRIMITIVE_ALPHA          -> shade alpha x 0x20/255

The card draws the first cycle -- the vertex colour, red here -- and drops the
second: `pass2_kind` recognises only the lerp toward the environment colour as
a second pass, and a modulation by the primitive is neither of its two forms. So
the track comes out as its raw vertex colour at its raw alpha, where the RDP
darkens it to a quarter and makes it an eighth opaque.

The shape is one Glide stage can hold when the first cycle reduces to the shade
(environment alpha zero): colour `CONSTANT x ITERATED`, alpha `CONSTANT_ALPHA x
ITERATED_ALPHA`, with the primitive in the constant register. `gl_set_state`
now programs exactly that when a `MULTIPASS` entry has this shape and the
environment's alpha is zero (`shade_times_prim`). On the card, `CKEY2370`'s
tracks come out dark and translucent like the oracle's -- (24,150,16) against
(29,159,33) at the probed pixel, where they were (206,8,57) -- and the twelve
captures of the card corpus are byte-identical to the build before: none of
them draws with this recipe.

## Then: a wall striped in play and smooth in every replay

With both fixes in, one more pair disagreed: `CKEY1835`, the canyon wall
striped vertically in the game's own frame and smooth in the oracle and in the
card's replay -- once or three times, 0 pixels apart. Not the statistics switch
either (`--no-stats`, 0 pixels).

Two defects, one behind the other:

- **The texture key named a place, not a picture.** Address, format, size and
  dimensions: when the game loads another texture of the same shape at the
  same address -- a menu's, then a track's -- the card went on drawing the
  first. A replay converts every texture of its one list afresh and cannot see
  it. The key now carries eight words sampled across the image in RDRAM:
  eight reads per texture change.
- **`DKR_NO_TEXCACHE` did not rule the cache out**, which is why the in-game
  hunt with it came back black too. The decoder converts and uploads, and the
  allocator answers a resident key with its old address and no transfer --
  right for a cache, wrong for an upload. The backend now downloads the texels
  whenever an upload finds its key resident. The allocator's contract, which
  `test_tmu.c` checks, is unchanged.

Checked: the 21 corpus scenes in the oracle and the 12 on the card are
byte-identical to before; `CKEY1746` over three frames stays right; and a
capture taken at random in a race (`CKEY1819`) shows the game's frame and the
oracle's agreeing, the wall smooth.

What the three fixes cost, the attract mode over its steady window (normal
mode, `none`, idle meter on), `09c5022` against `81d72ae`: render per list
5.64 to 5.72 ms at the median and 6.89 to 7.01 ms on average; the frame is
34.65 against 34.57 ms. A tenth of a millisecond, for textures that are now
the ones the list names -- some of it transfers that should always have
happened.

## Sampling play for more of the same (30 September 2026, evening)

`DKR_CAPTURE_KEY=<n>` now allows n presses in a run, one capture and one card
frame (`KFnnnn.BMP`) each, at least thirty lists apart -- a single press used
to arrive as a burst on consecutive lists and filled the transfer disk's root
directory in one run. A pass presses F9 at the attract sequence, PLAYER
SELECT, GAME SELECT, the track choice, the race start and nine moments of the
race, then replays every capture in the oracle and compares it with the card's
frame (`tools/render/compare`, the project's metric).

With `DKR_RDRAM_SNAPSHOT=none`, fourteen samples, frankly different pixels per
million: 42, 826, 338, 296, 524, 540, 120, 22, 58, 61, 4016, 3225, 1927, 1227.
The worst, list 2102 in front of the dinosaur head, differs at the edge of a
translucent mist and in texture detail: the ordinary distance between the card
and the oracle, not a defect of the kind above.

The adventure hub the same way, eight samples from the fly-in over the
dinosaur head to Timber's Island's village gate: 29, 1826, 1546, 520, 364, 244,
113 and 19 per million. The hub is drawn as the oracle draws it.

**Captures stall the default mode.** Writing eight megabytes to the transfer
disk holds the graphics thread for seconds. In the snapshot mode a graphics
task is outstanding meanwhile, DKR's scheduler watchdog counts the retraces,
and the game stopped drawing after the second capture of a pass. In `none` the
SP and DP edges are published before the drawing, nothing is outstanding, and
all fourteen captures went through. Sample in `none`.

## How the investigation went, for next time

Before the probe named the fill rectangle, this was the reasoning:
what survives a frame on the card and is not the backend's remembered state:
the colour and auxiliary buffers' contents, and Glide state the backend never
sets (21 entry points are programmed; everything else is whatever
`grSstWinOpen` left and whatever a call made since has changed). Both surfaces
are drawn with blends; a blend that reads the destination -- or its alpha, which
on a Voodoo 2 with a depth buffer is the auxiliary buffer -- would give exactly a
result that depends on the previous frame. The next test was to find which
recipes the sky and the water draw with -- and the probe answered that neither
is drawn by a triangle at all. The lesson: probe the pixel before theorising
about the state.

Tools added for this: `REPLAY.EXE --no-stats`, `--no-texcache`;
`DKR_NO_STATE_SHADOW`, `DKR_FORGET_AT_FRAME` (backend diagnostics, off by
default); `KFnnnn.BMP` beside every F9 capture.
