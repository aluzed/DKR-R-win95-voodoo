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

1. **PLAYER SELECT draws its title and leaves the rest black — IT IS A DEFECT**,
   and my first reading of it was wrong.

   I wrote it off as a transition frame, reasoning that a decoder rendering GAME
   SELECT whole would not lose PLAYER SELECT's body. Sound reasoning, wrong
   conclusion. Driving the game one press at a time and taking a **second
   screenshot twelve seconds later with no press between** shows it unchanged -
   the title's colours shift, so the frame is live, and the blackness is stable.

   And the capture says the opposite of the screen: the oracle draws the scene
   whole, 380,294 pixels of recipe 3 alone, and the card measures `tail 800,
   real 77` against it. **The list has the content and the backend renders it.**

   So the fault is between rendering and presenting in the live game, which
   `REPLAY.EXE` does not exercise - it decodes one list into one buffer and reads
   it back. **The first defect here the capture harness cannot reproduce.** See
   `docs/research/win95-player-select-black.md`.
2. ~~A card render beside `CAP0420`.~~ **Done.** `tail 46 real 6` - the cleanest
   scene in the corpus, and the one carrying `G_CC_MODULATEIA`. The ninth
   configuration is exercised *and* verified, which is what makes it a corpus entry
   rather than a screenshot. Nine scenes: 2,600 divergent, 173 real, of 2.76
   million.
3. **Drive further** - partly done, and it answered an older question on the way.
   **Reaching vehicle select needs an interactive sequence**: screenshots between
   presses record but do not confirm, the run still advances on a fixed sleep, and
   it drifts - measured twice, landing at track select once and two screens short
   the next time. The next attempt has to send one press per step and read the
   screen before the next. See `docs/TEST-ENVIRONMENT.md`.
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

   **And it is captured.** `CAP0600.BIN` is DINO DOMAIN / ANCIENT LAKE with its
   live 3D preview, taken by the mapped route with a screenshot confirming every
   press. Six configurations where the flat menus have three or four, and
   `G_CC_MODULATEIDECALA` - number 2 - had **never been exercised**: 307,200
   pixels, a full screen's worth. Coverage is **ten of twenty-nine**.

   **And it measures at `tail 460, real 83` - the worst real divergence in the
   corpus**, the next being the race at 54. A new configuration exercised for the
   first time surfaced a divergence no existing scene could show, which is exactly
   what the coverage work was for. `G_CC_MODULATEIDECALA` is the obvious suspect
   and not yet the diagnosis: the screen also mixes a live 3D preview with menu
   furniture, which nothing else in the corpus does.

   **Probed, and the likely class is coverage at mesh junctions.** The card's
   probe shows a batch marked `elsewhere` changing the pixel, and the neighbouring
   batches explain it: one of them has a vertex at (379.25, 250.01) - the probed
   pixel - so several triangles meet there. A point on a shared edge is inside for
   one fill rule and outside for another. Same class as the corpus-wide residue,
   arriving in larger numbers because this is the first captured screen with a
   dense 3D mesh.

   **And the class is measured, not assumed:** the neighbour test at widening
   radius gives 460 -> 83 -> 49 -> 35 -> 15 -> **9 at twelve pixels**, the same
   shape as the corpus-wide residue. A displaced boundary is forgiven as the radius
   grows and a wrong colour is not, so nine is what this scene really adds to the
   hard core.

   It also exposed a limit of the instrument: `covers` is the watch's opinion, not
   the card's, and where they disagree the watch is wrong by construction. That is
   now written beside the measurement.

   Eleven scenes: 3,069 divergent pixels, 261 real, of 3.38 million.

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

## Follow-on, 17 September 2026

| # | Item | Status |
|---|------|--------|
| 8 | PLAYER SELECT / menus black on the card | DONE - confirmed in the running game |

