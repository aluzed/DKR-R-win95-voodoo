# Fog

Recorded in the decomp and measured on 14 August 2026 on the test machine, by
`tools/win95/witnesses/fog_probe.c`.

## The N64's model, recorded and not assumed

`gbi.h`'s `gSPFogPosition(min, max)` loads two values:

    multiplier = 128000 / (max - min)
    offset     = (500 - min) * 256 / (max - min)

The RSP derives from them a per-vertex factor which it stores in **the vertex
alpha**. The blender then applies it through `G_RM_FOG_SHADE_A`, defined as
`GBL_c1(G_BL_CLR_FOG, G_BL_A_SHADE, G_BL_CLR_IN, G_BL_1MA)`: blend the fog colour
with the pixel, in the proportion given by the vertex alpha.

The game calls `set_fog(index, near, far, r, g, b)` from the level header, and the
weather replaces it mid-race — `rain_fog()` recomputes near and far according to
the storm's intensity. The fog colour therefore comes from the game's state and is
never fixed at build time.

## Fog has no bit of its own

It is deduced from the blender's configuration: colour source `G_BL_CLR_FOG` in
position `m1a`, factor `G_BL_A_SHADE` in position `m1b`. Our decoder left that bit
hard-wired to zero, whereas `G_RM_FOG_SHADE_A` is **DKR's most frequent render
mode** — 74 occurrences in the source.

And the RDP's trap turns up again: the value 3 means `G_BL_CLR_FOG` in position
`m1a` but `G_BL_0` in position `m1b`. Reading both with the same dictionary would
declare fog where there is none. A trial checks that case explicitly.

## The route chosen: the per-vertex factor

The ticket asks for a decision between `GR_FOG_WITH_ITERATED_ALPHA` and the
64-entry table. The measurement decides without hesitation:

    green fog colour, red surface, alpha from 0 on the left to 255 on the right

    without fog : FF0000 everywhere - the vertex alpha changes nothing
    with        : DE1C00 on the left, 7B7D00 in the middle, 18DB00 on the right

The gradient is regular and the direction is the right one: alpha 255 means full
fog, which matches the N64's factor, itself increasing with distance. Getting the
direction wrong would give an **inverted** fog — opaque up close, clear far away —
spectacular, and easy to blame on the curve rather than on the direction.

The 64-entry table is therefore not built: it would bring nothing but an
approximation of a curve we already hold exactly, per vertex. The question of its
approximation error becomes moot.

## What the measurement revealed beyond the question asked

**The vertex alpha serves the fog and the transparency at the same time.**

Measured by enabling both at once, on a blue background:

    left  0x1804DE - the background's blue shows through: the surface is translucent
    right 0x18BE18 - green: full fog

Both uses work, and that is precisely the problem: they are **coupled**. A
translucent surface in fog draws its transparency and its fog proportion from the
same value, and one cannot set one without disturbing the other. The game uses 78
translucent modes against 74 fog modes: the meeting is certain.

### A consequence for E05-S03

E05-S03 proposed carrying the second constant colour in the vertex alpha, in order
to work around Glide's single constant register. **That way out conflicts with the
fog**, which already occupies that place on 74 render modes.

The way out is therefore not general. It stays conceivable on the configurations
without fog, which makes it conditional rather than impossible — but E05-S03's
classification must not rest on it without saying so.

Better to have discovered this here, on a witness, than on a wrong piece of
scenery.

## The cost, and why the measurement does not settle it

    100 frames without fog : 1538 ms
    100 frames with        : 1662 ms

That is 8 %. The ticket expected a negligible cost, "since the unit is in
hardware". This figure allows that neither to be confirmed nor denied: the two
durations correspond to 15.4 ms and 16.6 ms per frame, that is to different
multiples of the scan period. **The measurement is quantised by the buffer swap**,
and cannot resolve a cost smaller than one period.

What can be asserted: the fog does not push past more than one period, which bounds
its cost from above. Measuring it finely would require disabling the
synchronisation, or a scene heavy enough to leave the synchronised regime.

## What remains open

- The comparison of the transition against the reference **on a receding camera**.
  The ticket is right to insist: a wrong curve does not show on a still image. That
  requires the ROM.
- The behaviour to choose when fog and transparency compete for the vertex alpha.
  The measurement says they coexist; it does not say what the game expects.
