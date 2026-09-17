# What is left, and where each piece stands

Status is one of **PENDING**, **RUNNING**, **DONE**. Keep it current in the same
commit as the work: a tracker that lags is worse than none, because it is trusted.

Ordered by usefulness to the port, not by difficulty.

---

## 1. The copyright logo renders as a flat quad — DONE (not a defect)

**Resolved 17 September 2026: there is no defect.** The logo is **rotating**, and
`CAP0150` catches it showing a plain gold face. Counting colours in the logo's
area of the two captures ten lists apart:

    CAP0150   (255,222,90) 10388   (255,222,89) 2873   (254,221,89) 2762
    CAP0160   (255,222,89)  1933   (254,221,88) 1111   (0,2,60) 1078   (0,1,59) 859

The dark navy is present at 160 and absent at 150. The two live screenshots show
the same rotation at two angles, which I read backwards as a fade destroying a
texture.

`win95-corpus.md` already said "the copyright screen looked broken and is not",
and said why it is easy to get wrong. It was right.

What the investigation left behind, all of it still true and useful: the probe now
records **where in a texture** it sampled; the trace prints the raw per-corner
coordinates and vertex colours; and the polygon record's layout is confirmed
against `include/structs.h`. See `docs/research/win95-fade-loses-texture.md`,
which is kept with a retraction on top because how it went wrong is worth more
than what it concluded.

---

## 2. Fog: the blocker is found and named, fog still does not render — PENDING

**The question this item asked is answered, and it was the wrong question.** It
read "why does the blender half never fire". It does fire, and my "zero draws
fogged" was a bad measurement - a `grep` over a log that never printed the
counter. With the counter printed:

    CG0060    emitted=1412  fogged=1390     (98 %)
    CAP0250   emitted= 904  fogged= 739
    CAP0800   emitted= 755  fogged= 147

**Fog is enabled on most draws and moves the image by not one pixel**, because the
oracle's coefficient is `clampf(z, 0, 1)` with `z = -oow` - negative for every real
fragment, so `k` is zero at every pixel. That is a genuine latent bug and it is
why "fog is off" and "fog is on and does nothing" were indistinguishable for a
month.

**Refuted the same hour:** using the vertex alpha as the coefficient. With
`k = sa/255` the race collapses to 167 distinct colours and 89 % pure black -
August's black-screen disaster reproduced. DKR's vertices carry opacity in alpha
whatever the geometry mode says, so gating on `G_FOG` does not make the alpha a
coefficient.

**Found by measurement:** the lists *do* carry `G_MW_FOG` - `MOVEWORD` type
0x08, twice per capture, ignored by the decoder until now. It is decoded and
recorded (not applied):

    race and hub   w1 = 0x64009867     attract   w1 = 0x0F26F127

packing a signed multiplier in the high half and a signed offset in the low.

**What remains.** Compute the coefficient from depth with those two constants,
since this port does its own vertex transform and nothing else writes it. Two
things make that more than an afternoon: the convention has to be derived rather
than recited, and **validating it is the hard part** - both backends move
together, so the corpus can only check that they agree, not that they are right.
That is item 7 in miniature.

---

## 3. The rest of the geometry mode is read and unused — DONE (class is empty)

**Resolved 17 September 2026 by measurement: the two sources never disagree.**

Depth and culling are *derived* - from the blender and from the winding - and the
geometry mode *states* them. Both were available and nothing had confronted them,
so wherever they disagreed there would be a class of error. Counted per batch:

    CG0060    agrees on all 300 batches
    CAP0250   agrees on all 209 batches
    CAP0800   agrees on all  93 batches
    CKEY1622  agrees on all 231 batches

833 batches, zero disagreements. The class is empty, and the counters stay in the
report - silent while they agree, so that the day one disagrees it says so instead
of nobody asking again.

`G_SHADE` is still read and unused, which is deliberate: nothing derives shading
from anywhere else, so there is no second source to confront it with and no defect
to suspect.

---

## 4. `G_TEXTURE` (0xBB) — DONE (decoded; it carries no scale to miss)

**Resolved 17 September 2026.** Decoded, recorded, compared - in that order, which
was the point of the item. `gSPTexture` packs `w1 = s << 16 | t`, two unsigned
0.16 factors with 0xFFFF meaning one, the mip level at bits 11..13, the tile at
8..10 and the enable in the low byte of `w0`.

What the game actually sends, every occurrence in four captures:

    CG0060    2x   s=0x0000 t=0x0000 level=0 tile=0 on=0
    CAP0250   1x   s=0x0000 t=0x0000 level=0 tile=0 on=0
    CAP0800   1x   s=0x0000 t=0x0000 level=0 tile=0 on=0
    CAP0150   1x   s=0x0000 t=0x0000 level=0 tile=0 on=0

Always the disable form, never a scale. So there is nothing here the port was
losing: `tex_scale_s` is the S10.5-to-normalised conversion Glide wants, a
microcode scale would multiply it, and the multiplier is never sent. The enable
bit is redundant too - texturing is already decided per batch by bit 16 of the
polygon command, which is the finer source.

Decoded and left unapplied, with the counter in place. Applying a scale of zero
would erase every texture in the scene, which is a good illustration of why the
order in this item was "record, compare, then decide".

