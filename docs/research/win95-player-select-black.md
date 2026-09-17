# PLAYER SELECT is black in the running game and correct in its own capture

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
