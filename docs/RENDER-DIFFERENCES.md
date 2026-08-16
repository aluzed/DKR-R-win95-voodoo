# What will not look like the console, and why

This document lists the port's **accepted** deviations from the Nintendo 64's
rendering. It exists for a precise reason: a documented deviation is a known
characteristic of the port; the same deviation, undocumented, will be reported as
a defect at every comparison, indefinitely.

Each entry says what differs, why it is irreducible, and what it looks like on
screen.

---

## Texture filtering — three points against four

**What differs.** The RDP filters over a **triangle of three texels**; the Voodoo
2 does a classic bilinear over **four**. It is not a setting, it is the structure
of the filtering unit.

**Why it is irreducible.** Three-point filtering is not expressible with Glide's
modes. Reproducing it would demand rendering every surface in several passes with
texture offsets, at a cost bearing no relation to the gain.

**What it looks like.** The deviation is visible on low-resolution textures — that
is, on most of DKR's, the console's texture memory being very limited. The port's
rendering is slightly smoother on the diagonal; the console shows a characteristic
triangular pattern on stretched gradients.

**What is offered in compensation.** Point sampling stays available, and the game
already asks for it on some of its elements — 30 table entries out of 214 use
`G_TF_POINT`. Those surfaces will be identical to the console's.

---

## Coverage-based anti-aliasing

**What differs.** The RDP computes a pixel coverage and brings it into the blender
— that is the `AA_` prefix of the render modes, very common in DKR. Glide 2 offers
nothing but `grAADrawTriangle`, whose cost bears no relation to that of an
ordinary triangle.

**Why it is irreducible.** On a 1998 machine where fill rate is the limiting
resource, drawing every edge a second time is not conceivable.

**What it looks like.** Polygon edges are hard where the console softens them. It
is a degradation **of edges**, not of colour: it does not accumulate from one
surface to the next and does not spread through the image.

---

## The colour combiner — fourteen configurations out of twenty-nine

**What differs.** The RDP's combiner is programmable; Glide's is fixed. Of the
twenty-nine configurations DKR uses, twelve translate exactly, ten require several
passes, four are approximate and three belong to the second texture unit.

**Why it is irreducible.** Two structural limits, measured and not assumed: Glide
has **only one constant colour register** where the RDP has two, and **none of its
sixteen factors delivers that register's alpha**.

**What it looks like.** The measured deviations run from 99 to 148 units out of
255 for the configurations concerned. That is significant, and it is the largest
deviation in this list. The detail per configuration is in
`research/combiner-mapping.md`.

**What remains to be tried.** Carrying the second constant in the vertex alpha.
The way out is restricted by the fog, which already occupies that place on 74
render modes, but it stays open for surfaces without fog.

---

## Distant textures shimmer

**What differs.** Nothing: DKR uses no mipmaps — `G_TL_TILE` fifteen times in the
source, `G_TL_LOD` not once.

**What it looks like.** Textures seen from afar shimmer **exactly as on the
console**. It is therefore not a deviation, and the entry appears here because it
is the kind of thing taken for a defect of the port.

Adding mipmaps the game does not ask for would be a deviation, not a correction:
it would cost a third of the texture memory and change the appearance.

---

## Colour quantisation

**What differs.** The Voodoo 2's frame buffer is 565 — five bits of red and blue,
six of green. The combiner multiplies in 0..255 and **truncates**, which costs one
further quantisation step.

**What it looks like.** A maximum deviation of 9 units out of 255, measured on the
full comparison against the reference rasteriser. Invisible on a surface,
perceptible as banding on a very stretched gradient.

---

## Depth sorting

**What differs.** Sixteen bits, against the N64's compressed format. Measured: the
buffer separates two surfaces two parts per thousand apart at any distance within
the play range, and gives way entirely at two tenths of a part per thousand.

**What it looks like.** Nothing visible under the game's conditions, except on
surfaces closer together than the track presents. The wall is known and measured,
which will allow the symptom to be recognised if it appears.
