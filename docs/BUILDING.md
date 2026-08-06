# Building DKR-R 1.0.0 RC4

DKR-R combines project-owned integration code with pinned Diddy Kong Racing
decomp, N64Recomp, N64ModernRuntime and RT64 revisions. A legally obtained
Diddy Kong Racing US 1.0/v77 Game Pak image is required locally to build the
matching decomp ELF and generated translation. No ROM is provided.

## Protected source boundaries

Never hand-edit `runtime-recomp/RecompiledFuncs`,
`runtime-recomp/RecompiledPatches`, `extern/rt64`,
`extern/n64-modern-runtime` or its `N64Recomp` submodule. Put DKR hooks in
`runtime-recomp/dkr.us.v77.recomp-policy.json`, dependency patches under
`patches/`, then regenerate through the Patch Pipeline.

## Windows prerequisites

- Windows 10/11 x64
- Visual Studio 2022 Desktop development with C++ and a Windows SDK
- Git and PowerShell 5.1+
- native Windows CMake (the Visual Studio-bundled CMake is supported)
- WSL2/Ubuntu 24.04 for the matching decomp build
- a local DKR US 1.0/v77 ROM

Prepare dependencies, the matching ELF and generated functions:

```text
Build-DKR-Runtime.cmd -BuildRenderer
```

After changing the recomp policy, regenerate before compiling:

```text
Diagnose-DKR-Recompile.cmd
```

Configure and build the shipping runtime:

```text
cmake -S runtime-recomp -B build/dkr-runtime-rt64 ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DDKRPORT_ROOT=C:/DKRPort ^
  -DDKR_RUNTIME_BUILD_GENERATED=ON ^
  -DDKR_RUNTIME_BUILD_RT64=ON
cmake --build build/dkr-runtime-rt64 --config Release --parallel
ctest --test-dir build/dkr-runtime-rt64 -C Release --output-on-failure
```

Output: `build/dkr-runtime-rt64/bin/Release/DKR-R.exe`.

Run the storage self-test with a disposable directory:

```text
build\dkr-runtime-rt64\bin\Release\DKR-R.exe --self-test-pak build\pak-self-test
```

## Linux and AppImage

Ubuntu 24.04 x86-64 is the qualified Linux build environment. Install CMake,
Ninja, GCC/G++, SDL2, GTK3 and Vulkan development/runtime packages after the
generated translation has been prepared.

The complete build, test and mandatory AppImage step is:

```bash
DKR_RELEASE_VERSION=1.0.0-rc4 ./Build-Linux.sh
```

This emits:

```text
build/dkr-runtime-linux/bin/Release/DKR-R
dist/DKR-R-1.0.0-rc4-Linux-x86_64.AppImage
```

Linux/SteamOS uses RT64's Vulkan backend. Vulkan availability is required to
run the game, but the AppImage still needs testing on physical Steam Deck
hardware before that platform is certified.

## Packages

```powershell
scripts\Package-Windows.ps1 -Version 1.0.0-rc4
```

```bash
DKR_RELEASE_VERSION=1.0.0-rc4 ./scripts/Package-Linux-AppImage.sh
```

After the final release commit:

```text
python scripts/package_source.py
```

Expected artifacts:

```text
dist/DKR-R-1.0.0-rc4-Windows-x64.zip
dist/DKR-R-1.0.0-rc4-Linux-x86_64.AppImage
dist/DKR-R-1.0.0-rc4-Source.zip
```

Packagers refuse to overwrite existing outputs and scan staging trees for ROM
extensions and N64 ROM headers.

## Release checks

Keep Accurate fixed at 4:3/30 FPS. Visually validate Accurate 4:3 and Modern at
16:9, 21:9 and 32:9. Exercise launcher and in-game overlay navigation with a
mouse and controller, gyro recentering, a post-race results screen, intro to
character-select audio, save import/export and clean Exit to Desktop. Record
automated results, dependency revisions, hashes and physical-hardware tests in
`BUILD-VALIDATION.md`.