**8 — the card's hardware fog painted the scene black.** Forty-three of seventy-three
draws reached the card with fog programmed, a fog colour of zero because
`G_SETFOGCOLOR` is never decoded, and a coefficient taken from the vertex alpha,
which this game uses for opacity. On the card, `CAP0420` went from 29.87 % of the
screen painted to 99.18 % with fog off - matching the 08:46 render to the colour
count - and `CAP0600`, 440 fogged draws of 464, comes back at 99.01 %. Fog is off
unless `DKR_FOG=1` asks for it, which is what the port's own comments have said
since the switch existed.

Confirmed in the running game the same evening: the N64 logo and the Rare
copyright screen both carry their sky gradient, and PLAYER SELECT draws its sky,
trees, grass, flowers, eight characters and title. The caution screen's black
background - recorded in item 5 as something to check rather than concluded - was
the same defect and went with it. One black frame appeared between the copyright
screen and the menu; the two frames after it are identical at 48.6 % mean
brightness, so it was a transition, compared against the next frame rather than
reported.

Two instruments were built on the way and both stay useful: `replay --frames N`,
which replays a capture through the same backend N times, and
`replay --probe-depth X,Y`, which reads the card's depth buffer and proves its own
reader against the clear before reporting anything.

### The method notes this one paid for

* **Build the instrument that answers directly before the ones that answer by
  elimination.** The depth test was suspected for a day and the depth buffer was
  never read. Seven diagnostic switches inferred its contents from what survived;
  one `grLfbLock` said it outright, in forty lines.
* **A comparison is only as good as the oldest number in it.** The fog hypothesis
  was refuted at 10:34 by setting fresh counts against divergence figures measured
  at 08:51, before the gate being counted reached a built binary. Nothing in the
  table said how old its columns were. That refutation cost two hours and sent the
  search back to depth.
* **Reproduce a live defect in the harness before chasing it live.** The black
  scene turned out to be one `--card` replay away, where the pixel probe, the card
  probe and the log all work and a run needs no driving.

| # | Item | Status |
|---|------|--------|
| 9 | Drive the game into a race, to exercise the remaining configurations | RUNNING |

**9 — the instruments are built, the route is not.** `game-mode` gives the arrival
signal a route needs: the game logs `gGameMode` and a race is the one that reads
INGAME, readable from the host while the guest runs. `pad-until` removes the timing
drift that made fixed press counts unreliable.

What is missing is the route itself, and two attempts said so rather than one.
Alternating A and Start for twelve presses stays in MENU: that path creates a new
save file and stops at the initials entry, where A appends a letter and never
confirms. Backing out and taking TRACKS instead, for six more presses, also stays
in MENU.

So this is menu-structure knowledge, not tooling - which choice on which screen,
and which control reaches "Ok" in a letter grid. Written down as PENDING rather
than attempted again by mashing buttons, because three blind routes would say no
more than two did.

Anything learnt on the way belongs here: the mode is coarse (every menu screen
reads MENU, so only arrival at a race is detectable, not progress through the
menus), and the two black screens met on the way were both transitions, confirmed
against the next frame before anything was concluded.

### Item 9, second sitting: the tooling was the problem, and the route is now half written

The blocker was not menu knowledge after all - it was that nothing could tell a
screen change from a menu animating, so every route was walked blind. The
distinct-colour count separates them by an order of magnitude where the two image
differences tried before gave 34 against 35. With `pad-until` comparing that
instead, and screens identified by their fingerprint, the route walks and every
step is checked:

    step                       brightness   colours   gGameMode   level
    PLAYER SELECT                  51.7 %     89831   MENU        0x801FB780
      a  (choose)                  52.3 %     90709   MENU        0x801FB780
      a  (confirm "OK?")           64.0 %     69945   MENU        0x801FB780
    CAUTION
      start                        59.8 %     40224   MENU        0x801FB780
    GAME SELECT
      down, a  (TRACKS)            59.9 %    105590   MENU    **0x8023E7C0**
      a                            65.0 %    110279   MENU        0x8023E7C0
      a                            72.6 %     76813   MENU        0x8023E7C0
      a                            73.6 %     69747   MENU        0x8023E7C0

