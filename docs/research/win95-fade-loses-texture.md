# RETRACTED: the logo does not lose its texture, it turns

**This note is wrong from its title down, and it is kept because the way it went
wrong is worth more than the conclusion it reached.** Read the retraction at the
end first.

---

# (original) The logo loses its texture during a fade, and the corpus cannot see it

Found on 17 September 2026 by driving the game rather than by replaying a
capture, which is the whole of why it had not been found before.

## What was seen

Two screens of the copyright sequence, six seconds apart, from one run:

* the Rare logo rendered correctly - textured, its shape and colours right, over
  a bright sky with a bloom;
* the same logo, six seconds later, as a **flat olive silhouette** of exactly the
  same outline, over a darkened sky, with the copyright line dimmed.

The background and the text are dimmer in the second, so a **fade-out is in
progress**. During that fade the logo keeps its geometry and loses its texture:
what remains is one flat colour inside the correct outline.

## Why the corpus is blind to it

`CAP0150` and `CAP0160` are captures of this screen, ten display lists apart, and
they are among the corpus's best agreements - 36 divergent pixels and 100, the
second with **zero** real divergence by the neighbour test.

That is not a contradiction. The corpus compares the **card against the oracle**,
and both are fed by the same decoder. A configuration the decoder resolves wrongly
is rendered wrongly and identically by both, and the comparison reports perfect
agreement. `win95-corpus.md` already records one case of exactly this - the hub's
grey rectangles, where "the card and the oracle agree on that scene to 165 pixels
of 307,200 and are both wrong in the same place" - and this is a second.

**Every figure in this repository's corpus measures agreement, not correctness.**
That is worth restating whenever a scene comes back clean.

## What it is not

Not a fill-rule difference, not a combiner rounding, not any of the classes the
week of 10-17 September worked through: those all move the card relative to the
oracle, and this moves neither.

Not obviously the multipass fall-through either, though that is the first
candidate. The screen's fill is dominated by `G_CC_PRIMITIVE` - 508,162 pixels,
more than the frame - with 307,200 of recipe 3 and 5,396 of `G_CC_BLENDT_ENV_ALPHA_A_TxP`
classified approximate. A flat primitive colour inside a correct outline is what a
textured configuration looks like when the texel is dropped and the constant
survives.

## What would settle it

A capture **during the fade**, which no capture in the corpus is. `CAP0150` and
`CAP0160` are lists 150 and 160 and the fade is seconds later; the harness can arm
a capture at any list number, and one taken there would put the fade in front of
both backends and in front of `--probe`.

Until then this is a screenshot and an inference, and it is recorded as such.

## It is already in the corpus, and the bisection was unnecessary

Two runs were spent capturing at lists 280 and 220 to put the fade in front of
both backends. Both landed past the screen, and the second one should not have
been attempted: this repository already records that **the list index is not a
stable coordinate** for a timed animation - it is why `CG0060` was anchored by
game mode and landed at list 1717, "a number no run could have been asked for in
advance". Bisecting an animation by absolute list number across runs is measuring
against a moving mark.

And it was unnecessary twice over, because `CAP0150` **is** the screen. Rendered
through the oracle it shows the same flat quad: the logo as one rounded rectangle
of flat gold, over the right sky, with the copyright line in place.

So the corpus has carried this defect for a fortnight under the heading "the
copyright screen looked broken and is not". That conclusion was reached because
two captures ten lists apart showed the shape turning, and because both backends
agree on it to 36 pixels. Turning or not, agreed or not, it is a flat quad where
there is a picture.

## What the probe says, host-side and free

At the centre of the quad:

    1  painted 0x000000 -> 0x000000  SHADE        recipe=18 blend=additive tex=0  depth=0
    2  painted 0x000000 -> 0xFEDD59  TEX*SHADE+A  recipe=3  blend=opaque   tex=1  depth=2
    3  depth   0xFEDD59 -> 0xFEDD59  TEX*SHADE+A  recipe=3  blend=opaque   tex=6  depth=2
    4  painted 0xFEDD59 -> 0xFEDD59  SHADE        recipe=18 blend=alpha    tex=0  depth=0

