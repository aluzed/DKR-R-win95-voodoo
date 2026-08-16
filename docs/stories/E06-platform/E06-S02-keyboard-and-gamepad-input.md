# E06-S02 — Inputs: keyboard and gamepad

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | TODO |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E06-S01 |
| **Blocks** | E09-S04 |

## Context

`runtime_input.cpp` (25 KB) handles keyboard, gamepads and gyroscope today through
SDL2, with complete remapping. The mapping logic towards the N64 controller is the
project's own code, portable, and it must be kept; it is the acquisition layer that
changes.

Under Windows 95, two routes:

- **DirectInput**, from DirectX 3 onwards, which handles gamepads and joysticks
  uniformly;
- **`winmm`'s joystick API** (`joyGetPosEx`), older, simpler, available everywhere, but
  limited in the number of axes and buttons.

DirectInput is the right route, with `winmm` as a fallback if the gamepad's driver does
not expose DirectInput.

Two differences of the period to take in: the gamepads of 1998 are analogue but often
badly calibrated, and Windows's control panel exposes a system calibration that has to
be taken into account. And there is no vibration — DKR does not use the Rumble Pak, so
that is of no consequence, but it should be checked.

## Objective

To deliver input acquisition under Windows 95, wired onto the existing mapping logic.

## Scope

**In:** keyboard and gamepad acquisition, calibration, remapping, mapping to the N64
controller.

**Out:** the gyroscope, which makes no sense on this target and disappears with
E07-S02.

## Work

1. Isolate in `runtime_input.cpp` what belongs to SDL2 and what belongs to the mapping
   logic. The latter is kept as it is: it is tested and it has no reason to change.
2. Implement keyboard acquisition. In accelerated full screen, decide between window
   messages and direct DirectInput acquisition: the second avoids auto-repeat and the
   message queue's latency, which counts in a racing game.
3. Implement gamepad acquisition through DirectInput, with a fallback on
   `joyGetPosEx`.
4. Deal with calibration and the dead zone. The analogue gamepads of the period drift;
   a configurable dead zone is necessary, not optional.
5. Deal with mapping the axes to the N64's analogue stick, respecting the range and
   response shape the game expects. A badly calibrated range makes the driving
   imprecise without any error being visible.
6. Deal with multiplayer: up to four gamepads, the N64 accepting four. Check what the
   hardware of the period really allows — two game ports are more common than four.
7. Keep the remapping, with its persistence in the configuration file (E06-S05) rather
   than in the removed ImGui interface.
8. Measure the input latency and compare it against the modern target's. In a racing
   game, latency is a characteristic of playability, not a detail.

## Acceptance criteria

- [ ] The keyboard works in full screen, with no message-queue latency.
- [ ] A DirectInput gamepad works, with the `winmm` fallback verified.
- [ ] The dead zone and the calibration are configurable.
- [ ] The mapping to the N64 stick respects range and response shape, verified by
      comparison against the modern target.
- [ ] The number of gamepads really supported is determined and documented.
- [ ] The remapping is kept and persistent.
- [ ] The input latency is measured and compared against the reference.
- [ ] The existing mapping logic is reused, not rewritten.

## Risks

An imprecise axis mapping does not show: it is felt, as driving that "does not respond
the same". It is a defect hard to diagnose after the fact, hence step 8's objective
comparison rather than an appreciation while playing.

## References

- `runtime-recomp/src/game/runtime_input.{hpp,cpp}` — 25 KB, logic to be kept
- `runtime-recomp/src/game/motion_steering_policy.hpp` — gyroscope, out of scope
- E06-S05 — configuration persistence
