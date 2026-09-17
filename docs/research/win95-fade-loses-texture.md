# The logo loses its texture during a fade, and the corpus cannot see it

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
