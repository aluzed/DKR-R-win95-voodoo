# E09-S02 — Visual comparison harness

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E00-S07, E04-S02, E04-S08, E09-S01 |
| **Blocks** | E05-S03, E08-S03, E09-S04 |

## Context

The port produces images. The only way to know whether they are right is to compare them
against a reference, and to do it automatically — comparison by eye does not detect
progressive deviations, and it does not scale to several hundred scenes.

Three references are available, of decreasing fidelity and increasing convenience:

1. **The console or a reference emulator** — the truth, but heavy to instrument;
2. **DKR-R in Accurate mode on RT64** — kept as the oracle by E00-S07's ADR, and easy to
   drive;
3. **E04-S08's software rasteriser** — implements the RDP's combiner faithfully, without
   Glide's constraints.

The third is the most useful in practice, because it isolates a single variable: if the
software rendering is correct and the Glide rendering wrong, the decoder is out of the
question and the error is in the backend. It is that isolation which gives E05-S03 its
effectiveness.

The central building block is **display-list capture**. Once we know how to record a
session's graphics tasks and replay them, the comparison becomes deterministic and
reproducible — which a play session played by hand never is.

## Objective

To deliver a harness that captures, replays and compares the images between the three
backends, automatically.

## Scope

**In:** capture, replay, comparison, report, integration into continuous verification.

**Out:** validation on real hardware (E09-S04).

## Work

1. Implement the capture of the graphics tasks: `OSTask` and the associated RDRAM
   snapshot, written to a file. The format must be stable and documented, those captures
   being intended to serve for a long time.
2. Build a set of captures covering the game: title screen, menus, character selection, a
   lap on each level, cutscenes, split screen, results screen. It is the project's
   reference corpus, and its coverage decides what the harness can detect.
3. Implement the replay: reload a capture and submit it to the chosen backend, without
   running the game. The replay is deterministic, which makes every comparison
   reproducible.
4. Implement the comparison: per-pixel difference, with a metric tolerant of the
   reduction in colour depth — Glide renders in 16 bits, an exact difference would be
   unusable. The metric must distinguish "quantised differently" from "wrong".
5. Produce a visual report: reference image, image obtained, difference map, metric. It
   is that report which will make E05-S03 practicable.
6. Integrate it into the verification: a visual regression must announce itself
   automatically, with a per-scene threshold rather than a global one — some scenes are
   intrinsically closer than others.
7. Provide for replay on the target machine, in order to compare the real Glide rendering
   against the reference obtained on the host.
8. Document the procedure in `docs/VISUAL-TESTING.md`.

## Acceptance criteria

- [x] The graphics task captures can be recorded and replayed. `DKR_CAPTURE_LIST=<n>`
      writes the list's start address and the whole of RDRAM; `replay` renders it
      through the oracle on the development machine and `REPLAY.EXE` through the
      oracle and the card on the target. Two replays of one capture produce
      byte-identical files, which was checked and not assumed.
- [ ] The corpus covers title, menus, every level, cutscenes, split screen and results.
- [x] The replay is deterministic — the comparison of the counts of triangles emitted
      precedes that of the images, precisely so that an image deviation does not mask a
      determinism defect. Established on a **real** capture on 3 September 2026: the two
      backends decoded `cmd=1539 tri=943 emitted=510 rejects=0` identically, and the
      software rasteriser produced the same image on the development machine and on the
      target — 0 divergent pixels out of 307,200 across two compilers and two
      architectures, x86-64 with SSE against i686 with x87.
- [x] The comparison metric distinguishes quantisation from error — the reference is
      quantised to 565 before comparison, with the same replication of the high-order
      bits as the read-back, and the edge pixels are counted separately. The wide
      threshold that served to clear the ground is doubled by a tight one once the real
      noise is measured: worst deviation 9 out of 255, bound set at 16. A threshold one
      does not tighten after measuring asserts nothing but its own indulgence.
- [x] The report presents reference and obtained as 24-bit BMPs brought back to the host,
      plus the metrics — area painted on either side, divergent pixels, edge pixels,
      worst deviation and its position — **and the difference image**, which the harness
      now draws: a real divergence in red, one forgiven as an edge in dim amber, over the
      reference darkened so the scene stays readable. Written by `REPLAY.EXE` on the
      machine that made both images, and by `tools/render/compare.c` on the host.
- [ ] A visual regression is reported automatically, with a per-scene threshold.
- [x] The replay works on the target machine with the Glide backend — the same scene
      crosses the complete chain to the rasteriser then to the Voodoo, whose frame buffer
      is read back. Result: 0 divergent pixels out of 307,200, after correcting three
      defects **all of them located in the oracle**.
      See `docs/research/win95-oracle-vs-card.md`.
- [x] The capture format is documented — `docs/VISUAL-TESTING.md`, field by field, with
      the reason each field is in the file rather than assumed by the reader.

> **Correction of 3 September 2026**: the metric's edge rule was forgiving far too much.
> It absolved any pixel standing next to a sharp change in the reference — right for four
> large triangles, wrong for text, where glyphs one to three pixels wide are edges
> throughout. It reported 186 divergent pixels for a nameplate with 2416 wrong ones. A
> pixel is now forgiven only if the card's colour matches one of the reference's
> neighbours, which is the whole of what a half-pixel fill-rule shift can produce. The
> count went from 186 to 5071 on that image, and from 0 to 0 between the two oracles —
> the control that says the extra pixels are real. See
> `docs/research/win95-oracle-vs-card-capture.md`.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

An incomplete corpus gives false confidence: what it does not cover will not be detected,
and the absence of an alert will be read as an absence of problems. Coverage by level and
by game mode is therefore an acceptance criterion in its own right, not a later
refinement.

## References

- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — snapshot per task, already in place
- E04-S08 — reference rasteriser
- E00-S07 — keeping the RT64 oracle
- E05-S03 — the harness's main consumer
