# Building DKR-R 1.0.0

## Requirements

Windows preparation requires Visual Studio 2022 with Desktop development with
C++, Git, Python 3, PowerShell and WSL2/Ubuntu. Linux requires CMake 3.24+, Ninja,
Clang or GCC, SDL2 development files, Vulkan development files and AppImage
packaging dependencies. You must supply a supported US 1.0 ROM locally.

## Prepare generated game code and dependencies

From a Windows terminal at the repository root:

```text
Build-DKR-Runtime.cmd
```

This validates the ROM, builds the matching decomp ELF, checks out the exact
dependency commits, applies `patches/manifest.json`, generates CPU/RSP sources
and runs the runtime probe. Generated and dependency worktrees are ignored and
must not be edited.

After a recomp policy change use:

```text
Diagnose-DKR-Recompile.cmd
```

## Windows release

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-DKR-R-Windows.ps1 -Clean -Package
```

This configures a native Visual Studio x64 Release build, compiles the runtime,
runs the complete DKR-R CTest suite, executes the Controller Pak self-test,
scans the staged package and creates:

```text
dist/DKR-R-1.0.0-Windows-x64.zip
```

## Linux and AppImage

On Ubuntu or another supported build host:

```bash
./Setup-Linux.sh
./Build-Linux.sh
```

The build uses Vulkan through RT64, runs the complete DKR-R CTest suite and the
packaged Controller Pak self-test, then creates:

```text
dist/DKR-R-1.0.0-Linux-x86_64.AppImage
```

An unpackaged Linux binary is not a complete release deliverable.

## macOS

On an Apple host with Xcode command-line tools, CMake and Ninja:

```bash
./Setup-macOS.sh
./Build-macOS.sh
```

RT64 uses Metal on macOS. See `packaging/MACOS-BUILD-README.md` for the handoff
and validation checklist.

## Release safety

Run `python scripts/scan_for_game_assets.py` before packaging. No ROM, save,
Controller Pak, extracted asset, log, build cache or local configuration may be
included. Release archives are scanned again by their packaging scripts.
