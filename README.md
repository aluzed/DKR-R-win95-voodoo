# DKR-R (Diddy Kong Racing - Recompiled)

<img width="912" height="607" alt="DKR-R 3" src="https://github.com/user-attachments/assets/bb46fcfa-c100-4aeb-9100-5a7834918780" />


**1.0.0 release candidate - native Windows and Linux static recompilation**

DKR-R recompiles the original Diddy Kong Racing executable for modern desktop
systems. It is a static recompilation port: the completed decompilation supplies
readable reference source, symbols, structures and patch locations, while
N64Recomp translates the matching original program and the project Patch
Pipeline applies host-side fixes. N64ModernRuntime and RT64 provide the modern
runtime, audio, input and graphics boundary.

No ROM, extracted textures, models, music or other copyrighted game data is
included. Players must provide their own legally obtained Diddy Kong Racing US
1.0 Game Pak image.

## Play

1. Launch `DKR-R.exe` on Windows or the DKR-R AppImage on Linux.
2. Select a supported `.z64`, `.v64` or `.n64` image with the in-window,
   controller-friendly Game Pak finder.
3. After local validation succeeds, choose **Begin the Adventure**.

The launcher and game share one window. Press **F1** or **Escape**, or
controller **Back / View**, to open Taj's Tent over the running game. The
overlay supports mouse, keyboard and controller navigation.

Supported revision:

```text
Diddy Kong Racing US 1.0 / v77
SHA-1: 0cb115d8716dbbc2922fda38e533b9fe63bb9670
```

## Presets

Accurate is the stable reference preset:

- original 4:3 presentation and 30 FPS cadence;
- original FOV, visibility, model detail and audio mix;
- original HUD placement;
- Modern-only controls hidden and neutralised.

Modern keeps simulation, timers, input and audio on the original timeline and
adds:

- fit-to-window widescreen and ultrawide presentation;
- interpolated presentation from 30 to 500 FPS or Match Display;
- FOV, view-distance and culling expansion controls;
- optional full-detail vehicle models;
- Direct3D 12 or Vulkan selection with automatic fallback;
- complete keyboard/controller remapping and controller-response tuning;
- calibrated angle-based gyro steering with recentering;
- master, music, effects and vehicle volume plus three-band EQ;
- T.T.'s Save Garage for validated backup, import and export bundles.

The HUD always retains its original authored layout. A configurable HUD
placement control is deliberately not part of this release candidate.

## Default controls

```text
Analogue: W A S D       A button: Space       B button: Shift
Z trigger: Z            Start: Enter          L/R: Q / E
C buttons: I J K L      D-pad: Arrow keys     Overlay: F1 / Escape
```

SDL game controllers are detected automatically. Use the D-pad or left stick
to move, A/Cross to select, B/Circle to return, LB/RB to change pages and
Start/Options to launch after a valid Game Pak is selected.

## Saves and compatibility paths

- Windows: `%APPDATA%\DKRPort`
- Linux: `$XDG_CONFIG_HOME/dkr-port`, or `~/.config/dkr-port`

Those legacy directory names are intentionally retained so the DKR-R rename
does not strand existing settings or Adventure saves. For a portable Windows
installation, create an empty `portable.txt` beside `DKR-R.exe`; data then uses
the adjacent `dkr-runtime-data` directory.

## Build and package

Protected dependency submodules and generated recompilation output are never
edited by hand. Game hooks live in
`runtime-recomp/dkr.us.v77.recomp-policy.json`; dependency changes live in the
project Patch Pipeline.

```text
Build-DKR-Runtime.cmd
Diagnose-DKR-Recompile.cmd
scripts/Package-Windows.ps1
Build-Linux.sh
```

Expected release artifacts:

```text
dist/DKR-R-1.0.0-rc4-Windows-x64.zip
dist/DKR-R-1.0.0-rc4-Linux-x86_64.AppImage
dist/DKR-R-1.0.0-rc4-Source.zip
```

Every release artifact is scanned for prohibited N64 ROM extensions and ROM
headers. See [BUILD-VALIDATION.md](BUILD-VALIDATION.md) for the exact current
build and test record.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Building](docs/BUILDING.md)
- [Modern feature plan](docs/MODERN_MILESTONE_PLAN.md)
- [F3DDKR integration](docs/F3DDKR.md)
- [ROM setup and privacy](docs/ROM_SETUP.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)

## Legal

This unofficial project is not affiliated with, authorised by or endorsed by
Nintendo or Rare. Read [LICENSE.md](LICENSE.md), [THIRD_PARTY.md](THIRD_PARTY.md)
and [runtime-recomp/COPYING-NOTICE.md](runtime-recomp/COPYING-NOTICE.md) before
redistributing a build.
