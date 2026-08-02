# Milestone 0.4 — Readable Launcher and Runtime Preparation

## Launcher changes

- Default window increased to 1600 × 900 with a 1280 × 720 minimum.
- Main launcher wording aligned with the supplied `Diddy Kong Racing on PC!?` revision.
- Fonts, line heights, card padding and page spacing increased throughout.
- Blank `<br/>` spacing was replaced by RCSS margins so the layout scales correctly.
- Controls now use a full-width three-column binding table.
- Analogue options sit in a horizontal panel above the mappings.
- Diagnostics, settings, game telemetry and modal text are larger.

## Runtime direction

Milestone 0.3 proved ROM validation, input remapping and N64-format controller packets. Milestone 0.4
adds the reproducible preparation path for actual game code:

1. Build the completed DKR decomp and matching ELF under WSL.
2. Use that ELF and the user's ROM as N64Recomp metadata/input.
3. Generate native CPU functions.
4. Integrate those functions with N64ModernRuntime.
5. Recompile the F3DDKR and audio RSP microcode.
6. Register RT64 for graphics and connect SDL audio/input callbacks.

The project is not labelled playable until graphics, audio, input, saves and full game progression are
working. The runtime preparation command is designed to expose the first DKR-specific blocker rather
than hiding it behind a placeholder screen.
