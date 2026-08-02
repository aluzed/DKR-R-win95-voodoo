# DKR Port

**Release candidate 1.0.0 — playable Windows and Linux recompilation**

DKR Port is a faithful native recompilation of _Diddy Kong Racing_. The game boots, renders through RT64, plays original audio, accepts keyboard and controller input, saves progress, and has completed Adventure-mode race testing at the original 30 FPS gameplay cadence.

The repository and release packages contain no ROM, extracted texture, model, audio, font, or other original game asset. You must supply your own legally obtained copy of Diddy Kong Racing US v1.0.

## Play

1. Launch `DKRPort.exe` on Windows or `DKRPort-1.0.0-Linux-x86_64.AppImage` on Linux.
2. Select a supported `.z64`, `.v64`, or `.n64` ROM.
3. After local validation succeeds, select **Start Diddy Kong Racing**.

The redesigned startup screen and RT64 game use one SDL window. Press **F1** or controller **Back / View** while the game is running to open the matching settings overlay. The overlay provides graphics, audio, controls, resume, and safe-quit surfaces without launching a second application.

The selected ROM path is remembered locally. ROM contents are not copied into either release package and are never uploaded.

## Supported game

```text
Diddy Kong Racing US 1.0 / v77
SHA-1: 0cb115d8716dbbc2922fda38e533b9fe63bb9670
```

## Default controls

```text
Analogue: W A S D       A button: Space       B button: Shift
Z trigger: Z            Start: Enter          L/R: Q / E
C buttons: I J K L      D-pad: Arrow keys     Overlay: F1
```

SDL game controllers are detected automatically. The left stick drives the N64 analogue stick, triggers/shoulders map to Z/L/R, the D-pad is direct, and the right stick supplies the C directions.

## Presentation policy

The default presentation is accuracy-first:

- original 4:3 composition and HUD;
- automatic integer resolution scaling;
- original 30 FPS game cadence;
- automatic D3D12/Vulkan backend selection;
- optional MSAA and high-precision framebuffer settings;
- no simulation-speed or cadence controls in the release UI.

The frame cadence remains deliberately fixed because the complete intro, races, Adventure mode, audio synchronization, wheels, propellers, and vehicle billboards have been validated against it.

## Save and configuration locations

- Windows: `%APPDATA%\DKRPort`
- Linux: `$XDG_CONFIG_HOME/dkr-port`, or `~/.config/dkr-port`

For a portable Windows installation, create an empty `portable.txt` beside `DKRPort.exe` before first launch. Data will then use the adjacent `dkr-runtime-data` directory.

## Build the runtime

The runtime uses the pinned DKR decomp, N64Recomp, N64ModernRuntime, RT64, and the project Patch Pipeline.

```text
Build-DKR-Runtime.cmd
Diagnose-DKR-Recompile.cmd
```

The project rules are strict: dependency submodules and generated recompilation output are never edited manually. DKR instruction hooks, hardware virtualization, and scheduler guards originate in `runtime-recomp/dkr.us.v77.recomp-policy.json`; dependency adjustments originate in `patches/manifest.json` and are applied reproducibly.

Linux AppImage packaging is reproducible after the Linux Release build:

```text
scripts/Package-Windows.ps1
scripts/Package-Linux-AppImage.sh
```

## Release artifacts

- `dist/DKRPort-1.0.0-rc2-Windows-x64.zip`
- `dist/DKRPort-1.0.0-rc2-Linux-x86_64.AppImage`

Both are checked to contain no N64 ROM files. See [BUILD-VALIDATION.md](BUILD-VALIDATION.md) for the current validation record.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Building](docs/BUILDING.md)
- [F3DDKR notes](docs/F3DDKR.md)
- [ROM setup and privacy](docs/ROM_SETUP.md)
- [Asset policy](docs/ASSET_POLICY.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Runtime work area](runtime-recomp/README.md)

## Legal

This is an unofficial porting project. It is not affiliated with, authorised by, or endorsed by Nintendo or Rare. Users must supply their own legally obtained game. Read [LICENSE.md](LICENSE.md), [THIRD_PARTY.md](THIRD_PARTY.md), and [runtime-recomp/COPYING-NOTICE.md](runtime-recomp/COPYING-NOTICE.md) before redistributing a build.
