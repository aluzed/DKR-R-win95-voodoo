# Roadmap

## Locked baseline — Milestone 0.2.5

- Native Windows SDL3/RmlUi launcher physically built and run.
- Real DKR US 1.0 ROM validated.

## Completed host/input boundary — Milestone 0.3.0

- Persistent ROM source path and revalidation.
- N64 header and 4 MiB RDRAM host model.
- Fixed 60 Hz tick.
- Remappable keyboard/controller input and N64 packets.

## Current — Milestone 0.4.4

- Recovers automatically from an incomplete `.venv` before DKR extraction.
- Larger, better-spaced launcher typography.
- Full-width controls screen.
- User-supplied wording aligned with the launcher.
- Matching DKR ELF build automation under WSL.
- N64Recomp CPU-generation automation.
- N64ModernRuntime compilation probe.
- Optional RT64 configuration.
- Dedicated N64Recomp stdout/stderr capture and quick diagnostic rerun.
- Detailed dependency and compiler diagnostics.

## Milestone 0.5 — First native boot executable

- Resolve the first N64Recomp DKR-specific symbols/instructions.
- Register the DKR entrypoint with librecomp.
- Implement ROM/PI and save callbacks.
- Connect existing remapped controls to ultramodern.
- Recompile/register DKR audio and F3DDKR RSP microcode.
- Create and register the RT64 render context.
- Reach the earliest visible boot frame or document the precise next blocker.

## Milestone 0.6 — Title screen and menus

- Correct initial graphics and audio.
- Stable menu navigation and save creation.
- Frame/audio comparison against reference output.

## Milestone 0.7 — First complete race

- Character/track selection, vehicle physics, AI, items and race completion.
- Save/load round trip and controller hot-plug testing.

## Milestone 1.0 — Complete-game parity target

- Full Adventure progression, bosses, challenges, tracks and multiplayer.
- No known progression blockers, save corruption or reproducible normal-play crashes.
- Windows, Linux and macOS packages.
- Enhancements only after original-behaviour parity is reliable.

## Next — Milestone 0.5

See [`MILESTONE_0_5_PLAN.md`](MILESTONE_0_5_PLAN.md) for the first native boot,
RT64/audio/input integration and one-click player setup plan.
