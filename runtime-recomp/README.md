# DKR-R native recompilation runtime

This directory contains the project-owned integration layer for the playable
Diddy Kong Racing static recompilation.

The runtime combines N64Recomp-generated DKR CPU functions, recompiled Rare
audio/F3DDKR RSP microcode, N64ModernRuntime, the DKR-specific RT64 bridge and a
shared SDL2 startup/in-game UI. It boots, renders, plays audio, accepts input,
saves and supports both Accurate and Modern presentation profiles.

## Reproducible patch boundary

Never edit `RecompiledFuncs`, `RecompiledPatches`, RT64, N64Recomp or
N64ModernRuntime manually.

- DKR hooks are declared in `dkr.us.v77.recomp-policy.json`.
- dependency changes are declared by the repository Patch Pipeline.
- `Diagnose-DKR-Recompile.cmd` regenerates the CPU output from the prepared ELF.

## Build output

From the repository root run `Build-DKR-Runtime.cmd`, or run
`Diagnose-DKR-Recompile.cmd` after a policy-only change.

Windows output:

```text
build/dkr-runtime-rt64/bin/Release/DKR-R.exe
```

Linux output:

```text
build/dkr-runtime-linux/bin/Release/DKR-R
```

Every Linux release must also be packaged as an AppImage with `Build-Linux.sh`
or `scripts/Package-Linux-AppImage.sh`.

## ROM policy

Only Diddy Kong Racing US 1.0/v77 is supported. ROMs, extracted assets, saves,
logs and local configuration must never enter source or release archives.
