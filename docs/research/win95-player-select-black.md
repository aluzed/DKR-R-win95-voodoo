# SOLVED: PLAYER SELECT is black because every depth-tested draw fails

**Diagnosis, 17 September 2026.** Running the game with `DKR_NO_DEPTH=1` brings the
scene back:

    region        baseline   depth off
    the title       51 %       54 %      unchanged - it never used depth
    the scene        0 %       36 %      **from nothing to present**

The title is drawn with the depth test **disabled** (`depth=0`, `z=-1.0`, the 2D
overlay convention) and the scene with **test and write** (`depth=2`), which the
capture's own probe shows. Disabling the test restores the scene and leaves the
title alone. So the live depth buffer rejects every fragment tested against it, and
the black screen is exactly the draws that consult it.

This codebase carries the precedent twice, in its own comments: a depth buffer that
is not cleared - because the mask is shut at clear time - keeps the previous
frame's depths, and *"the screen went black, everything failed the test against a
frozen scene"*. `gl_begin_frame` opens the mask for the clear for that reason. Why
it fails in the running game and not in the replay is the next question, and it is
a narrow one: the replay clears once and draws one list, the live game clears every
frame.

### The clear looks correct, and the presents outnumber the lists eight to one

Reading the clear path rather than guessing at it: `gl_begin_frame` sets
`grDepthBufferMode` back to the W buffer *and* opens the mask before clearing -
that fix is there, dated 24 August, with the measurement that motivated it in the
comment - and `dkr_glide_clear` passes `GR_WDEPTHVALUE_FARTHEST`. Mode, mask and
value are all right, and `begin_frame` is called once per graphics task in the live
renderer, so the clear happens every frame.

So the obvious mechanism is already handled, and the counters carry an anomaly I
had not looked at:

    list=420  ...  present=3600

**About eight presents for every list decoded.** Each present swaps the buffers,
and only one of the two carries what the list drew. A frame presented repeatedly
without being redrawn alternates between the drawn face and whatever the other one
holds, which is neither cleared nor drawn in that interval.

That is a lead and not a diagnosis - it does not by itself explain why the title
survives the alternation and the scene does not - but it is a measured asymmetry in
exactly the part of the pipeline the replay never exercises: `REPLAY.EXE` decodes
one list, presents once, and reads back.

**And the lead is weakened by evidence already in hand**, before it cost a run. If
the display alternated between a drawn face and an empty one at eight presents per
list, the screen would *flicker*: roughly one screenshot in two would catch the
scene. Two screenshots twelve seconds apart are both black, and the title is
steady in both. Whatever is happening is stable, not alternating.

So buffer alternation does not explain it either, and the eight-to-one ratio is
recorded as an oddity worth knowing rather than as the cause. The depth reading
stands on its own measurement - `DKR_NO_DEPTH=1` brings the scene back - and why a
clear that looks correct on every line does not take is the question left.

### Three switches, and the buffer holds the previous frame

    switch                  title   scene
    (none)                   51 %     0 %
    DKR_NO_DEPTH=1           54 %    36 %
    DKR_FLATTEN_W=1          54 %    53 %

`DKR_FLATTEN_W` puts every triangle at `oow = 1`, the **nearest** depth there is,
and the scene comes back. The three readings exclude each other's explanations:

* the buffer is **not cleared to farthest** - if it were, the scene's own depths
  would pass and there would be no defect;
* it is **not stamped at nearest** - if it were, `FLATTEN_W` would be rejected
  too, and it is not;
* so it holds values **between** the two, which on a buffer written every frame
  and cleared by a call that does not take means **the previous frame's depths**.

That is the stale-depth signature this file already carries twice from August,
measured this time from three switches rather than inferred from one. The clear
does not reach the depth buffer in the running game, while the path is correct on
every line - mode restored, mask opened, `GR_WDEPTHVALUE_FARTHEST`, once per
graphics task.

### RETRACTED: the clear does take, and the buffer is not stale

`DKR_CLEAR_NEAREST=1` clears the depth to nearest instead of farthest. Against
`DKR_FLATTEN_W`, which puts every triangle at the nearest depth and paints the
scene at 53 %:

    switch                       title   scene
    (none)                        51 %     0 %
    DKR_NO_DEPTH=1                54 %    36 %
    DKR_FLATTEN_W=1               54 %    53 %
    DKR_FLATTEN_W + CLEAR_NEAREST 51 %     2 %

