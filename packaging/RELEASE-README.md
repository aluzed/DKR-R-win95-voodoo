# DKR Port 1.0.0

DKR Port is a native Windows and Linux recompilation of Diddy Kong Racing.

## Start playing

1. Launch `DKRPort.exe` on Windows, or the DKR Port AppImage on Linux.
2. Select your own legally obtained Diddy Kong Racing US v1.0 ROM (`.z64`, `.v64`, or `.n64`).
3. When the Game Pak is validated, select **Begin the Adventure**.

The ROM path is remembered locally. No ROM or extracted copyrighted game data is included in this release.

## Runtime settings

Press **F1** or **Escape** on the keyboard, or **Back / View** on a controller, to open Taj's Tent while the game is running. The overlay provides visual, audio, and control-reference pages. DKR's validated original 30 FPS presentation and gameplay cadence remain fixed in this release.

The launcher and Game Pak browser can be operated entirely with a controller. Use the D-pad or left stick to move, **A / Cross** to select, **B / Circle** to go back, **LB / RB** to change launcher tabs, and **Start / Options** to begin when a valid Game Pak is ready.

Default keyboard controls:

- Analogue stick: W A S D
- A / B / Z: Space / Shift / Z
- Start: Enter
- D-pad: Arrow keys
- C buttons: I J K L
- L / R: Q / E
- Taj's Tent overlay: F1 or Escape

## Data location

Windows stores settings and save data under `%APPDATA%\DKRPort`. Linux follows `$XDG_CONFIG_HOME/dkr-port`, or `~/.config/dkr-port` when `XDG_CONFIG_HOME` is not set.

For a portable Windows installation, create an empty `portable.txt` beside `DKRPort.exe` before first launch. Settings and saves will then use the adjacent `dkr-runtime-data` directory.

See `LICENSE.md`, `COPYING-NOTICE.md`, and `THIRD_PARTY.md` for licensing details.
