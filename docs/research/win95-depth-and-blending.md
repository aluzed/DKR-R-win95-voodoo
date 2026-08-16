# Depth, alpha test and blending

Measured on 14 August 2026 on the test machine, by
`tools/win95/witnesses/depth_probe.c` and `glide_state_probe.c`.

## Z or W: the measurement does not separate them

The ticket announces that "W offers a far better distribution of depth precision".
The measurement does not confirm it.

A scene built to be the worst case — two nearly coplanar surfaces, very far away,
within a race track's range (near plane 10, far plane 20000):

| Gap | Distance | W buffer | Z buffer |
|---|---|---|---|
| 2 ‰ | 5000 | 0 fighting pixels | 0 pixels |
| 2 ‰ | 15000 | 0 pixels | 0 pixels |
| 0.2 ‰ | 15000 | **307200 (all)** | **307200 (all)** |

Both buffers resolve two parts per thousand at any distance within the range, and
both give way entirely at two tenths of a part per thousand. **The wall is in the
same place.**

The saturation case was added on purpose: a comparison with no breaking point does
not say where the limit is, only that both candidates pass the easy cases. It is
that case which establishes that the result is not "both are perfect" but "both
have the same limit".

The choice therefore stays the W buffer, **for a reason that is not precision**:
`dkr_render_vertex` already carries `oow = 1/w`, which Glide consumes as it stands
in W mode. Z mode reads `ooz` over [0, 65535] whereas the chain produces a depth
over [0, 1], and every vertex would have to be rescaled — hence copied, hence the
benefit of having mirrored `GrVertex` field for field would be lost.

## A measurement trap that nearly inverted the conclusion

The first measurement gave an impossible result: W failed on 11 % of the screen at
5000 and succeeded perfectly at 10000 and 15000. A depth buffer's precision
degrades with distance and never improves.

The anomaly struck only the **very first case measured**, which pointed at the
card's state on opening rather than at depth. Two frames thrown away before
measuring made it disappear, and the two buffers turned out to be equivalent.

**Measuring the first frame after opening a Glide context is measuring a machine
that has not finished settling in.** Without the doubt raised by the profile's
implausibility — better far away than close up — the conclusion would have been "Z
is superior to W", recorded and false.

## Agreement with the transformation chain

The normalised depth the projection produces runs from 0.50025 at 20 units to
0.99983 at 15000. Over that whole range, the near hides the far.

It is a point of agreement between two tickets, and a disagreement there would
produce a globally wrong sort — hence one piece of scenery passing in front of
another, a very visible defect that would be blamed on the decoder rather than on
the range.

## The render modes DKR uses

Recorded in the neighbouring port's source, by frequency of appearance:

| Family | Occurrences | What it is | Glide translation |
|---|---|---|---|
| `FOG_SHADE_A` | 74 | fog on iterated alpha | E05-S06 — **measured**, per-vertex factor |
| `XLU_SURF` | 78 | translucent | `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA` |
| `OPA_SURF` | 47 | opaque | `ONE` / `ZERO` |
| `TEX_EDGE` | 25 | cut-out by alpha threshold | alpha test — the vegetation |
| `DECAL` | 21 | surface stuck on, same depth | depth bias |
| `INTER` | 12 | interpenetrating surfaces | depth bias |
| `XLU_LINE_MOD` | 9 | translucent lines | — |
| `AA_` prefix | frequent | coverage-based anti-aliasing | **no equivalent** |

The `AA_` prefix is the only structural deviation. The RDP does anti-aliasing by
pixel coverage, integrated into the blender; Glide 2 offers nothing but
`grAADrawTriangle`, whose cost bears no relation. Those modes are therefore
rendered without anti-aliasing, which is a degradation visible on edges and **not
a colour error** — it does not accumulate and does not spread.

## What is already measured elsewhere

`glide_state_probe.c` confirmed on the card, the same day:

- opaque, alpha and additive blending, to within the quantisation;
- the alpha test, in both directions — 0 pixels below the threshold, 112000 above,
  that is exactly the triangle's analytical area;
- the scissor window, bounds inclusive on the left and exclusive on the right.

## What remains open

- **`grChromakeyMode`** against the alpha test: the ticket asks for cost and result
  to be compared. Not measured.
- **The depth bias** for the `DECAL` and `INTER` modes: `grDepthBiasLevel` exists,
  its useful value is set on a real scene.
- **The rendering order of translucent surfaces** is verified structurally — the
  decoder emits in display-list order and groups nothing — but not on a scene from
  the game.
- **Shadows, water, particles and reflections**: require the ROM.