**53 % to 2 %.** Clearing to nearest destroys what clearing to farthest allows, so
the depth argument of `grBufferClear` takes effect and the buffer *is* cleared
every frame. The section above - "the buffer holds the previous frame's depths" -
is **wrong and withdrawn**. Its three readings were sound; the conclusion drawn
from them was not, because they never tested whether the clear did anything.

### What the four readings do say

Against a buffer cleared to farthest, a fragment can only fail `LESS` if its depth
is **at or beyond farthest**. The scene fails; a triangle forced to the nearest
depth passes. So the scene's depths are arriving at the far end of the encoded
range - saturated, or close enough that strict `LESS` rejects them at equality.

That is a different defect from a stale buffer and it points somewhere specific:
the W encoding of the scene's `oow`, not the clear, not the mask, not the mode.
It also explains the replay's success without contradiction, the replay drawing
into the same cleared buffer from the same list - which makes the next question
what differs about the live vertices, and that is answerable with the card probe's
vertex dump.

### And the allocation is right too, which exhausts inspection

One more candidate died on reading: the context is opened with
`grSstWinOpen(..., 2, 1)` - **two colour buffers and one aux**. The depth buffer is
shared between the two faces, so there is no second aux going uncleared, and one
clear per frame is the right number.

That closes the last thing inspection can reach. Every part of the path is correct:

* the mode is put back to the W buffer before the clear (August's fix, with its
  measurement in the comment);
* the mask is opened for the clear;
* the value is `GR_WDEPTHVALUE_FARTHEST`;
* `begin_frame` runs once per graphics task;
* the aux buffer exists and there is exactly one.

**So the next step is an experiment in the running game, not more reading.** The
shape of it: make the clear observable - a draw immediately after it at a known
depth, whose survival says whether the buffer took the value - since nothing can
read the buffer back live, which is itself a measured limit of this harness.

What is left is why that call does not take, which is now a question about the card
and `grBufferClear` rather than about this port's logic.

36 % rather than 98 % is expected and not a second defect: with depth off entirely
the draw order is wrong and surfaces overdraw each other. The measurement asks
whether the scene is there at all, and it is.

The investigation that led here is kept below, because five branches were closed on
the way and each one is a thing this harness can or cannot see.

---

# (original) PLAYER SELECT is black in the running game and correct in its own capture

Found 17 September 2026, and it is the first defect in this project that the
capture harness **cannot** reproduce - which makes it worth writing down carefully.

## What was seen, live

Driving the game one press at a time, four holds on `start` land on PLAYER SELECT.
The screen shows its title and **nothing else**: the whole area where the character
line-up belongs is black.

A second screenshot twelve seconds later, with **no press in between**, is
identical except that the title's colours have shifted - so the frame is live and
being redrawn, and the blackness is stable rather than a moment in a fade. That
check is the one this repository learned to run the hard way the same day, and it
is what separates this from the logo, which turned out to be an animation.

## What the capture says, and it says the opposite

A capture armed at list 600 fired while that screen was up. Replayed:

* through the **oracle**: the complete scene - sky, trees, fence, flowers and the
  full character line-up - 380,294 pixels painted by
  `G_CC_MODULATEIDECALA + G_CC_BLENDI_ENV_ALPHA_PRIM2` alone;
* through the **card**: `tail 800, real 77` against that oracle image. An ordinary
  figure. The card draws the scene.

So the display list at that moment contained the whole screen, and the Glide
backend renders it correctly from the capture.

## What that leaves

Not the decoder: the list has the content. Not the backend: it draws the content
when handed the list. The blackness belongs to something **between rendering and
presenting in the live game** - a buffer drawn but not shown, a present that lands
on the wrong surface, a frame skipped - and none of that is exercised by
`REPLAY.EXE`, which decodes one list into one buffer and reads it back.

The title survives while the scene does not, which is a clue and not yet an
explanation: whatever draws the title reaches the screen by a path the rest does
not.

## The pipeline is not stalled, which removes half the candidates

Sitting on the black screen with the runtime log running:

    list=240  tri= 83723  emitted= 45803        present=3000
    list=300  tri=152767  emitted= 92372        present=3300
    list=360  tri=270129  emitted=170795        present=3600
    list=420  tri=350839  emitted=225999

Lists advance, triangles are emitted by the hundred thousand, and presents climb
steadily. **Nothing is stalled and nothing is skipped.** So the frame is being
built and shown; the content simply is not visible in it.

