# DKR Port

**1.0.0 RC3 - Windows and Linux native recompilation**

DKR Port recompiles Diddy Kong Racing for modern desktop systems while keeping
the original simulation, race timers, input cadence and audio timeline intact.
The accepted Accurate preset runs the original 4:3, 30 FPS presentation;
Modern adds widescreen and interpolated high-refresh presentation without
speeding up the game.

The repository and release packages contain no ROM or extracted texture,
model, audio or other game data. Supply your own legally obtained Diddy Kong
Racing US v1.0 Game Pak image.

## Play

1. Launch `DKRPort.exe` or `DKRPort-1.0.0-rc3-Linux-x86_64.AppImage`.
2. Choose a supported `.z64`, `.v64`, or `.n64` image with the in-window,
   controller-friendly Game Pak finder.
3. After local validation succeeds, select **Begin the Adventure**.

The launcher and RT64 game share one SDL window. Press **F1** or **Escape**, or
controller **Back / View**, to open the transparent Taj's Tent overlay over the
running game. It provides graphics, sound, controls, save information, resume
and confirmed desktop-exit actions.

The selected ROM path stays local, is never copied into a release package and
is never uploaded.

## Supported game

```text
Diddy Kong Racing US 1.0 / v77
SHA-1: 0cb115d8716dbbc2922fda38e533b9fe63bb9670
```

## Presets

Accurate is the release-safe default:

- original 4:3 composition and 30 FPS presentation;
- validated gameplay, audio and menu timing;
- original FOV, visibility, model-detail and audio-mix policy;
- Modern-only controls hidden and neutralised.

Modern preserves that game timeline and adds:

- fit-to-window widescreen, including ultrawide visibility expansion;
- interpolated presentation from 30 through 500 FPS or Match Display;
- gameplay FOV and view-distance controls;
- maximum vehicle detail plus HUD scale and safe-area controls;
- selectable graphics API with automatic failure recovery;
- full keyboard/controller remapping and controller-response tuning;
- optional calibrated gyro steering;
- master, music, effects and vehicle volume plus three-band EQ;
- T.T.'s Save Garage for checked backups and cross-platform `.dkrsave`
  import/export.

## Default controls

```text
Analogue: W A S D       A button: Space       B button: Shift
Z trigger: Z            Start: Enter          L/R: Q / E
C buttons: I J K L      D-pad: Arrow keys     Overlay: F1 / Escape
```

SDL game controllers are detected automatically. Use the D-pad or left stick
to navigate the launcher, A/Cross to select, B/Circle to return, LB/RB to change
pages and Start/Options to begin once a Game Pak is ready.

## Saves and configuration

- Windows: `%APPDATA%\DKRPort`
- Linux: `$XDG_CONFIG_HOME/dkr-port`, or `~/.config/dkr-port`

For a portable Windows installation, create an empty `portable.txt` beside
`DKRPort.exe`. Data then uses the adjacent `dkr-runtime-data` directory.

## Build and package

The runtime uses pinned DKR decomp, N64Recomp, N64ModernRuntime and RT64 sources
with the project Patch Pipeline. Dependency submodules and generated recomp
outputs are never edited by hand.

```text
Build-DKR-Runtime.cmd
Diagnose-DKR-Recompile.cmd
scripts/Package-Windows.ps1
scripts/Package-Linux-AppImage.sh
```

Release artifacts:

- `dist/DKRPort-1.0.0-rc3-Windows-x64.zip`
- `dist/DKRPort-1.0.0-rc3-Linux-x86_64.AppImage`

Every release artifact is scanned for prohibited N64 ROM extensions and ROM
headers. See [BUILD-VALIDATION.md](BUILD-VALIDATION.md) for the exact build,
test and hash record.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Building](docs/BUILDING.md)
- [Modern milestone plan](docs/MODERN_MILESTONE_PLAN.md)
- [F3DDKR notes](docs/F3DDKR.md)
- [ROM setup and privacy](docs/ROM_SETUP.md)
- [Asset policy](docs/ASSET_POLICY.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)

## Legal

This unofficial project is not affiliated with, authorised by or endorsed by
Nintendo or Rare. Users must supply their own legally obtained game. The
original project source is MIT licensed; distribution of the unified executable
must also comply with N64ModernRuntime's GPL-3.0 terms. Read [LICENSE.md](LICENSE.md),
[THIRD_PARTY.md](THIRD_PARTY.md) and
[runtime-recomp/COPYING-NOTICE.md](runtime-recomp/COPYING-NOTICE.md) before
redistributing a build.
