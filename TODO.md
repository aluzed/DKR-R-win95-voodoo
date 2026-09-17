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

## 2. Fog — DONE as an investigation; deliberately not implemented

**Every question this item asked is answered, and the answer changes its
priority.**

*Does the blender half fire?* Yes. My "zero draws fogged" was a bad measurement -
a `grep` over a log that never printed the counter. Printed:

    CG0060  emitted=1412 fogged=1390 (98%)   CAP0250 739/904   CAP0800 147/755

*Then why does nothing change?* The oracle's coefficient was `clampf(z, 0, 1)` with
`z = -oow`, negative for every real fragment, so `k` was zero at every pixel. Fog
could be on for 98 % of a scene's draws and move nothing.

*Is the vertex alpha the coefficient?* **No** - refuted by measurement. `k = sa/255`
collapses the race to 167 colours and 89 % pure black, August's disaster
reproduced. DKR's vertices carry opacity there whatever the geometry mode says.

*Then where does it come from?* `G_MW_FOG` - `MOVEWORD` type 0x08, twice per
capture, ignored until now. Decoded and recorded.

*And what does it ask for?* Inverting `gSPFogPosition`'s own arithmetic:

    race and hub   fm 25600, fo -26521  ->  near 1018, far 1023
    attract        fm  3878, fo  -3801  ->  near  990, far 1023

**The game asks for fog over the last half a per cent to three per cent of the
depth range** - a thin haze on the far plane, not a wash over the scene.

### Why it is closed without being implemented

Three reasons, in order of weight:

1. **The measured payoff is small.** A sliver at the horizon, on scenes the corpus
   already matches to 167 pixels in 2.46 million.
2. **There is no validation path.** Both backends would move together, so the
   corpus can only check that they agree - item 7's blindness exactly, and this is
   the case that falls in it.
3. **It needs a quantity neither backend keeps**: a normalised screen depth. `z` is
   `-oow` and `ooz` is a depth-buffer value; neither is the `d` the formula wants.

The derivation is written down in `cmd_move_word` so that whoever implements it
starts from `k = d*fm/128000 - (fm-fo)/256` and two decoded ranges rather than from
a convention half-remembered. That is worth more than a guess applied today.

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

## 6. Corpus coverage — DONE for the method; nine of twenty-nine and growing

**The method is proven, not proposed.** `CAP0420.BIN` was taken on 17 September
inside the menus - the first capture of this corpus taken from a state reached by
*driving the game*, which became possible the same day.

    309071 ppm 655902  99% opaque  G_CC_MODULATEIDECALA + G_CC_BLENDI_ENV_ALPHA_PRIM2
     85024 ppm 180435   0% opaque  G_CC_BLENDT_ENV_ALPHA_A_TxP
     67776 ppm 143832   0% opaque  **G_CC_MODULATEIA**      <- new
      9344 ppm  19829   0% opaque  G_CC_PRIMITIVE

`G_CC_MODULATEIA` is number 23 and no capture had ever exercised it. Coverage goes
from **8 of 29 to 9 of 29** in one run.

**What made it work**, and it is the reusable part: arm the capture at a list
number the run reaches *after* the menus are up. The list index is not a stable
coordinate between runs - this file records that - but "high enough that the menus
are up" is a weak enough condition to survive the variation.

**What is left here is arithmetic, not method.** Twenty more configurations, each
needing a state that reaches it; ten of them two-cycle, which is where the risk
is. And `CAP0420` itself needs a card render beside it before it contributes a
divergence figure rather than only coverage - one run.

The item is closed because the question it asked - how does coverage grow - is
answered and demonstrated. Growing it further is work, not investigation.

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

## Where this leaves the project, 17 September 2026

All seven are closed, and **closed means the question each one asked is answered**
- not that no work remains behind it. Two carry follow-on work that is work rather
than investigation, and both say so in their own section:

* **item 2** — fog is derived and quantified, deliberately not implemented: the
  payoff is a sliver at the far plane, there is no validation path, and it needs a
  normalised depth neither backend keeps;
* **item 6** — coverage went 8 of 29 to 9 of 29 and the method is proven; the
  remaining twenty are arithmetic, one game state at a time.

The one that changed the project is **item 5**. The Windows 95 build had no input
at all - `poll_input()` was a stub that zeroed every controller - and it now reads
Win32 directly and reaches PLAYER SELECT. Everything downstream of "can the game
be driven" was blocked on that and is not any more, including item 6, which was
only ever blocked on it.

