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