---

## 5. Driving the game — DONE for input; the menus are now reachable

**The Windows 95 build responds to a keypress for the first time.** Two faults,
both found and both fixed.

### The port had no input at all

`poll_input()`'s body sat inside `#if DKR_RUNTIME_HAS_RT64`, this target compiles
with that at 0, and the `#else` branch **zeroed every controller every frame** - a
deliberate stub, and the whole of input here. The first replacement reached for
`dkr::runtime::input::poll` and did not compile: the SDL window, the overlay and
the device layer are compiled out on this target too. So the branch reads Win32
directly through `GetAsyncKeyState`, which needs neither a window nor focus -
right for a game holding the Voodoo full screen and owning no focusable window.

### The harness pressed too briefly

Even with the path in place, nothing arrived. `pad` presses and releases in
milliseconds; this target presents about twelve frames a second, so a keystroke
falls between two polls. `pad-hold 1200 start` lands every time:

    [input] win95 buttons=0x1000     x3, one per press     (0x1000 is Start)

and the game leaves the attract sequence for **PLAYER SELECT**.

**Use `pad-hold`, not `pad`, against this target.** Worth a line in
`docs/TEST-ENVIRONMENT.md` beside the other harness traps.

### What it opens, and the next thing seen

A race is not reached yet - that is navigation, not a blocker - and the first
screen already shows the next defect: **PLAYER SELECT draws its title correctly
and leaves the rest of the screen black**, where the file panels belong. That was
not reachable before today.

It also unblocks item 6: new configurations need different game states, and the
states are now reachable.

**Cost:** twelve VM runs, three of them wasted on conditional witnesses whose
silence was ambiguous.

---

## 6. The corpus exercises 8 configurations of 29 — PENDING (unblocked by item 5)

**Measured, and the blocker is now evidence rather than assertion.**

Eight of twenty-nine, from the captures' own fill reports. Ten of the twenty-one
unexercised are two-cycle - 1, 4, 6, 9, 12, 14, 16, 22, 28, 29 - which is where the
risk is, every defect of 10-17 September having been in a two-cycle configuration.
`COMBINER.EXE` verifies all twenty-nine arithmetically on the card (0 failures,
17 September), so what is missing is their behaviour *in a scene*: interpolation,
blending against a real destination, depth, multipass sequencing.

**Two new captures were taken on 17 September** - lists 220 and 280 of the attract
sequence - and neither adds a single configuration:

    CAP0220   recipes 3, 8, 10
    CAP0280   recipes 3, 8, 10, and the unnamed one

So more of the same sequence adds nothing. New coverage needs **different game
states** - menus, a race, results, split screen - and reaching them means driving
the game, which is item 5, which is blocked on no key reaching the guest.

**Unblocked on 17 September**: item 5's input fix reaches PLAYER SELECT, so other
game states are now drivable and a capture can be taken in one. The finding stands
as written - the cheapest way to widen renderer coverage was to fix input - and it
is now done.

---

## 7. The corpus measures agreement, not correctness — DONE (scoped; watcher built)

**Not one problem. A question to ask per stage, and two stages already have an
answer.**

Every figure in `win95-corpus.md` compares the card against the oracle, and both
are fed by the same decoder, so a stage that resolves something wrongly renders it
wrongly and *identically* in both and the comparison reports perfect agreement.
The known instance is the hub's grey rectangles, where the two agree to 165 pixels
of 307,200 and are both wrong.

Stage by stage, does a reference exist that depends on neither backend?

| stage | third reference | state |
|---|---|---|
| combiner arithmetic | the closed form `(a-b)*c+d`, on the card via `COMBINER.EXE` | **covered** - 0 failures, 17 September |
| combiner table and state derivation | vectors generated from the decompilation's headers (63 macros) into `combiner_vectors.inc`, used by `test_rdp_state.c` | **covered** |
| geometry and transform | none - both backends share the decoder | **blind** |
| texture decode | none - both share the converter | **blind** |
| the whole frame | none - would need a console capture or a trusted emulator frame | **blind** |

So the blindness is narrower than the item claimed: it is geometry, texture decode,
and the frame as a whole.

**And one cheap signal already exists for part of it.** The decoder's own fill
report says how many pixels each configuration painted *and how it was classified*
- `approximate` and `multipass` counts. Those do not depend on either backend's
output: they are the decoder declaring where it knows it is not exact. The grey
rectangles were found that way. Reading that report is the closest thing to a
correctness check the project has, and nothing currently watches it for growth.

**Done 17 September 2026:** `check-corpus.sh` now records the approximate and
multipass pixel counts per scene, as `<name>.fill`, and fails the scene when they
move - the same discipline it already applied to the decoder's counts. Verified
extracting `multipass 599091 approximate 53872` from a real run.

**What remains true and cannot be fixed here:** the divergence figures still
measure agreement. Making them measure correctness needs a reference this project
does not have - a console frame, or a trusted emulator's. The item is closed
because its actionable content is done and its scope is now written down, not
because the condition went away. Any future reading of "this scene is clean"
should still be read as "the two backends agree".

---

## Recommended order

**1** first: nearly finished, needs no machine. Then **5**, because it is the only
thing that attacks **7**. The rest can wait.