Draw 2 paints the flat gold from texture 1. Draw 3 is depth-rejected, being
farther than what draw 2 wrote.

**Texture 1 is not flat.** Dumped, it is 32x32 with **117 distinct colours**, and
its most common are (255,222,82), (255,239,99), (255,214,82) - the same gold
family as the 0xFEDD59 that reaches the screen. It is sampled: 82 triangles,
19,091 pixels painted, which is about the area of the quad. So the texture
arrives, is read, and the whole quad lands inside a tiny patch of it.

That is degenerate texture coordinates, not a missing texture and not a combiner.

## The named suspect, and it is inference

`tex_scale_s` and `tex_scale_t` scale every emitted vertex's coordinates
(`f3ddkr.c:500`). They are **derived from the tile's own size** -
`1.0f / (32.0f * big)` - and never read from the command that states them.
`G_TEXTURE` (0xBB) is that command, it carries the microcode's s and t scale
factors, and it sits in the `0xB0..0xBF` family the decoder drops: exactly one per
capture, counted as `deferred`.

Inferring the scale from the tile is right whenever the list uses the default
scale and wrong whenever it does not. A model that sets its own would have its
coordinates multiplied by the wrong constant, and a scale too small collapses a
quad into one patch of its texture - which is what is on the screen.

**This last step is inference and is marked as such.** What is measured: the
texture has 117 colours and renders flat, the scale is derived rather than read,
and the command that states it is dropped. What is not measured: that decoding
0xBB changes this pixel. That is one run once it is written.

## The inference was wrong, and the probe says why

The probe was taught to record the texture coordinates it sampled at - it could
say which draws painted a pixel and never **where in a texture** they read, which
is the whole difference between "the texture is wrong" and "the coordinates are".

Three pixels spread across the quad, on the same draw:

    (300,200)   st = 0.974, 0.451
    (320,235)   st = 0.974, 0.451
    (345,270)   st = 0.974, 0.451

**Identical.** The coordinates do not vary across the polygon at all, and a wrong
scale cannot do that - a scale multiplies, it does not flatten. `G_TEXTURE` is
exonerated, and the derived `1/(32*big)` is exonerated with it: that expression is
the S10.5-to-normalised conversion Glide wants, not a stand-in for the microcode's
scale, which was the misreading behind the guess.

What is left is upstream of any scale. `f3ddkr.c:497` reads s and t **per corner**
from the triangle command itself - `read_s16(c, a + 4 + corner * 4)` and the two
bytes after - so they should differ between corners by construction. Texture 1 is
sampled by 82 triangles over 19,091 pixels, which is 233 pixels a triangle: a
constant coordinate over that area means the three corners carry the same pair.

So the display list is being read as giving every corner of this object the same
texture coordinates, for artwork that has 117 colours to map. Either those bytes
really are identical - which would make the mapping come from somewhere this
decoder does not look - or this object uses a vertex format whose s and t are not
at that offset.

**The next step is one trace and no run**: print the raw per-corner `s16` pairs
for the triangles that sample texture 1, and the list itself will say which.

## The list really does give every corner the same pair, and only here

The trace prints the raw `s16` pair per corner, as the list gives it, before any
scale. For the logo's triangles:

    corner=0 raw s=997 t=462
    corner=1 raw s=997 t=462
    corner=2 raw s=997 t=462     ... and the same for every one of them

997 / 1024 = 0.974 and 462 / 1024 = 0.451, which is exactly the `st` the probe
reported, so the chain from the list to the sample is consistent end to end.

**And the decoder is not broken in general**, which is the check that stopped this
note concluding the opposite: counting the distinct pairs it reads over a whole
capture gives **102** on this screen and **1,867** on the race. Coordinates vary
everywhere else. It is this object, and only this object, whose corners all carry
the same pair.

So the remaining possibilities are narrow and neither is yet measured:

* the logo's triangles genuinely carry one coordinate each, and its mapping comes
  from something this decoder does not follow - a matrix, a tile shift, a second
  command;
* or this object is drawn by a triangle command variant whose s and t are not at
  `a + 4 + corner * 4`, and the bytes read there happen to repeat.

