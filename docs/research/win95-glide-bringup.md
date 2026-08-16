# The first Glide pixel goes through the engine's own layer

A report from [E05-S01](../stories/E05-glide/E05-S01-glide-init-and-buffers.md),
14 August 2026, on the test machine — a Voodoo 2 with 2 MB of frame buffer and
2 MB per TMU, ADR 0002's **floor configuration**.

```text
detection          : success
Glide version      : 0x254
boards             : 1
TMUs               : 2
frame buffer       : 2048 KB
  TMU 0 memory     : 2048 KB
  TMU 1 memory     : 2048 KB
two TMUs required  : YES
opening 640x480    : success
resolution obtained: 640x480, 2 buffers, depth yes
100 frames in      : 1573 ms
frame rate         : 63 frames/s
close              : display restored
second close       : no effect, as expected
```

E09-S01 had already drawn a triangle. What changes here is that the path taken is
**the one the engine will use** — `platform/render/glide.c` — and not a throwaway
demonstration. What is proved is therefore what will serve.

## What the frame rate says, and what it does not

63 frames per second on one clear, one triangle and one swap. The swap is
synchronised to the scan, and the measurement merely shows that **the frame cycle
is not the bottleneck** under a trivial load.

It says **nothing** about the real game's fill rate, which is E08's question. One
triangle is not a scene.

## The display is restored, including after a crash

That is the property that counts most in practice, and it is measured in both
directions.

On a Voodoo 1 or 2, the card takes over through an analogue relay: as long as the
context is open, the screen shows the 3dfx output and not the 2D card's. A missed
close therefore leaves a black screen that only a reboot recovers — throughout
E05's development, where one crashes often, that is the difference between ten
seconds and two minutes per mistake.

The witness's "crash" mode opens the context then dereferences a null pointer. The
chain under trial is complete:

1. `dkr_win95_startup` installs the exception filter (E02-S03);
2. `dkr_glide_open` registers the restoration in the abnormal-exit registry;
3. the filter runs the registry **before** displaying anything at all.

Result on the machine: the Windows desktop reappears, then the filter's box. No
black screen.

## Two traps, one of which only the machine could tell us about

### A detection with side effects is not a detection

The first version unloaded and reloaded `glide2x.dll` on every call to
`dkr_glide_detect`. The witness called `detect` then `open`, which detected again:
the second `grGlideInit` found a library still holding the card, and Glide refused
with

```text
Mutual exclusion prohibits this
```

— a message that does not name its cause. The detection is now idempotent and
returns what it already knows.

### `grSstQueryHardware` does not distinguish the generations

Recorded in E00-S05 and confirmed here: the returned type is `0` for a Voodoo 2 as
for a Voodoo 1, and the FBI revision is identical. The detection therefore rests on
the **number of TMUs and the memory per TMU** — which are, moreover, the only two
things the engine needs in order to decide between one pass and two. See
[`win95-voodoo2-machine.md`](win95-voodoo2-machine.md).

## What is not done

E05-S01 is **not** finished, and two of its dependencies are still `TODO`:

- **E04-S01**, the backend interface this layer will have to implement. It does not
  exist; `glide.h` therefore exposes a narrow API that predates it, and which will
  adapt to it without the implementation changing.
- **E04-S05**, from which comes the scissor window to translate to `grClipWindow`.

Also untried, for want of being able to provoke them without breaking the machine:
the resolution fallback — the memory sufficed — and the message returned when the
card or the library is absent. Both paths exist and return a text naming the action
available; neither has been exercised.

## Reading the frame buffer back — the first image really seen

Until now, everything this document asserted about the Glide rendering rested on
**the absence of a crash**. On a passthrough Voodoo, the screen belongs to the
card: the analogue relay cuts the 2D output as long as the context is open, and no
capture of the emulator shows the 3dfx output. We knew `grDrawTriangle` returned;
we did not know it painted.

`grLfbLock` / `grLfbUnlock` lift that blindness. `dkr_glide_read_framebuffer` locks
the front buffer, reads 565, and returns 32-bit ARGB in the software rasteriser's
order — so that the two images can be compared without conversion.

### The 565 conversion replicates the high bits

A simple left shift would give 0xF8 for maximum red: white would be grey, and
**every comparison against the oracle would inherit a constant deviation** that
would be blamed on the rasteriser. Replicating does give 255.

Recorded on the machine, at the trial triangle's red vertex: `(247, 0, 0)`. That is
not 255, and it is correct: 247 is 30 in 5 bits, not 31 — the sampled point is
eight lines below the vertex, already inside the gradient. A 240 would have flagged
truncation; a 247 flags interpolation.

### What the card drew

    resolution obtained: 640x480, 2 buffers, depth yes
    frame rate         : 64 frames/s
    pixels painted     : 75264 out of 307200
    centre of screen   : 0x7B3C39

The triangle's vertices are at (320,72), (544,408) and (96,408). Its analytical
area is ½ × 448 × 336 = **75264 pixels — exactly the count recorded**. The card
therefore fills the geometric surface with neither overrun nor shortfall, and its
edge fill rule counts each pixel only once.

The centre `0x7B3C39` is red-dominant, which is the right answer and not the
obvious one: the centre of the *screen* (320,240) is not the triangle's centroid
(320,296), it is closer to the red vertex. An equal-parts blend would on the
contrary have betrayed a wrong interpolation.

### A trap in the witness itself

The first version counted "pixels painted" after the frame-rate loop, which clears
to a gradient: the 307200 pixels were painted and the verdict was positive without
proving anything. The witness now draws one last frame **on a black background**
before reading. The lesson goes beyond Glide: a counter whose background value is
not distinguishable from the result measures nothing.

The complete image is written as a 24-bit BMP (`D:\GLIDEBK.BMP`), readable from the
host — it is what will serve as input to E09-S02's comparison.
