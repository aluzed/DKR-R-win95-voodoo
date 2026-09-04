# The corpus of captures

E09-S02 asks for coverage: title, menus, a lap of each level, cutscenes, split
screen, results. This records what exists, how it was obtained, and what it has
already shown.

## What is in it, 4 September 2026

| capture | scene | commands | triangles | emitted | textures |
|---|---|---|---|---|---|
| `CAP0050.BIN` | the Nintendo 64 logo, intro | 640 | 419 | 234 | 35 |
| `CAP0150.BIN` | the copyright screen | 459 | 293 | 99 | 18 |
| `CAP0250.BIN` | the hub, Pipsy on the beach | 3592 | 1293 | 904 | 190 |
| `CAP0400.BIN` | Ancient Lake, Bumper racing | 1539 | 943 | 510 | 95 |

Four scenes that share almost nothing: one large model on a sky, a mostly
two-dimensional screen with text, an outdoor hub with 190 textures, and a race
with five-pass text. That is coverage of a kind — of the *decoder's* paths, not of
the game — and it is what the harness needed to stop being a one-scene instrument.

**It is four, and the ticket asks for a lap of each level.** Recorded as
incomplete rather than presented as a corpus.

## How they were obtained, and what that cost

`DKR_CAPTURE_LIST=50,150,400` and the like, several to a run. The comma is what makes a corpus
practical: a capture costs a boot, a launch and a wait, so one per run is a day's
work for a dozen scenes.

Two obstacles were met on the way and both are recorded elsewhere:

- **Five captures of six were lost to the disk cache.** `fclose` hands the bytes
  to Windows 95 and Windows 95 keeps them; stopping the emulator took them. The
  writer commits before closing now. See the commit of 4 September 2026.
- **A run does not reach an arbitrary list, and that is what bounds the corpus.**
  Measured on 4 September 2026 over four runs: the game reaches display list
  **300** and stops there, in `gGameMode=1 (MENU)`, with no error and with its
  window still answering the message loop. A single capture armed at list 800 was
  never written. Two armed at 200 and 250 were both written, so it is not the
  captures that shorten the run — the run is short.

  The log is durable (`dkr_diag_commit` calls `FlushFileBuffers` at every report),
  so this is read from the last report rather than inferred: `list=300 cmd=291998
  tri=152849 emitted=92252 rejects=0`, and nothing after it.

  That is well short of the 1500 lists E02-S06 recorded, and it is why the corpus
  has no lap of any level: the game does not get there. **Recorded as a limit on
  E09-S02's coverage and as a question for E02-S06, not diagnosed here.**

## What the corpus has already shown

**The copyright screen renders a blank shape.** The Rare logo comes out as a flat
yellow rounded rectangle, with the copyright text legible below it. The **oracle**
produces that, so it is not the card: it is the decoder or a texture that never
arrives. Eighteen textures for that screen, against ninety-five for the race.

**And the hub draws large grey rectangles over the scene.** Several flat
light-grey quads and one black one sit across Pipsy and the water in
`CAP0250.BIN`, again in the **oracle**. A flat untextured quad where a textured
one belongs is the signature of a texture that never arrived or a combiner that
fell through; 190 textures is twice any other scene in the corpus, so a cache or
an allocator limit is the first thing to ask about.

Neither is explained. `conversions: distinct-keys=64 overflow=48` in the same
report is **not** the cause, tempting though it looks: that counter says the
64-slot set used to *count* distinct tiles ran out of room, which is a limit on
the measurement and not on the rendering. Checked rather than assumed, because a
plausible number next to a defect is how a wrong diagnosis starts.

Both are recorded and not chased. That is exactly what a corpus is for — defects
that no amount of looking at the race would have found — and each is now a frozen
input that can be pointed at whenever E04 or E05 next has a hypothesis.

## Checking it

    tools/render/check-corpus.sh <corpus-dir>

Counts first, then images, with a per-scene threshold. See
`docs/VISUAL-TESTING.md`.

The four scenes replay to their references at **0 divergent pixels of 307,200**,
which is what one expects of a deterministic replay and which is checked rather
than assumed — it is the property every other measurement in this harness rests
on.

## Where it lives

Outside the repository. A capture is eight mebibytes and has to be: the decoder
reads at addresses the display list itself computes, so there is no knowing in
advance which bytes matter. Four scenes are thirty-two megabytes; a lap of each
level would be hundreds. What is versioned is the script, and the counts, which
are text.
