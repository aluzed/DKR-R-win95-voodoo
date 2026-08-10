# Troubleshooting DKR-R 1.0.0

## ROM rejected

Only Diddy Kong Racing US 1.0/v77 is supported. Normalized SHA-1:
`0cb115d8716dbbc2922fda38e533b9fe63bb9670`.

## Saves or settings appear missing

Compatibility paths are retained:

- Windows: `%APPDATA%\DKRPort`
- Linux: `$XDG_CONFIG_HOME/dkr-port` or `~/.config/dkr-port`

Portable Windows stores data in `dkr-runtime-data` beside the executable.

## Linux/SteamOS closes as gameplay starts

Run the AppImage from a terminal and verify Vulkan with `vulkaninfo`. Steam Deck
must expose its native Vulkan driver rather than a software or remote desktop
device. The launcher may appear before RT64 creates the Vulkan device, so a
driver failure can occur only when gameplay begins.

## Modern presentation is uneven

Start at 60 FPS with Match Display disabled. Verify Accurate remains steady at
4:3/30 FPS. High-refresh output interpolates presentation; it must not speed up
simulation or audio. Disable custom texture packs and CRT filters while
isolating third-party content.

## Overlay input

Open the overlay with Escape, F1 or controller Back/View. Navigate with D-pad or
left stick, select with A/Cross, cancel with B/Circle, and change pages with
LB/RB. Mouse input remains active while the overlay is open.

## Gyro drift

Place the controller still, calibrate, then recenter at the desired neutral
angle. Steam Input can intercept motion sensors on SteamOS; disable conflicting
Steam Input gyro mappings when using DKR-R's native gyro.

## Graphics API recovery

If a selected backend cannot initialize, remove only the local graphics setting
or choose Automatic on the next launch. Do not delete your ROM or save files.

## Recomp policy change has no effect

Run `Diagnose-DKR-Recompile.cmd`. Never edit generated functions or dependency
worktrees directly.
