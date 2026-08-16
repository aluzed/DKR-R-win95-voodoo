# E04-S08 — Reference software rasteriser

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | L |
| **Depends on** | E04-S01, E04-S05, E02-S06 |
| **Blocks** | E05-S01, E09-S02 |

## Context

This ticket is the pivot of the whole of epic E05, and its justification holds in one
sentence: when an image is wrong in Glide, we shall have to know whether the error
comes from the decoder or from the backend.

Without an intermediate oracle, a wrong pixel may come from ten different stages —
transformation, clipping, texture decoding, combiner translation, Glide setting,
driver. With a software rasteriser implementing the **same backend interface**
(E04-S01), the question is settled in one run: if the software image is correct and
the Glide image wrong, the decoder is out of the question.

That also makes it the project's first **visual** milestone: the game's first image
displayed by this port, before any line of Glide.

## Objective

To deliver a software rasteriser implementing the backend interface, capable of
displaying the game — slowly, but correctly.

## Scope

**In:** rasterising triangles and rectangles, texturing, depth, blending, output to an
image.

**Out:** any optimisation. It is explicitly permitted to be slow: this backend is a
measuring instrument, not a way of playing.

## Work

1. Implement E04-S01's interface: state management, textures, drawing, presentation.
2. Implement triangle rasterisation with perspective interpolation of the attributes —
   colour, texture coordinates, depth. Perspective correction is indispensable:
   without it, textures wobble, and that is precisely the kind of artefact we shall
   later be tempted to blame on Glide.
3. Implement texture sampling according to E04-S06's modes: wrapping, mirroring,
   clamping, point and bilinear filtering.
4. Implement the RDP's combiner **faithfully**, without Glide's constraints. That is
   what gives this backend its value as an oracle: it shows what the image ought to
   be, and E05-S03 measures its deviation from that reference.
5. Implement the depth buffer, blending and the alpha test.
6. Output to an image in memory, displayable by the window (E06-S01) and savable to a
   file for the comparison harness (E09-S02).
7. Make it usable on the modern host as well as on the target. On the host, it is fast
   enough to be comfortable in development; on the target, it will serve occasionally
   for diagnosis.
8. Check it against the modern target on captured scenes: the title screen, a menu,
   two levels.

## Acceptance criteria

- [x] The rasteriser implements E04-S01's interface with no dependency on Glide.
- [x] The perspective interpolation is correct, verified on a textured surface seen
      obliquely — **measured** against the analytic value: 0.2039 against 0.20
      expected. The self-test corrected the check along the way: forgetting the
      division does not give 0.50 but 0.125, so that the first version passed on a
      broken rasteriser.
- [~] The sampling and filtering modes are covered — repeat, clamp, mirror, point and
      bilinear, each verified at a known coordinate. **But E04-S06 is still `TODO`**:
      the list of the modes DKR really uses does not exist, and what is covered is
      what the interface defines, not what the game asks for.
- [~] The combiner is implemented faithfully, with no hardware constraint — for the
      interface's four modes. **The RDP's combiner has two stages with four inputs**,
      and inventorying what DKR uses of it is E04-S06's work.
- [x] Depth, blending and the alpha test work — depth including its **independence
      from the order of emission**, which a depth buffer promises and which one easily
      forgets to check.
- [ ] The game displays a recognisable image: title screen, menu, and a race —
      **blocked** by E02-S06, which requires the ROM.
- [x] The images produced are savable to a file — 24-bit BMP, header and dimensions
      read back by the suite.
- [x] It works on the modern host and on the target, and both produce **byte-for-byte
      identical** files — which authorises comparing an image produced here against an
      image produced there.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

The trap is to spend too much time on it, or to yield to the temptation of optimising
it. This backend does not have to be fast — it has to be **right** and **simple**,
because its entire value lies in the trust placed in it as a reference. An optimised
rasteriser is a rasteriser whose correctness must itself be checked, and the oracle
disappears.

## References

- E04-S01 — interface implemented
- E04-S06 — state modes to honour
- E09-S02 — comparison harness, the main consumer
- `../../Diddy-Kong-Racing/docs/stories/E04-hle-f3ddkr/E04-S08-rasteriseur-software-reference.md`
  — the same architectural choice on the native port's side
