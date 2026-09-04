# The grey rectangles are the second cycle, and both backends drop it

Measured on 4 September 2026 on `CAP0250.BIN` — the hub, Pipsy on the beach,
3592 commands, 1293 triangles, 904 emitted, 190 textures. The densest scene in
E09-S02's corpus.

## What is on the screen

Several large flat grey quads lie across the water and the character. They are
in the **oracle** and in the **card**, identically.

## What draws them

The oracle's pixel probe, at one of them:

    probe (400,250): 3 draw(s):
       1  0x3163DE -> 0xC89501  TEX*SHADE+A ... recipe=3  tex=13
       2  0xC89501 -> 0x1B0800  TEX*SHADE+A ... recipe=10 tex=71
       3  0x1B0800 -> 0xDCDCDC  TEX*CONST   const=0xFFFFFFFF recipe=8 tex=100

The last draw is the grey. Texture 100 is a 32×32 intensity map — cloud or glow —
and it paints 17,853 pixels over two triangles.

Recipe 8 is `G_CC_MODULATEIA_PRIM` in cycle 1 and **`G_CC_BLEND_ENV_ALPHA2` in
cycle 2**, classified `DKR_CC_MULTIPASS`, with the note *"reads PRIMITIVE and
ENVIRONMENT in the same term: Glide has only one constant register"*.

Cycle 1 is the texture tinted by the primitive colour, which here is white — so
the raw texel. Cycle 2 would blend that toward the environment colour by the
environment's alpha, which is what makes the effect subtle. **Neither backend
computes cycle 2.**

## Why neither computes it

`DKR_CC_MULTIPASS` says *several passes would get there*. It is a
**classification, not an implementation**: `gl_set_state` sends `DKR_CC_EXACT`
entries to the catalogue's Glide setup and everything else to `apply_combine`,
whose four modes are the nearest single-pass approximation. The software oracle
implements the same four modes. There is no second pass anywhere in this port.

E05-S03 records this honestly — its criterion *"the multipass fill cost is
measured"* is unchecked, with the reason: *"it needs a race, not the menu, and
the port does not render one legibly yet."*

**It renders one legibly now, and there is a frozen capture of it.** That
criterion is unblocked.

## What this says about the harness, and it is not comfortable

The two backends agree on this scene to **165 divergent pixels out of 307,200**,
537 per million, on 904 triangles and 190 textures. That is the best agreement
the harness has measured on a real frame.

**And they are both wrong in the same place.** `win95-oracle-vs-card.md` says the
confrontation establishes *"that two independent implementations of the same
specification agree"*. They are not independent below `dkr_render_state`: both
receive the decoder's collapse of a two-cycle combiner into one of four modes,
and both then compute that faithfully.

So agreement between the oracle and the card proves the **backend** right and says
nothing about the **translation**. The grey quads are the picture of that limit,
and it is worth having the picture: 79.7 % of this game's triangles are
classified multipass, and until today that number lived in a performance note
about doubled fill.

The gap is not a defect to fix here. It is the shape of a piece of work E05-S03
has already named, now with an image, a measured scene, and a deterministic input
to develop it against.

## The smaller half, done the same day

An oracle that computes both cycles puts a *correct* reference on one side of the
comparison, so that what the card cannot express shows up as a divergence instead
of an agreement. That was the smaller half of the job and the half that makes the
other half checkable, and it is done: see `win95-oracle-combiner.md`.

The result on this very scene: **165 divergent pixels became 11,396**, and the
difference map is a character the card paints black. The grey quads are still
there on both sides — a glow whose second cycle would fade it — but they are no
longer the biggest thing wrong, and the biggest thing wrong is now measurable.

## Files

- `CAP0250.BIN` — the capture
- `docs/research/win95-corpus.md` — the corpus and how it is checked
- `docs/stories/E05-glide/E05-S03-color-combiner-translation.md`
