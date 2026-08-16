# E05-S01 — Glide initialisation, context and buffers

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | DONE |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E00-S05, E04-S01, E04-S08 |
| **Blocks** | E05-S02, E05-S03, E05-S05, E05-S07 |

## Context

Glide is a low-level API, specific to 3dfx, with no abstraction layer: it exposes the
hardware directly. That is good news for this project — the correspondence with the
RDP is more direct than with the Direct3D of the period — but it means every
configuration detail counts.

The structuring points of the initialisation:

- **The display mode.** On Voodoo 1 and 2, the card is a *passthrough* accelerator:
  it takes over full screen and has no windowed mode. Banshee and Voodoo 3, by
  contrast, are complete 2D/3D cards. That changes the nature of the integration with
  E06-S01's Win32 window.
- **The buffers.** Double or triple buffering, a 16-bit depth buffer, all of it having
  to fit in the card's frame-buffer memory — which, on a Voodoo 2, limits the
  resolutions available.
- **The resolution.** Fixed by E00-S05's ADR, with the lower resolutions as a fallback
  if the fill budget does not hold.

## Objective

To deliver `platform/render/glide_backend.{h,cpp}`: opening the Glide context,
configuring the buffers, presenting, and closing cleanly.

## Scope

**In:** initialisation, buffers, swap, shutdown, card detection.

**Out:** textures (E05-S02), combiner (E05-S03), and all of the rendering.

## Work

1. Link against Glide. Decide between static import linking and dynamic loading
   through `LoadLibrary`: dynamic loading allows an explicit error message when the
   card is absent, rather than a load refusal by the OS. Given that E01-S04's guard
   rail already distinguishes DLLs supplied by a driver, dynamic loading is probably
   the right choice.
2. Implement detection: presence of the library, number of cards, number of TMUs,
   memory available per TMU and for the frame buffer. Those figures drive E05-S02 and
   E05-S04 at run time; they must not be hard-coded.
3. Open the context at the retained resolution, with the chosen depth-buffer format.
   Check that the requested configuration fits in the detected card's memory, and fall
   back cleanly otherwise.
4. Implement the frame cycle: clear, draw, buffer swap. Decide between a swap
   synchronised on the scan and an immediate swap — the first avoids tearing, the
   second avoids losing a whole frame when the deadline is missed, which counts when
   the budget is tight. That decision is measured (E06-S04).
5. Translate E04-S05's scissor window into `grClipWindow`.
6. Implement shutdown: restoring the display mode, releasing the context. On a
   passthrough Voodoo, an incorrect shutdown leaves the screen in an unusable state
   and forces a reboot — a very penalising defect during debugging, where abnormal
   stops are frequent.
7. Deal with the abnormal stop: install a handler that restores the display even in
   the event of a crash.
8. Display a triangle. It is the project's first Glide pixel, and it is worth several
   days of reading documentation.

## Acceptance criteria

- [x] Detection reports the card, the number of TMUs and the memories available —
      recorded on the machine: 1 card, 2 TMUs, 2048 KB of frame buffer, 2048 KB per
      TMU.
- [x] The absence of a card produces a clear message, not a load refusal by the OS —
      **exercised** by removing the Voodoo from the emulator's configuration, then
      putting it back, the configuration restored byte for byte. The clean path is
      reached and `DKR_GLIDE_ERR_NO_BOARD` comes back.
      But the measurement revealed better than what the criterion asked for: **Glide
      displays its own modal English box during the `LoadLibrary`**, before our code
      sees anything at all. The registry does not allow us to get ahead of it — the
      readings with and without the card are identical, and a Voodoo 1 that never
      existed figures there as the card present. The error text therefore explicitly
      ties in the box that precedes it.
      See `docs/research/win95-glide-no-card.md`.
- [~] The context opens at the ADR's resolution — 640×480, double buffer, depth — **the
      fallback stays unverified**: 86Box offers no Voodoo poor enough to make 640×480
      fail, and the memory has always sufficed. The computation is written and
      reread, it is not exercised. The budget is computed rather than guessed, so that
      the failure says "640×480 does not fit in 2 MB" instead of a mute refusal.
- [x] A coloured triangle is displayed under Windows 95 on the target, **and the image
      has been read back**. `grLfbLock` gets round the obstacle of the passthrough
      Voodoo, whose output appears in no capture from the emulator: 75,264 pixels
      painted out of 307,200, that is exactly the triangle's analytic area
      (½ × 448 × 336). The centre is red-dominant, which is the right answer and not
      the obvious one — the centre of the screen is not the triangle's centroid. The
      whole image is brought back as a BMP.
- [x] The frame cycle runs at a stable rate — 100 frames in 1573 ms, that is 63
      frames/s, swap synchronised on the scan. That shows the cycle is not the
      bottleneck at trivial load, and nothing about the real fill rate.
- [x] Shutdown restores the display, including after an abnormal stop — exercised in
      both directions, the witness's "crash" mode dereferencing a null pointer with
      the context open. The desktop comes back before the filter's box.
- [x] The scissor window works, measured rather than observed: a symmetric triangle of
      112,000 pixels, restricted to the right half, keeps 55,960 of 56,000. The 40
      missing are the boundary column counted once — Glide's window is therefore
      inclusive on the left and exclusive on the right, which `backend.h` assumed
      without having checked it. E04-S05's `dkr_scissor_for_player` supplies the
      rectangles.
- [x] The layer implements E04-S01's `dkr_render_backend`: blending, depth, scissor,
      alpha test and fog are translated, and **the eight texture-free modes are
      confirmed by reading the frame buffer back**. The textures are left to
      E05-S02/E05-S03, and `texture_upload` returns zero rather than a bogus handle.
      See `docs/research/win95-glide-states.md`.
- [x] No hardware capability is hard-coded — TMUs and memories come from
      `grSstQueryHardware`, and the resolution fallback from the computed budget.

## Risks

Glide is no longer a living API: the official documentation is of its period, and the
implementations available are 3dfx's open sources and their forks. Allow time for
reading those sources — the gap between the documentation and the driver's real
behaviour will have to be settled by experiment.

Development under an emulator (E09-S01) is indispensable here: the "modify, run,
reboot the machine" cycle on real hardware would be unbearable at the rate we grope
around at this stage.

## References

- [3dfx Glide sources](https://sourceforge.net/projects/glide/) ·
  [sezero/glide](https://github.com/sezero/glide) ·
  [hatarch/glide3x](https://github.com/hatarch/glide3x)
- E00-S05 — hardware target and Glide version
- E04-S01 — interface to implement