That removes the "a frame never reaches the screen" branch. What is left is
narrower: the geometry is drawn somewhere that is not what the display shows - the
wrong buffer, a surface cleared after the draw, or coordinates that put it off
screen - and the title survives because whatever draws it takes a different route.

## The next instrument, and why it is not a one-line patch

The question left is whether the pixels are in the back buffer **at the moment of
the swap**: non-black there means the draw works live and the swap loses it, black
means the draw never lands, and the two want opposite investigations.
`dkr_glide_read_pixel` already samples one pixel of the back buffer, so the
measurement itself is one call.

Putting it in `gl_present` was tried and withdrawn: **`glide_backend.c` contains no
logging at all** - not one `printf` - and that is deliberate, the file being a
library shared between the host replay and the Windows 95 target. Bolting a
logging dependency onto it to carry one diagnostic is a poor trade.

So the shape is an accessor - the backend exposes the sampled value, the game logs
it, as `dkr_glide_backend_*_stats` already do for the pass counters. That is a
small design rather than a patch, and it is where this stops rather than
compromising a file that has kept itself clean.

## The sampler is in place, reads black everywhere, and cannot yet be believed

`glide_renderer.cpp` samples one pixel of the back buffer immediately before
`backend_.present`, using the already-public `dkr_glide_read_pixel`, and logs it.
Every sample comes back `0x000000`.

**That is not yet evidence**, for two reasons, and both are worth more than the
reading:

* the first version logged the **first twelve presents**, which all happen during
  boot where the screen is legitimately black - it sampled the wrong window
  entirely, which is the same mistake as timing a witness badly and was caught the
  same way, by asking what moment the numbers belong to;
* the second version spreads the samples and still reads black - but there is **no
  positive control**. If this read returns black whatever the screen holds, in the
  live game, then "always black" says nothing at all. Nothing has yet shown the
  sampler returning a colour when a colour is on screen.

The control was added - a second pixel inside the title text, which the running
game draws in bright yellow on this very screen - and it **failed**:

    [gfx] before swap #200 title=0x000000 scene=0x000000
    [gfx] before swap #400 title=0x000000 scene=0x000000
    [gfx] before swap #600 title=0x000000 scene=0x000000

The title is visibly yellow while these are logged. So the sampler returns black
for a pixel that is demonstrably not black, **it does not work in the running
game**, and every reading it produced is void - including the ones that seemed to
say the draw never lands.

`dkr_glide_read_pixel` is sound in the replay harness; three witnesses depend on it
and `REPLAY.EXE` writes its images through the same path. What this shows is that
the back buffer is **not readable that way in the live game** - at that moment, in
that thread, with the Voodoo holding the screen. Bounding where that instrument can
be trusted is the one thing this attempt established, and it is worth having.

The sampler has been removed rather than left in place: an instrument known to be
broken, still logging numbers someone might read, is worse than no instrument.

So the buffer question is **unmeasured**, exactly as it was before, and the next
attempt needs a way to see the live frame that does not go through an LFB lock -
a screenshot compared against a capture replayed at the same moment, most likely,
since that is already known to work on both sides.

## Texture memory is not it either

The difference between the live game and the replay is *accumulated state*: the
running game has been uploading textures for minutes where `REPLAY.EXE` starts
clean. If the texture units were full and uploads refused, the scene would draw
without what it needs.

The game already logs those counters, so the question cost one `grep` of the
black-screen run's own log:

    refused-tmu=0      refused-aspect=0      reclaimed=0      (thirteen times)
    resident=4714, 9937, 44305, 55765, 67225, 78685           (climbing normally)

**Nothing refused, nothing reclaimed**, residency growing as it should. That branch
is closed too.

## The screen switches render target, and nothing in the port knows

This screen sets the colour image **twice**, to two different addresses:

    SetColorImage width=320 address=0x01000000     (four draws in the capture)
    SetColorImage width=320 address=0x02000000     (one)

and the live run's log carries the same pair. So the game is switching render
target on PLAYER SELECT, and asking for a 320-wide image where the frame buffer is
640.

`color_image_address` is decoded and stored - `f3ddkr.h:162`, four uses in the
decoder - and **nothing outside the decoder reads it**. Neither backend sees it.
The port has no concept of a render target at all: every draw lands in the one
frame buffer, whichever colour image the list selected.

That is a real architectural gap, measured rather than supposed, and it is the
first concrete mechanism that matches "drawn somewhere that is not what the display
shows".

