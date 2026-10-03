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
- [x] The buffer size is justified by the 99th percentile of time per frame --
      checked on 3 October 2026 against the timing export. `audio_out.c` primes with
      two of DKR's blocks (720 to 848 frames each), about 1,520 frames or **69 ms** at
      22,050 Hz, and adds a third (104 ms) after an underrun. A one-player race at
      Ancient Lake with its audio running: frame period **p99 57.4 ms** inside the
      cushion, p99.9 78.9 ms and the maximum 89.3 ms inside the third block. The
      underruns measured on 2 October fall at the level-load freeze and nowhere
      else. A two-player race: p99 69.0 ms, at the cushion's edge, and p99.9
      100.4 ms, which the third block covers. *Corrected 3 October*: the first
      figures written here came from runs whose audio had stopped at the start. The ring's sixteen buffers of 4,096 frames hold far more than either;
      it is the cushion that decides, and the figures say two blocks is enough.
- [ ] No audible drift over twenty minutes of continuous play. **Not measured -- this
      box was ticked on 1 October 2026 on evidence that does not hold.** The "219,000
      blocks played, 0 underruns, 0 dropped" of E08-S04's long session were blocks
      *submitted*: every run on the test machine since at least 27 September logs
      `waveOut at 48000 Hz: refused (devices=0)` then the same at 22050 Hz, and
      `[audio][out] ... frames=0`. Windows 95 there has no wave device -- the
      configured Sound Blaster 16 has no driver installed (no SB16 file in
      `WINDOWS\SYSTEM`, only `wavemapper` under `[drivers]`; checked 2 October) --
      so nothing was ever played and nothing could underrun. The driver was
      installed on 2 October (E09-S01) and sound now plays (next criterion).
      *Partly measured since*: a run of 22 minutes of host time gave only **6.7
      minutes of guest time** -- 86Box runs this machine at about a third of real
      speed -- in the attract mode: 404 s of audio played, underruns 9 at the end of
      the start-up and 13 at the end, the four new ones at the attract sequence's
      level loads, **nothing dropped**; past the start-up the mixer produced 22,016
      frames a second against 22,050, the 0.15% short being the loads' pauses.
      No drift shows; twenty minutes of guest time need an hour of host time and
      are still to be run.
- [x] The underruns are counted and visible in diagnostics: `[audio][out] underruns=`
      in the log, and on the on-screen display (`OSD=1`). **Shown to work on 2 October
      2026**, once the test machine had a sound driver (E09-S01): a race at Ancient
      Lake, 2.23 million frames played (101 s), **9 underruns while the game loads**
      at 3 to 5 fps, **2 more at the race's level load** (a 1 s freeze), and **none in
      the 50 s of racing** that followed; nothing dropped.
- [ ] The output's CPU cost is measured, including with a poor driver. *Partly*: in a
      race, the sampler never finds `audio_out.c` among 43,335 samples of the executable
      (E08-S01) -- negligible with the test machine's driver; no other driver tried.
- [x] The equaliser's cost is measured, and keeping it is settled on that figure --
      **nothing, on this target**, read from the code on 3 October 2026: the
      equaliser, the master volume and the interface tones are applied in
      `runtime_platform.cpp`'s RT64 branch only; the Windows 95 branch hands DKR's
      samples to `waveOut` as they are, swapped into left-right order. The
      equaliser's source is compiled into `DKRR.EXE` and never called. Kept as it
      is: it costs no time here, and the modern build uses it. The master volume
      being absent here is a missing feature, not a cost.
- [x] The absence of a sound card lets the game go on running in silence: a refused
      `waveOutOpen` is logged and the game continues; with the output closed
      (`DKR_NO_AUDIO_OUT=1`) it ran 660 display lists in silence on the test machine
      (1 October 2026).

## Silent starts, found and fixed (3 October 2026)

Five starts in twenty-five between 1 and 3 October ran **with no sound at all**:
the game mixed its first audio task and never another. DKR's scheduler gives an
RSP task up after ten retraces (`__scHandleRetrace` in the decompilation's
`sched.c`) and drops its completion when it arrives; the audio manager then
waits for that completion for ever. The first audio task had grown from 13-26
ms to 44-329 ms because the card was being opened beside the game's boot since
1 October (`docs/research/win95-startup.md`), its driver taking the one
processor. The card is now opened at the lowest thread priority: six starts in
a row, the first task 18 ms each time and the audio running. The log line that
says it happened is `[boot][scheduler] dropped late SP completion`.

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