`gCurrentLevelHeader` moves from `0x801FB780` to `0x8023E7C0` on the TRACKS
choice: **a level is loaded**, which no previous attempt ever achieved. What
follows is a pre-race sequence of three or more screens that keeps `gGameMode` at
MENU, so the remaining work is which control each of those wants - a shorter
question than the one this item started with, and every step of the answer is now
verifiable rather than guessed.

Note the second signal, found by accident and worth keeping: the level pointer in
the same log line changes a full report before the mode does, so it sees a level
load that `gGameMode` has not caught up with yet.

### Item 9, third sitting: the route is reproducible, and neither button confirms

The route was walked again on a restarted machine with a reloaded game, and every
step landed on the same screen as before, within the animation variation:

    step                    brightness   colours   first walk
    PLAYER SELECT              48.5 %      90499      90621
      a  (choose)              50.5 %      93163      90709
      a  (confirm)             64.2 %      70066      69945   -> CAUTION
      start                    61.1 %      40554      40224   -> GAME SELECT
      down, a  (TRACKS)        59.9 %     105590     105590   -> level loaded
      start                    47.1 %     150590          -   -> a load frame

So the route is **reproducible**, which was not established before: the
fingerprints identify screens across a reboot.

Past that point the sequence cycles - 96091, 85402, 120078, then 76512, 99512,
69025 distinct colours - and `gGameMode` stays at MENU through six readings taken
over about a hundred seconds, far past the one-report lag. `a` was tried and
`start` was tried; neither leaves MENU. `gCurrentLevelHeader` holds at 0x8023E7C0
throughout, so a level stays loaded the whole time.

**The next hypothesis, and why.** A colour count that keeps moving between six
readings without a press is a screen animating on its own, and one that swings
between 69k and 120k is a large part of the frame changing - which is what a
carousel of track previews would do. A carousel needs a *direction* before a
confirmation, and only `a`, `start` and one `down` have been sent so far. Try
`left`/`right` to settle a selection, then confirm.

Recorded rather than attempted a fourth time in one sitting: the previous three
each cost a boot, and the question is now specific enough that one run should
answer it.

### Item 9, fourth sitting: the run was spoiled by my own criterion, and the fix is in the driver

The carousel hypothesis was **not** tested. The run that was meant to test it
climbed to PLAYER SELECT with `colours > 85000` - a floor, not a window - and
stopped on a screen holding 127,788. Every richer screen passes a floor, and this
game has them up to 150,590.

The route then ran six presses against screens it had misidentified. It partly
realigned by accident, which is worse than failing outright: `start` from GAME
SELECT took ADVENTURE rather than TRACKS, so the two experiment presses landed on
the initials entry, where `right` moves a cursor in a letter grid. The readings
look plausible and mean nothing.

    climb stop     49.0 %   127788   not PLAYER SELECT (90,500)
    a              17.7 %    60961
    a              48.7 %    91654   PLAYER SELECT, reached by accident
    start          64.7 %    70437   CAUTION
    down           65.0 %    70630   no effect, CAUTION has no menu
    a              60.6 %    39516   GAME SELECT
    start          63.2 %    50610   ADVENTURE, not TRACKS
    right          64.3 %    49866   a cursor in a letter grid
    a              65.5 %    56159   the initials entry

`pad-until-screen <control> <colours> [tolerance]` now does this properly, in the
driver rather than in a throwaway inline loop: it presses until the colour count
is **within a window** of a named fingerprint, tolerance 3000 by default - twice
the largest drift measured within one screen, well inside the smallest gap between
two. Tested against a screen it was already on: arrived after 0 presses, 464 off.

So the carousel question is still open and the next run can be written against
fingerprints instead of press counts:

    pad-until-screen start 90621     -> PLAYER SELECT
    a, a                             -> CAUTION      (expect 70446)
    start                            -> GAME SELECT  (expect 40554)
    down, a                          -> TRACKS, a level loads
    then the experiment
