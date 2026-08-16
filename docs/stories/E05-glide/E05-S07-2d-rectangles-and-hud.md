# E05-S07 — 2D rectangles, HUD and the game's interface

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E05-S01, E05-S02, E04-S07 |
| **Blocks** | E09-S02 |

## Context

A large part of what the player sees is not 3D: the menus, the level-selection map,
the position counter, the timer, the balloon icons, the dialogue screens, the
cutscenes. All of that goes through textured rectangles and filled rectangles.

The N64 draws them with dedicated RDP commands — `FillRect` is among the current
decoder's handlers — which are expressed directly in screen coordinates, with no
transformation. Glide has no rectangle primitive: everything is drawn in triangles.
The conversion is simple, but it conceals two classic traps:

- **the half-texel offset**, which makes interface elements blurred or shifted by one
  pixel — a discreet, permanent defect, and a very visible one on text;
- **the fill rule**, which decides whether a rectangle's right and bottom edges are
  included. An error there produces one-pixel seams between adjacent elements,
  particularly visible on tiled backgrounds.

The interface is also what the player looks at longest, and what a defect is noticed
on immediately.

## Objective

To render all the game's 2D elements correctly, to the pixel.

## Scope

**In:** filled and textured rectangles, interface elements, pixel positioning.

**Out:** the port's configuration interface (E06-S05), which is not game rendering.

## Work

1. Translate the rectangle commands into pairs of triangles, in screen coordinates,
   without going through the transformation pipeline.
2. Set the half-texel offset. It is determined by experiment — draw a one-pixel grid
   on a contrasting background and check its alignment — not by reasoning.
3. Check the fill rule on adjacent rectangles: no seam, no overlap.
4. Deal with rectangles with texture coordinates, with their own sampling mode. The
   decoder's `TextureOffset` handler indicates that the microcode has its own handling
   of offsets: survey it rather than assume it.
5. Deal with filled rectangles, with no texture.
6. Check the rendering of the game's text, which is composed of small assembled
   textures and constitutes the most demanding test of pixel positioning.
7. Compare to the pixel against the reference on frozen screens: title screen, main
   menu, character selection, in-race HUD, results screen. The interface being static,
   the comparison can be exact there rather than tolerant — that is a rare opportunity
   in this project and it must be seized.
8. Check the HUD in split screen, where the rectangles are constrained by the scissor
   window (E04-S05).

## Acceptance criteria

- [x] Textured and filled rectangles are rendered, in screen coordinates and without
      going through the transformation pipeline.
- [x] Verified on a one-texel grid, and the result is **that no correction is
      necessary**: a zero offset aligns the 64 columns. The safe band runs from −0.5
      to +0.25 texel, the break falling exactly where the sampled point changes texel.
      Knowing that a quarter of a texel of margin remains on each side says that a
      small rounding error elsewhere in the chain will not tip the interface over.
- [x] Four rectangles edge to edge: 0 background pixels out of 200. With a negative
      control checking that the four colours are distinct, without which a single
      rectangle covering everything would pass.
- [ ] The game's text — **blocked by the ROM**. It is the most demanding trial of
      positioning, and the one-texel grid is only a substitute for it.
- [ ] Five reference screens — **blocked by the ROM**.
- [x] Exact to the pixel in the four cases measured: 153,600 painted for 153,600
      expected with two players, 76,800 for 76,800 with four.
- [x] Surveyed — **and it revealed an error in the decoder**. `w1` is an RDRAM
      address, the texture load base, and the command resets the offset and the count.
      Our decoder read it as two 16-bit `s` and `t` offsets. The error would have
      displaced patterns rather than made them disappear, and we would have looked on
      the texture-decoding side.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

A half-texel offset is the kind of defect one stops seeing after a few hours of
exposure, and that leaps to the eye of anyone discovering the port. Step 7's exact
comparison is what prevents us getting used to it.

## References

- `runtime-recomp/src/game/f3ddkr_rt64.hpp` — `FillRect`, `TextureOffset` handlers
- E04-S07 — decoding the interface textures
- E09-S02 — comparison harness
