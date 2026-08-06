# DKR-R 1.0.0 Release Candidate 4

DKR-R is a native Windows and Linux static recompilation of Diddy Kong Racing.
It contains no ROM or extracted game data. Supply your own legally obtained
Diddy Kong Racing US 1.0 Game Pak image.

## Start racing

1. Launch `DKR-R.exe` on Windows or the DKR-R AppImage on Linux.
2. Select your `.z64`, `.v64` or `.n64` image in the Game Pak finder.
3. After validation succeeds, choose **Begin the Adventure**.

Accurate is the default 4:3, 30 FPS reference experience. Modern adds
fit-to-window widescreen, high-refresh interpolated presentation, FOV and
visibility controls, maximum vehicle detail, graphics API selection, gyro
steering, expanded sound controls and T.T.'s Save Garage. Both presets retain
the original HUD placement and original gameplay/audio timeline.

Press **F1** or **Escape**, or controller **Back / View**, to open Taj's Tent
over the live game. The launcher and overlay can be operated with mouse,
keyboard or controller.

## Saves

Settings and saves remain in the compatibility paths `%APPDATA%\DKRPort` on
Windows and `$XDG_CONFIG_HOME/dkr-port` (or `~/.config/dkr-port`) on Linux.
These legacy names intentionally preserve data created by earlier builds.

T.T.'s Save Garage can back up, import and export the Adventure EEPROM and all
four virtual Controller Pak files in a validated `.dkrsave` bundle.

For a portable Windows installation, create an empty `portable.txt` beside
`DKR-R.exe`; data then uses the adjacent `dkr-runtime-data` directory.

## Controls

- D-pad or left stick: move through the UI
- A / Cross: select
- B / Circle: back
- LB / RB: change pages
- Start / Options: launch when a valid Game Pak is ready
- F1 / Escape or Back / View: toggle Taj's Tent

Controls can be rebound from the Controls page. Restore Defaults always remains
available as a recovery path.

See `LICENSE.md`, `COPYING-NOTICE.md` and `THIRD_PARTY.md` before distributing
this package.
