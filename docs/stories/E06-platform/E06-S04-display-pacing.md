# E06-S04 — Display pacing and synchronisation

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E02-S03, E05-S01 |
| **Blocks** | E08-S01 |

## Context

DKR runs at 30 frames per second on the N64, the rate at which its simulation is
calibrated. `vi_presentation_policy.hpp` implements that presentation policy today, and
the Modern mode adds an interpolation towards high refresh rates — a feature that
disappears with the "Accurate" profile alone (E07-S01).

What remains is simpler but not trivial: presenting a frame every 33.3 ms, on a 3dfx
card whose buffer swap synchronises on the monitor's scan.

That is where the knot is. A 1998 monitor at 640 × 480 typically scans at 60, 72 or
85 Hz. At 60 Hz, a game frame occupies exactly two scans — the ideal case. At 72 or
85 Hz, the ratio is no longer whole, and synchronised presentation will produce a
regular stutter. And if the game misses its deadline, the synchronised swap waits for
the next scan, which loses a whole frame: at 30 frames per second, losing one shows a
great deal.

## Objective

To present the game at its original rate, regularly, on the display modes available on
the target.

## Scope

**In:** the presentation rate, synchronisation, measuring regularity.

**Out:** interpolation towards high rates, removed along with the Modern mode.

## Work

1. Survey the refresh rates really available at the retained resolution, on the target.
2. Settle between a synchronised swap and an immediate swap. The synchronised one
   avoids tearing; the immediate one avoids losing a whole frame when the deadline is
   missed. On a machine with a tight budget, the second may be the better choice — to
   be decided on measurement, and to be made configurable (E06-S05).
3. Deal with rates that are not multiples of 30 Hz: present at the nearest scan,
   measuring the stutter induced. Document the recommended display mode.
4. Implement rate regulation on E02-S03's clock, decoupling the simulation rate from
   the presentation rate. The simulation must advance at 30 Hz whatever happens, failing
   which the game runs in slow motion or fast forward.
5. Deal with the case of an exceeded budget: when a frame takes more than 33.3 ms,
   decide between skipping a presentation and letting the simulation fall behind. The
   console's behaviour is the reference.
6. Measure the regularity: the distribution of the times between presentations, not
   only the mean. A mean of 30 frames per second with an irregular distribution gives a
   game that feels bad despite a correct figure.
7. Check the consistency with the audio synchronisation (E06-S03): the two clocks must
   stay in agreement over time.

## Acceptance criteria

- [ ] The rates available at the retained resolution are surveyed.
- [x] The synchronised / immediate choice is justified by a measurement, and
      configurable — `DKR_GLIDE_SWAP=immediate`, announced once in the log so a
      switch that fails to take cannot be read as a switch that changed nothing.
      Measured 21 September 2026 on the test machine: **no difference**, 67.7%
      against 67.9% of wall in guest execution and the same frame period, so the
      synchronised swap is free here and keeps its no-tearing. The emulated Voodoo
      does not make the caller wait for a scan; on real hardware it would, so this
      is a fact about the bench and E09-S04 still owns the question.
      See `docs/research/cpu-budget.md`.
- [x] The simulation rate stays at 30 Hz independently of the presentation --
      **measured against the game's own race timer**, 2 October 2026. Two F9
      captures in a race, their lists stamped on the guest's clock: 70.801 s
      showing `00:01:74`, 92.638 s showing `00:20:29`. 21.84 s of guest time, of
      which 3.42 s the first capture's frame dump held the renderer; the game
      caps its catch-up at six retraces (`LOGIC_10FPS`), 0.10 s, over such a
      stall. Expected 18.52 s on the timer, read **18.55 s** -- the HUD's
      hundredths -- with frames at 30 and 20 fps throughout.
- [x] The behaviour on a budget overrun is decided and documented -- **the
      console's, because it is the game's own code**. `thread3_main.c` asks
      `fb_update` how many VI retraces the last frame took and advances the
      logic by that many sixtieths, capped at six: a frame that misses 33.3 ms
      is shown late, and the simulation does not fall behind, down to 10 fps.
      Below that the game slows, as on the N64. The port adds nothing on top:
      the VI thread counts retraces on E02-S03's clock, which is what
      `fb_update` reads.
- [x] The regularity is measured as a distribution, not as a mean -- the timing
      export's periods binned in retraces (`racewin.py`), a one-player race at
      Ancient Lake with its audio, 2 October 2026: 1% at 60 fps, **78% at 30**,
      19% at 20, 1% at 15; mean 36.7 ms, median 33.5, 99th percentile 57.4. A
      two-player race: 75%, 14% and 9% at 15 fps
      (`docs/research/win95-two-players.md`). *Corrected 3 October*: the figures
      first written here came from runs whose audio had stopped at the start.
- [x] The recommended display mode is documented -- **it is not left to the player**:
      `glide.c` opens the card at 640 x 480 and asks `grSstWinOpen` for
      `GR_REFRESH_60Hz`, the ticket's ideal case, two scans per game frame at 30 fps.
      Every monitor of the period shows 640 x 480 at 60 Hz. The package README says
      so (3 October 2026). Glide's own `SST_SCREENREFRESH` variable, set by some 3dfx
      control panels, can override it; that and other monitors' rates belong to the
      survey above, which needs the hardware (E09-S04).
- [x] No drift between the audio clock and the video clock over twenty minutes --
      20.6 minutes measured, 3 October 2026: over 1,237 s of guest time the mixer,
      driven by the game's frames, produced 22,037 frames a second for 22,050 the
      sound card played; the queue ran dry only at three level loads and never
      overflowed (E06-S03).

## Risks

An irregular rate is perceived as poor performance even when the mean number of frames
per second is correct. That is particularly true at 30 Hz, where every frame counts
double. Step 6's distribution measurement is what allows "slow" to be distinguished
from "irregular" — two problems whose remedies have nothing in common.

## References

- `runtime-recomp/src/game/vi_presentation_policy.hpp`
- `runtime-recomp/src/game/interpolation_state_policy.hpp` — removed with E07-S01
- E02-S03 — time base
