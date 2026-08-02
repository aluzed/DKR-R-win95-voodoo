# DKR native runtime

This directory contains the project-owned integration layer for the playable Diddy Kong Racing static recompilation.

## Current boundary

`DKRPort` combines:

- N64Recomp-generated DKR CPU functions;
- recompiled Rare audio and F3DDKR RSP microcode;
- N64ModernRuntime scheduling, events, input, save, and audio interfaces;
- the project F3DDKR-to-RT64 bridge;
- SDL2 window, audio, controller, and keyboard hosting;
- one startup/settings UI shared with the in-game RT64 overlay.

The game boots, renders, plays audio, accepts input, saves, and has completed Adventure-mode race testing.

## Reproducible patch boundaries

Never edit `RecompiledFuncs`, `RecompiledPatches`, RT64, N64Recomp, or N64ModernRuntime manually.

- DKR function boundaries, instruction patches, and function hooks are declared in `dkr.us.v77.recomp-policy.json`.
- Dependency changes are declared under `patches/` and applied by `scripts/Apply-Dependency-Patches.ps1`.
- `Diagnose-DKR-Recompile.cmd` regenerates the CPU output from the prepared ELF and policy.

The policy includes virtualized cartridge/MMIO checks, texture command-space corrections, supported CIC values, the original vehicle-attachment path, audio pointer guards, and late host RSP/RDP completion guards.

## Build

From the repository root:

```text
Build-DKR-Runtime.cmd
```

For an already prepared toolchain and ELF:

```text
Diagnose-DKR-Recompile.cmd
```

The Windows Release target emits `build/dkr-runtime-rt64/bin/Release/DKRPort.exe`. The Linux Release target emits `build/dkr-runtime-linux/bin/Release/DKRPort` and can be packaged with `scripts/Package-Linux-AppImage.sh`.

## ROM policy

The runtime accepts only the supported Diddy Kong Racing US v1.0/v77 revision. ROMs, built matching ROMs, extracted assets, saves, logs, and user configuration must not be added to source or release archives.
