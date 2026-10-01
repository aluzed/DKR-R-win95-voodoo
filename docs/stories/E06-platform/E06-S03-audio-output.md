# E06-S03 — Audio output

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E03-S02, E02-S03, E06-S01 |
| **Blocks** | E09-S04 |

## Context

The audio microcode produces buffers (E03-S02); they remain to be played back. SDL2
took care of that; under Windows 95, two routes:

- **`waveOut`**, from `winmm`: simple, universal, present on every installation. Its
  latency is high, of the order of several tens of milliseconds;
- **DirectSound**, from DirectX 3 onwards: far lower latency through a circular
  buffer, but it depends on the version of DirectX installed and on the quality of the
  sound card's driver.

DirectSound is preferable, with `waveOut` as a fallback. On 1998 hardware, driver
quality varies enormously from one card to another, and the fallback is not
theoretical.

Two constraints peculiar to this target:

- **the CPU budget.** DirectSound's software mixing consumes processor, and the budget
  is already tight. A sound card handling hardware mixing changes the picture;
- **synchronisation.** The game produces its audio at the N64's rate. The drift
  between the game's clock and the sound card's must be absorbed, failing which the
  buffer empties or overflows after a few minutes.

Volume control, the equaliser and the independent levels already exist in the
project's own code (`audio_equalizer.cpp`, `runtime_audio_controls.cpp`) and are kept —
subject to their CPU cost, to be measured.

## Objective

To play back the game's audio under Windows 95, without dropout or drift, within the
CPU budget.

## Scope

**In:** audio output, buffers, synchronisation, volume control.

**Out:** producing the buffers (E03-S02) and HLE mixing (E03-S03).

## Work

1. Implement the DirectSound output through a circular buffer, with a `waveOut`
   fallback.
2. Size the buffer. That is the central trade-off: a short buffer reduces the latency
   and increases the risk of a dropout when a frame exceeds its budget; a long buffer
   does the opposite. Set it on measurement, taking into account the 99th percentile of
   time per frame measured in E08-S01 — not the median.
3. Deal with clock synchronisation: detect the drift between the game's production rate
   and the card's consumption, and absorb it through the buffer's fill level rather
   than by resampling.
4. Deal with buffer underrun: detect it, count it, and make it visible in the
   diagnostic display (E08-S01). An audio dropout is the most audible symptom of a CPU
   budget overrun, and it is an excellent overall indicator.
5. Measure the output's CPU cost, DirectSound mixing included, and check that a poor
   driver does not make it explode.
6. Check the equaliser's cost (`audio_equalizer.cpp`) on the target and rule on
   keeping it. It is a per-sample treatment, hence potentially expensive on a Pentium
   II — if it weighs, it becomes an option disabled by default.
7. Check the behaviour when no sound card is present or the driver fails: the game must
   continue in silence, not stop.
8. Check over a long duration — at least twenty minutes of continuous play — that the
   synchronisation does not drift.

## Where it stands (1 October 2026)

The output is `waveOut`, a ring of sixteen 4,096-frame buffers with two blocks
of cushion before playback starts (`platform/win95/audio_out.c`), fed by the
high-level mixer (E03-S03). DirectSound is not used: `waveOut` has been enough
for every measurement in E08, and the criterion asking for DirectSound with a
`waveOut` fallback stays open.

## Acceptance criteria

- [ ] The audio is played back through DirectSound, with the `waveOut` fallback
      verified.
- [ ] The buffer size is justified by the 99th percentile of time per frame.
- [x] No audible drift over twenty minutes of continuous play -- measured rather than
      listened to: over 123.7 minutes of the game (E08-S04's long session), 219,000 blocks
      played, **0 underruns and 0 dropped**, and the mixer at full rate in every race since.
- [x] The underruns are counted and visible in diagnostics: `[audio][out] underruns=`
      in the log, and on the on-screen display (`OSD=1`).
- [ ] The output's CPU cost is measured, including with a poor driver. *Partly*: in a
      race, the sampler never finds `audio_out.c` among 43,335 samples of the executable
      (E08-S01) -- negligible with the test machine's driver; no other driver tried.
- [ ] The equaliser's cost is measured, and keeping it is settled on that figure.
- [x] The absence of a sound card lets the game go on running in silence: a refused
      `waveOutOpen` is logged and the game continues; with the output closed
      (`DKR_NO_AUDIO_OUT=1`) it ran 660 display lists in silence on the test machine
      (1 October 2026).

## Risks

On this class of machine, the audio dropout will be the first visible symptom of an
exceeded CPU budget — even before the frame rate falls, because the ear is more
sensitive than the eye to irregularities. The underrun counter is therefore a measuring
instrument for the whole project, not only for the audio.

## References

- `runtime-recomp/src/game/audio_equalizer.cpp`, `runtime_audio_controls.cpp`
- `runtime-recomp/src/game/audio_mix_policy.hpp`
- E03-S02 — producing the buffers
- E08-S01 — per-frame budget
