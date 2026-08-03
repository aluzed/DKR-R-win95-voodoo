# Building DKR Port 1.0.0 RC3

DKR Port is built from project-owned integration source, a pinned Diddy Kong
Racing decomp checkout, generated N64Recomp CPU/RSP output, N64ModernRuntime and
RT64. The repository and build process do not provide a ROM. A legally obtained
Diddy Kong Racing US v1.0/v77 Game Pak image is required locally to produce the
matching ELF and generated translation.

## Protected source boundaries

Do not edit these generated or third-party trees directly:

- `RecompiledFuncs`
- `RecompiledPatches`
- `extern/n64-modern-runtime`
- `extern/n64-modern-runtime/N64Recomp`
- `extern/rt64`

Game instruction/function changes belong in
`runtime-recomp/dkr.us.v77.recomp-policy.json`. Dependency changes belong in
`patches/manifest.json` and its referenced patch files. Apply them with the
Patch Pipeline. Regenerate translated functions; never hand-edit generated
output.

## Prerequisites

Windows builds require:

- Windows 10 or 11 x64;
- Visual Studio 2022 with Desktop development with C++ and a Windows SDK;
- Git, CMake and PowerShell 5.1 or newer;
- WSL2 with Ubuntu 24.04 for the matching decomp build;
- a locally supplied DKR US v1.0/v77 ROM.

Linux builds require a modern x86-64 distribution, CMake, Ninja, GCC/G++, SDL2,
GTK3 development files and Vulkan development/runtime support. Ubuntu 24.04 is
the qualified build environment; SteamOS/Steam Deck is the target AppImage
environment.

## Prepare dependencies and generated code

From the repository root on Windows:

```text
Build-DKR-Runtime.cmd -BuildRenderer
```

The preparation script validates the supported ROM revision, builds the
matching decomp ELF under WSL, resolves the exact dependency revisions, applies
the Patch Pipeline, builds the pinned N64Recomp tools and generates the DKR CPU
translation. The resolved revisions are recorded in
`runtime-recomp/resolved-runtime-dependencies.json`; release pins also live in
`dependencies.lock.json` and `patches/manifest.json`.

After a policy change, regenerate from the prepared ELF with:

```text
Diagnose-DKR-Recompile.cmd
```

## Windows runtime

Configure and build the full renderer-enabled runtime from a Visual Studio
developer shell:

```text
cmake -S runtime-recomp -B build/dkr-runtime-rt64 ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DDKRPORT_ROOT=C:/DKRPort ^
  -DDKR_RUNTIME_BUILD_GENERATED=ON ^
  -DDKR_RUNTIME_BUILD_RT64=ON
cmake --build build/dkr-runtime-rt64 --config Release --parallel
```

The executable and required runtime DLLs are emitted under:

```text
build/dkr-runtime-rt64/bin/Release
```

Run the test executables generated under
`build/dkr-runtime-rt64/Release`. The release record lists the exact suite and
results. The Pak storage test can also be run directly:

```text
build\dkr-runtime-rt64\bin\Release\DKRPort.exe --self-test-pak build\pak-self-test
```

## Linux runtime

After the dependencies and generated translation have been prepared, build in
WSL2/Ubuntu 24.04 or a native Ubuntu 24.04 environment:

```bash
cmake -S runtime-recomp -B build/dkr-runtime-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKRPORT_ROOT="$PWD" \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_RUNTIME_BUILD_RT64=ON
cmake --build build/dkr-runtime-linux --parallel
ctest --test-dir build/dkr-runtime-linux --output-on-failure
```

The renderer-enabled executable is:

```text
build/dkr-runtime-linux/bin/Release/DKRPort
```

Linux and SteamOS use RT64's Vulkan backend. The launcher uses SDL2's software
2D renderer before handing the same window to the Vulkan-capable game runtime.

## Release packages

Build a fresh Windows ZIP from the Release tree:

```powershell
scripts\Package-Windows.ps1 -Version 1.0.0-rc3
```

Build the Linux AppImage from Ubuntu/WSL:

```bash
DKR_RELEASE_VERSION=1.0.0-rc3 ./scripts/Package-Linux-AppImage.sh
```

Build the committed project-source archive only after the final release commit:

```text
python scripts/package_source.py
```

The packagers refuse to overwrite an existing output. Use a fresh version or
remove an obsolete local preflight artifact deliberately. Every staging tree
and archive is scanned for prohibited game-data extensions and N64 ROM headers.
The AppImage additionally carries the dependency package copyright records and
common licence texts deployed from the qualified Ubuntu environment.

Expected public artifacts:

```text
dist/DKRPort-1.0.0-rc3-Windows-x64.zip
dist/DKRPort-1.0.0-rc3-Linux-x86_64.AppImage
dist/DKRPort-1.0.0-rc3-Source.zip
```

## Release discipline

Keep the accepted Accurate preset at original 4:3/30 FPS. Treat Modern frame
pacing as a locked, separately tested presentation layer. Every renderer or
microcode change must be tested in a visible build at Accurate 4:3 and Modern
16:9, 21:9 and 32:9 before packaging. Record the final commit, dependency
commits, tests, hashes and physical-controller/Steam Deck results in
`BUILD-VALIDATION.md`.
