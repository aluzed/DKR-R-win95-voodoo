# DKR Port 1.0.0 RC3

DKR Port is a native Windows and Linux recompilation of Diddy Kong Racing. It
ships without a ROM or extracted game data; you must supply your own legally
obtained Diddy Kong Racing US v1.0 Game Pak image.

## Start racing

1. Launch `DKRPort.exe` on Windows or the DKR Port AppImage on Linux.
2. Use the controller-friendly Game Pak finder to choose your `.z64`, `.v64`,
   or `.n64` image.
3. After validation succeeds, select **Begin the Adventure**.

The selected path stays on your computer and is remembered locally.

## Accurate and Modern

**Accurate** is the safe default. It locks the original 4:3 composition and
30 FPS presentation while preserving the accepted game and audio timing.

**Modern** keeps simulation, race timers, input and audio on that same original
timeline while enabling fit-to-window widescreen, high-refresh interpolated
presentation, an FOV and view-distance tune-up, maximum vehicle detail, HUD
scale and safe-area controls, graphics API selection, gyro steering, expanded
audio controls and T.T.'s Save Garage.

If a graphics backend cannot start, DKR Port automatically returns to the
Automatic backend instead of trapping the next launch in a failure loop.

## Taj's Tent overlay

Press **F1** or **Escape**, or **Back / View** on a controller, while racing.
The transparent overlay leaves the game visible underneath and provides
graphics, sound, controls, save information, resume and confirmed exit actions.

The launcher and overlay are controller-first:

- D-pad or left stick: move
- A / Cross: select
- B / Circle: back
- LB / RB: change pages
- Start / Options: begin when a valid Game Pak is ready

Keyboard defaults:

- Analogue stick: W A S D
- A / B / Z: Space / Shift / Z
- Start: Enter
- D-pad: Arrow keys
- C buttons: I J K L
- L / R: Q / E
- Taj's Tent: F1 or Escape

Controls can be rebound from the Controls page. Conflicting assignments are
resolved by unbinding the old action, and Restore Defaults always remains
available as a recovery path.

## Saves

Windows stores settings and save data under `%APPDATA%\DKRPort`. Linux follows
`$XDG_CONFIG_HOME/dkr-port`, or `~/.config/dkr-port` when `XDG_CONFIG_HOME` is
not set.

T.T.'s Save Garage can back up, import and export the Adventure EEPROM plus all
four virtual Controller Pak files as a validated `.dkrsave` bundle. Imports are
verified completely and backed up before an atomic replacement.

For a portable Windows installation, create an empty `portable.txt` beside
`DKRPort.exe` before first launch. Data will then use the adjacent
`dkr-runtime-data` directory.

See `LICENSE.md`, `COPYING-NOTICE.md`, and `THIRD_PARTY.md` before
redistributing a build.
