# DKR-R native recompilation runtime

This directory contains the project-owned integration layer for the playable
Diddy Kong Racing static recompilation.

The runtime combines N64Recomp-generated DKR CPU functions, recompiled Rare
audio/F3DDKR RSP microcode, N64ModernRuntime, the DKR-specific RT64 bridge and a
shared SDL2 startup/in-game UI. It boots, renders, plays audio, accepts input,
saves and supports both Accurate and Modern presentation profiles.

Modern also provides independent live audio buses, two-axis angle-held gyro
control, configurable anisotropic filtering, real runtime telemetry, a guarded
Quick Restart chord and a controller-navigable Save Builder backed by a native
DKR EEPROM codec. The codec validates and regenerates all Adventure,
global-config and T.T.-record checksums while retaining unknown fields.

The Save Builder also exposes the 24 retail Magic Codes as a separate launch
configuration. Codes are applied once at the first authoritative game-loop
boundary after save initialization; they are not written into EEPROM data.
Mutually exclusive codes follow the original game's rules, and one-shot codes
are removed from their launch queue after injection.

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

macOS output is a native RT64/Metal `DKR-R.app`; `Build-macOS.sh` performs the
complete compile, test, self-test, bundle, scan and ZIP flow on an Apple host.

## ROM policy

Only Diddy Kong Racing US 1.0/v77 is supported. ROMs, extracted assets, saves,
logs and local configuration must never enter source or release archives.
