# DKR-R architecture

## What kind of port this is

DKR-R is a static recompilation port, not a conventional source-to-source
decompilation port. The completed Diddy Kong Racing decompilation remains the
authoritative readable reference for symbols, structures, algorithms and patch
locations. The executable path is:

```text
User-owned DKR US 1.0 ROM
        +
Pinned matching DKR decomp ELF and symbols
        |
N64Recomp-generated native CPU functions
        |
Project Patch Pipeline hooks and host policies
        |
N64ModernRuntime scheduling, saves, audio, input and RSP dispatch
        |
Recompiled Rare audio/F3DDKR microcode
        |
RT64 renderer in the shared SDL2 launcher/game window
```

This approach preserves the original program and timing while allowing focused,
readable host enhancements without manually rewriting the entire game.

## Protected boundaries

The following are generated or pinned dependency work areas and must never be
edited directly:

- `runtime-recomp/RecompiledFuncs`
- `runtime-recomp/RecompiledPatches`
- `extern/rt64`
- `extern/n64-modern-runtime`
- `extern/n64-modern-runtime/N64Recomp`

DKR instruction/function hooks belong in
`runtime-recomp/dkr.us.v77.recomp-policy.json`. Dependency changes belong in
`patches/manifest.json` and its referenced patches. Regeneration is performed
by `Diagnose-DKR-Recompile.cmd`.

## Runtime ownership

The project-owned `runtime-recomp/src/game` layer owns:

- ROM validation and game registration;
- one-window SDL platform integration;
- controller, keyboard and angle-based gyro input;
- virtual EEPROM and Controller Pak storage;
- audio mix/EQ policy without altering the original audio clock;
- Accurate/Modern presentation policy;
- widescreen, interpolation, FOV, visibility and detail controls;
- Taj's Tent startup/in-game UI and save manager;
- F3DDKR command translation and DKR-specific renderer policy.

## Preset boundary

Accurate is the regression baseline: original 4:3, original 30 FPS cadence,
original FOV/detail/visibility/audio mix and original HUD placement.

Modern leaves simulation, race timing, input polling and audio on that original
timeline. It changes only presentation and explicitly selected quality-of-life
policies. High-refresh output is interpolation, not a faster game clock.

## ROM and save boundary

The selected Game Pak is validated locally and never copied into a release.
Releases are scanned for ROM extensions and N64 ROM headers. EEPROM and four
virtual Controller Pak files remain host files and can be managed through T.T.'s
Save Garage.
