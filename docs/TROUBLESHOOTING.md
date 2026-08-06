# Troubleshooting DKR-R

## The ROM is rejected

Only Diddy Kong Racing US 1.0/v77 is supported. The normalised SHA-1 must be:

```text
0cb115d8716dbbc2922fda38e533b9fe63bb9670
```

The `.z64`, `.v64` or `.n64` extension is only a picker filter; byte order is
detected from the N64 header and normalised in memory. The original file is not
modified or copied.

## Existing saves or settings appear missing

DKR-R intentionally retains the earlier compatibility paths:

- Windows: `%APPDATA%\DKRPort`
- Linux: `$XDG_CONFIG_HOME/dkr-port` or `~/.config/dkr-port`

Do not move these directories merely because the executable was renamed.

## Windows build selects the wrong CMake

If `cmake` cannot create the Visual Studio generator, an MSYS/devkitPro CMake
may be ahead of native CMake in `PATH`. Use the Visual Studio-bundled executable
or a native Kitware installation, then reconfigure the existing build tree.

Typical Visual Studio path:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

## A recomp policy change has no effect

Do not edit generated functions. Run:

```text
Diagnose-DKR-Recompile.cmd
```

That reapplies the project Patch Pipeline and regenerates the N64Recomp output.
The newest stdout/stderr logs are written under `build-logs` if regeneration
fails.

## Linux or SteamOS closes when gameplay starts

The startup UI can appear before RT64 creates its Vulkan device. Confirm a
working Vulkan driver with `vulkaninfo`, launch the AppImage from a terminal and
inspect its output. On Steam Deck, use the native SteamOS session rather than a
Remote Play/desktop environment that exposes only software Vulkan.

An AppImage build succeeding under Ubuntu/WSL does not certify physical Steam
Deck runtime behaviour; record that separately in `BUILD-VALIDATION.md`.

## Modern high refresh stutters or runs too quickly

Reset the Modern presentation rate to 60 FPS and leave Match Display disabled
while diagnosing. Accurate must remain at 4:3/30 FPS. The correct high-refresh
path interpolates presentation only; simulation, audio and timers stay on the
authored 30 FPS timeline.

## The overlay will not open or cannot be navigated

Press F1 or Escape, or controller Back/View. Use D-pad/left stick to navigate,
A/Cross to select, B/Circle to go back and LB/RB to change pages. Mouse input
must remain active while the overlay is visible. If a controller was connected
after launch, close and reopen the overlay once so focus is restored.

## Gyro steering drifts

Place the controller still on a flat surface and choose **Calibrate**, then hold
it at the desired steering angle. Use **Recenter Steering** whenever the neutral
position changes. The Invert option should only be used for personal preference,
not to correct the default polarity.

## Graphics API recovery

Modern can request Direct3D 12 or Vulkan where supported. If a requested backend
cannot initialise, DKR-R records the failure and returns to Automatic on the
next launch so an invalid setting cannot permanently trap startup.

## Reset does not delete the ROM

That is intentional. DKR-R never owns the source ROM. Reset removes only local
generated metadata and settings controlled by the port.
