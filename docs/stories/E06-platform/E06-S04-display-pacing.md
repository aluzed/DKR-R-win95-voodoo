# E06-S04 — Display pacing and synchronisation

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | TODO |
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
- [ ] The synchronised / immediate choice is justified by a measurement, and
      configurable.
- [ ] The simulation rate stays at 30 Hz independently of the presentation.
- [ ] The behaviour on a budget overrun is decided and documented.
- [ ] The regularity is measured as a distribution, not as a mean.
- [ ] The recommended display mode is documented.
- [ ] No drift between the audio clock and the video clock over twenty minutes.

## Risks

An irregular rate is perceived as poor performance even when the mean number of frames
per second is correct. That is particularly true at 30 Hz, where every frame counts
double. Step 6's distribution measurement is what allows "slow" to be distinguished
from "irregular" — two problems whose remedies have nothing in common.

## References

- `runtime-recomp/src/game/vi_presentation_policy.hpp`
- `runtime-recomp/src/game/interpolation_state_policy.hpp` — removed with E07-S01
- E02-S03 — time base