### The follow-on work, and two of the three are done

1. ~~PLAYER SELECT draws its title and leaves the rest black.~~ **Very probably not
   a defect.** `CAP0420`, taken from the same driving session a few presses later,
   shows GAME SELECT rendered completely - sky, title, three panels, both footer
   labels. A decoder that renders one menu whole does not lose the body of the
   previous one; the black frame was almost certainly caught mid-transition. The
   same trap as item 1, on the same day. Not chased.
2. ~~A card render beside `CAP0420`.~~ **Done.** `tail 46 real 6` - the cleanest
   scene in the corpus, and the one carrying `G_CC_MODULATEIA`. The ninth
   configuration is exercised *and* verified, which is what makes it a corpus entry
   rather than a screenshot. Nine scenes: 2,600 divergent, 173 real, of 2.76
   million.
3. **Drive further** - partly done, and it answered an older question on the way.
   `CAP0700` is the controller-pak caution screen, three menus deep: **tail 9,
   real 5**, the best figure in the corpus. It adds no new configuration (recipes 3
   and 20) but it is almost entirely the small proportional font that
   `win95-hud-digits.md` reported as shredded on menus and could not test for want
   of a capture. Both backends render it cleanly, so that hypothesis is much weaker
   than it was.

   **Where the navigation stalls, measured twice:** after the file choice the game
   puts up the caution screen and waits (`CAP0700`). Adding ten more A presses did
   not get further - `CAP0900` has the *same fill profile as `CAP0420`*, meaning
   the sequence went round GAME SELECT and came back.

   **Mashing A plateaus**, and mapping the presses one at a time said why: four
   holds from the attract loop land back on the **title screen**, not deeper. The
   game opens on an attract sequence and presses sent into it do not navigate -
   sequences built there go round and return. The title screen (START / OPTIONS)
   is the real entry point, and that is now in `docs/TEST-ENVIRONMENT.md`.

   Six runs at about ten minutes each. The ones that taught something screenshotted
   after **every** press; the batches that inspected only the end could not tell
   "went deeper" from "went round".

   **The path is now mapped as far as track select** and is in
   `docs/TEST-ENVIRONMENT.md`: four holds on `start` reach the title, then
   `start`, `a`, `a`, `a`, `down`, `a` reach **DINO DOMAIN / ANCIENT LAKE** with
   its preview - seven presses from a cold start, where seven sent into the attract
   loop got nowhere at all. The `down` is what the earlier batches were missing:
   `a` confirms whatever the cursor is on and never moves it.

   Track select is **one press from a race**, which is where the remaining
   configurations are - the HUD, the racers, the track itself.

   **How the next attempt can miss**, met on 17 September: sending the sequence
   with different press counts desynchronises the whole route. `CAP1100` was meant
   to be vehicle select and came back with GAME SELECT's fill, because five
   `start` then three `a` is not four `start` then `start a a a down a`.

   I first wrote that up as "the menus idle back and the capture found the game
   elsewhere", and **withdrew it**: the screenshot taken right after the presses
   shows GAME SELECT as well, so the capture agrees with the screen and there is no
   evidence of idling at all. The only fault was my own press count.

   **And the exact sequence fired blind is not reliable either.** `CAP0600` sent
   all seven correct presses with the same waits and landed on the *caution*
   screen - one step short - because each screen takes a variable time to become
   responsive and fixed sleeps drift. The route holds only when each step is
   confirmed by a screenshot before the next press. That is now the standing
   instruction in `docs/TEST-ENVIRONMENT.md`, and it is why three ten-minute runs
   on 17 September captured screens the corpus already had.

   **One thing seen and deliberately not concluded:** the caution screen rendered
   live has a black background where `CAP0700`'s oracle render has a blue sky.
   That is very likely another transition frame - the same trap as items 1 and the
   PLAYER SELECT body, twice already today - and it is written here as something to
   check against a second frame rather than as a defect.

   Ten scenes now: 2,609 divergent pixels, 178 real, of 3.07 million.

### Three method notes this round paid for

* **Make the first witness unconditional.** A conditional witness's silence is
  ambiguous - a poll that never runs says exactly as little as a poll that sees no
  key - and that cost three VM runs of the twelve spent on item 5.
* **When a single frame looks wrong, compare it with the next frame first.** Item 1
  was an animation, and this file had already recorded that trap once.
* **Measure the mechanism's premise, not the mechanism.** Every guess made this
  round was about how something worked; every refutation came from looking at what
  the data actually was.