**It is a candidate and not the diagnosis**, and the reason to be careful is on the
record already: the *oracle* ignores the switch in exactly the same way and renders
this capture correctly. So collapsing two targets into one is not sufficient on its
own to produce the blackness - something about the live run must make the same
collapse lose the scene where the replay's does not. What that is, is the next
question, and it now has a mechanism to hang on rather than a shrug.

### One comparison that does not work, recorded so it is not repeated

The obvious test of the candidate is whether the live game switches target more
often than the capture does - if it does, and nothing consumes the switch, content
could be overwritten repeatedly. The counts look damning at first glance:

    live run's log   4 SetColorImage lines in total
    one capture      5 SetColorImage lines in one display list

**They are not comparable.** The live trace is throttled to the first few lists, so
its four lines are an early sample and not a per-frame rate; the capture's five are
one list in full. Nothing follows from the comparison in either direction.

Measured properly, by exempting that one command from the trace budget for a single
run and then taking the exemption back out:

    2,433 SetColorImage in one run
    1,945 to 0x01000000
      488 to 0x02000000

**The second target is used constantly** - roughly one switch in five - so the gap
is structural rather than marginal. The game renders to two colour images as a
matter of course, and the port has never looked at which one.

That still does not prove it causes the blackness, for the reason already given:
the oracle ignores the switch identically and renders this capture correctly. What
it does establish is the size of what is being ignored, which was worth one run.

### And what it costs, which is nothing the corpus can see

Switching target often is not the same as *drawing* to the second one. The decoder
now counts draws whose colour image is not the first the list named, and across
every capture - `CAP_PS` included, the black screen's own - the count is **zero**.

Every draw goes to the first colour image. The second is selected and left again
without geometry in between, at least within a single display list.

So the gap costs nothing the corpus can measure, and the candidate is much weaker
than the 488 switches made it look. It is not dead: the 488 were counted across a
whole run of hundreds of lists, and a capture is one list, so a draw to the second
target in some *other* list remains possible. But the obvious version of the
story - "the scene is drawn to a target nobody shows" - is refuted for every list
this project can inspect.

The counter stays, silent unless a list ever does it, so that the day one does the
report says so.

## Where the defect stands

Established: the screen is black live and correct in its own capture; the list
carries the scene and the backend renders it on replay; lists, triangles and
presents all advance while the screen is black; no texture upload is refused.

Refuted: a transition frame (a second screenshot twelve seconds later is
identical); a frame that never reaches the screen (presents climb); the back-buffer
sampler (its own control failed); texture memory (counters clean).

Unmeasured: where, between the draw and the display, the content is lost - though
it now has a candidate, above, in a render-target switch no part of the port
consumes. Four branches are closed; this one has a mechanism and no measurement.

## The comparison that works, and it carries its own control

The route that does not need an LFB lock: hold the live screenshot against the
capture taken at that same moment, replayed through the oracle. Both already
existed; what was missing was a number rather than a description.

Exact pixels cannot be compared - the emulator window scales the 640x480 frame -
but region coverage can. Sampling the fraction of pixels above a dark threshold in
two regions, expressed relative to the frame in both images:

    region        capture    live
    the title       94 %      51 %
    the scene       98 %       0 %

**The scene is at zero.** Not dim, not partial - not one sampled pixel above the
threshold, where the capture has ninety-eight per cent of them.

And the title is the control, built into the comparison rather than bolted on: it
reads 51 % live, so the regions are aligned and the sampler works. Had the
alignment been wrong, the title would have read zero as well and the measurement
would have said nothing - which is precisely how the back-buffer sampler failed,
and why this one was arranged to fail loudly instead.

So the defect is total rather than partial, and it spares whatever draws the title.
That is the sharpest statement of it so far, and it cost no run: both images were
already on disk from earlier the same day.

## Why it matters beyond this screen

`win95-corpus.md` says the corpus measures agreement between two backends and not
correctness. This is the other half of the same limitation: **a capture cannot see
a defect that lives outside the display list.** The harness is blind to it by
construction, and the only instrument that found it was driving the game and
looking at the screen.

It also corrects a conclusion from earlier the same day. The black body was first
written down as "very probably not a defect", reasoning that a decoder rendering
GAME SELECT whole would not lose PLAYER SELECT's. That reasoning was sound and the
conclusion was wrong, because the fault is not in the decoder at all.