Distinguishing them means reading the bytes of one of those commands against the
decompilation's F3DDKR layout. That is the next step, it needs no machine, and it
is where this note stops rather than guessing a third time.

## The offsets are right, checked against the decompilation's own structure

`include/structs.h` defines the record the polygon command points at: a byte of
flags and three vertex indices, then three four-byte texture-coordinate pairs at
0x04, 0x08 and 0x0C. Sixteen bytes, which is what `gSPPolygon` announces
(`numTris * 16`) and what `TRIANGLE_STRIDE` already is.

So `a + 4 + corner * 4` is the right offset, and the second of the two remaining
possibilities is dead.

**And the address and stride are right too**, by an argument that needs no further
reading: the vertex indices come out of the *same* sixteen-byte records, and the
logo's geometry is correct on screen - the outline, the rounded corners, the
rotation. A wrong `source` or a wrong stride would wreck the shape before it
touched the texture. It does not.

So the uniform pair is real data: this object's triangles each carry one texture
coordinate, in the list, as shipped.

## Which moves the question again

The screen is not uniformly gold when it is right. The live capture of the correct
frame shows the logo with a gold border and a dark navy interior; the flat frame
is gold everywhere. So what is lost is the **dark part**, not the pattern - and an
object whose triangles each carry a single coordinate is exactly how one would
draw a shape in flat colours, one colour per triangle, with the texture supplying
the shade rather than a pattern.

That reframes it: the question is no longer "why is the texture not mapped" but
"why do the triangles that should be dark come out gold". Candidates, none
measured: the vertex colours are being ignored where they should modulate; or the
dark triangles are a separate batch that is not drawn; or they are drawn and lost
to the depth test, as draw 3 of the probe is.

This note has now been wrong twice - once about `G_TEXTURE`, once about the
offsets - and both times the measurement that refuted it was cheaper than the
change it would have justified. The pattern is worth naming: **every guess here
was about the mechanism, and every refutation came from looking at the data.**


---

# Retraction, same day

The logo is not losing anything. **It is rotating**, and `CAP0150` catches it
showing a plain gold face.

The measurement, which took one command and should have been the first thing
tried: count the colours in the logo's area of both captures.

    CAP0150   (255,222,90) 10388   (255,222,89) 2873   (254,221,89) 2762   (66,123,231) 916
    CAP0160   (255,222,89)  1933   (254,221,88) 1111   **(0,2,60) 1078**   **(0,1,59) 859**

The dark navy is **present at list 160 and absent at list 150**. Ten display lists
apart, one object, turning. The two live screenshots say the same thing and I read
them backwards: the decorated face, then six seconds later the gold face as the
screen fades - a rotation caught at two angles, not a texture lost during a fade.

So `win95-corpus.md` was right when it said "the copyright screen looked broken and
is not", and it even recorded why this is easy to get wrong: *"it took two captures
ten display lists apart to establish that, and the first one on its own said the
opposite."* That sentence is in the file. I read it, quoted the surrounding section
in an earlier commit, and then spent an afternoon doing exactly what it warns
against.

## What the afternoon was worth anyway

Every measurement in the body above stands; only the conclusion drawn from them
was wrong. And three of them stand as *evidence that there is no defect*:

* the coordinates are uniform per triangle **because the object is drawn in flat
  colours**, one tone per face, with the texture supplying shade - which is what a
  gold-and-navy logo with a relief would be;
* the vertex colours carry exactly two tones, white and grey 119 - a lit face and a
  shaded one, which is relief, not damage;
* the offsets, the stride and the address are confirmed correct against
  `include/structs.h`, which is worth having written down whatever prompted it.

## The lesson, which is the reason this file is not deleted

Three guesses, three refutations, and **every refutation came from looking at the
data rather than at the mechanism**: a scale multiplies and cannot flatten; the
struct says the offsets are right; the neighbouring capture has the missing
colour. The third one was available from the first minute and cost one command.

The rule that would have saved the afternoon is not "be more careful". It is:
**when a single frame looks wrong, compare it with the next frame before
comparing it with anything else.** An animation is the cheapest explanation for a
frame that disagrees with expectation, and this repository had already paid for
that lesson once and written it down.
