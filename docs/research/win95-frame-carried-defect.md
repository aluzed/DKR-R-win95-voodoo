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

`DKR_CAPTURE_KEY=1` arms F9. A capture now also writes `D:\KEYFRAME.BMP`: the
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

The sky of frame 1 is saturated where frame 3's is a gradient, so "frame 1 is
right" is not settled either: both frames may be wrong in opposite ways, and
the oracle agreeing with frame 1 says only that they take the same path.

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

## Still different: the skid marks

In that frame the tyre tracks are red on the card and a dark translucent green
in the oracle. They were red before the fix too, so this is another defect, in
whatever recipe draws the tracks. Not investigated.

## How the investigation went, for next time

Before the probe named the fill rectangle, this was the reasoning:
what survives a frame on the card and is not the backend's remembered state:
the colour and auxiliary buffers' contents, and Glide state the backend never
sets (21 entry points are programmed; everything else is whatever
`grSstWinOpen` left and whatever a call made since has changed). Both surfaces
are drawn with blends; a blend that reads the destination -- or its alpha, which
on a Voodoo 2 with a depth buffer is the auxiliary buffer -- would give exactly a
result that depends on the previous frame. The next test is to find which
recipes the sky and the water draw with, and whether either names a
destination factor.

Tools added for this: `REPLAY.EXE --no-stats`, `--no-texcache`;
`DKR_NO_STATE_SHADOW`, `DKR_FORGET_AT_FRAME` (backend diagnostics, off by
default); `KEYFRAME.BMP` beside every F9 capture.
