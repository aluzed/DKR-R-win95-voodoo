# Troubleshooting

## Build-Windows.cmd closes or reports failure

Run it from an existing Command Prompt so the complete output remains visible:

```bat
cd C:\path\to\DKRPort
Build-Windows.cmd -Clean
```

The final lines print a file under `build-logs`. That transcript is the most useful item to provide
when diagnosing the failure.

## winget is missing

Install or update **App Installer** from the Microsoft Store. Alternatively install Git, CMake and
Visual Studio 2022 Build Tools manually, then run:

```bat
Build-Windows.cmd -NoInstall
```

## Visual Studio exists but the C++ compiler is missing

Open Visual Studio Installer, choose **Modify**, and install **Desktop development with C++**, MSVC
v143 x64/x86 tools and a Windows 10/11 SDK.


## CMake says "Could not create named generator Visual Studio 17 2022"

This means a Unix/MSYS build of CMake was selected instead of native Windows CMake. Milestone 0.2.2
checks generator capabilities and will ignore that executable automatically. It should then use or
install the official Kitware CMake at:

```text
C:\Program Files\CMake\bin\cmake.exe
```

Run the corrected package with:

```bat
Build-Windows.cmd -Clean
```

The log should show both `CMake executable:` and `CMake generator:` before configuration starts.

## CMake configuration fails while installing libraries

Check internet access to GitHub, then delete `.deps\vcpkg` and `build\windows-x64` or run:

```bat
Build-Windows.cmd -Clean
```

The first build is considerably larger because SDL3, RmlUi, FreeType and their dependencies are
compiled. Later builds reuse the cache.

## The native window does not open

Run `dist\DKRPort-Windows-x64\DKRPort.exe` from Command Prompt and inspect the newest file under its
`runtime\logs` directory. Update the graphics driver and confirm Remote Desktop or virtual-machine
graphics acceleration is available.

## Text is missing

The launcher loads a suitable system font at runtime rather than distributing a font. On Windows it
tries Segoe UI and Arial. Repairing standard Windows fonts should correct this without rebuilding.

## My ROM is rejected

Only the canonical US 1.0 / v77 revision is supported in this milestone. A valid N64 header is not
enough; the normalised SHA-1 must match. The error panel reports the calculated hash without saving
the ROM.

## Reset does not delete my ROM

That is intentional. DKR Port never owns or copies the source ROM. Reset removes only the generated
manifest and local manifest O2R from the port data directory.


## `Cannot convert value "3.31.6-msvc6" to System.Version`

This was a build-script bug fixed in Milestone 0.2.2. Visual Studio's bundled CMake may add an `-msvcN` vendor suffix. Use the 0.2.2 build script or replace `scripts\Build-Windows.ps1` with the corrected copy.

## MSVC C2220 from `getenv` or an F3DDKR constant condition

Milestone 0.2.5 fixes two warnings that Visual Studio correctly promoted to errors under `/WX`:

- `Paths.cpp`: C4996 for `getenv`; Windows now uses `_dupenv_s`.
- `F3DDKRRegistry.cpp`: C4127 for an always-constant condition; the invariant is now checked with `static_assert`.

Use the 0.2.5 source package and rerun `Build-Windows.cmd` without `-Clean`; the configured vcpkg dependencies can be reused.


## `Cannot open include file: SDL3/SDL_main.h`

The header is supplied by SDL3, but Milestone 0.2.4 hid SDL3 behind a private static-library dependency. As a result, the executable linked SDL transitively but did not inherit SDL's include directory while compiling `main.cpp`.

Milestone 0.2.5 makes SDL3 and RmlUi public usage requirements of the native UI target and links `DKRPort` directly to `SDL3::SDL3`. Rerun `Build-Windows.cmd` without `-Clean`; CMake will regenerate the Visual Studio project and reuse the existing vcpkg packages.

## `wslpath` prints a path without backslashes

Use the 0.4.1 or newer runtime-preparation script. Older scripts invoked the
command through an intermediate WSL shell, which could transform
`C:\DKRPort\extern\dkr-decomp` into `C:DKRPortexterndkr-decomp`.

The corrected script uses `wsl.exe --exec wslpath` and passes the DKR source
directory to Bash as a positional argument. Rerun `Build-DKR-Runtime.cmd`;
there is no need to clean the native launcher build or delete the prepared
decomp checkout.


## `No module named splat` during DKR runtime preparation

Milestone 0.4.4 validates the contents of the DKR Python virtual environment,
not merely the presence of `.venv/bin/python3`. If an earlier interrupted
setup left an incomplete virtual environment, the runtime script reruns
`make setup` automatically before `make extract`.

If setup still cannot provide `splat`, delete only:

```text
extern\dkr-decomp\.venv
```

and rerun `Build-DKR-Runtime.cmd`. The decomp checkout and validated ROM can
remain in place.

## `: invalid option nameefail` during the WSL build

This indicates that a Windows CRLF carriage return reached Bash in the
`set -euo pipefail` line. Milestone 0.4.4 writes an explicit UTF-8, BOM-free,
LF-only shell script into `build-logs` and executes it directly through WSL.
Replace `scripts/Prepare-DKR-Runtime.ps1` with the 0.4.3 version and rerun
`Build-DKR-Runtime.cmd -BuildRenderer`; no clean operation is required.

## N64Recomp fails but the main transcript shows no diagnostic

PowerShell transcripts do not reliably capture the output of native console
programs. Milestone 0.4.4 runs N64Recomp with redirected stdout and stderr and
writes both streams to:

```text
build-logs\n64recomp-<timestamp>.stdout.log
build-logs\n64recomp-<timestamp>.stderr.log
```

The script also prints the captured output into the console and includes the
first non-empty diagnostic in the final error message. Rerun:

```bat
Build-DKR-Runtime.cmd -BuildRenderer -SkipApt
```

Do not clean the repository. The matching DKR ELF, N64Recomp build and RT64
checkout are reusable.


## N64Recomp says `Could not find entrypoint function`

The ROM header contains a processor start address, but N64Recomp requires the
configured entrypoint to resolve to a function in the ELF metadata. DKR's
matching ELF may represent the ROM bootstrap address as non-function startup
code. The runtime scripts therefore inspect the ELF with the MIPS binutils,
resolve the documented `mainproc` boot function, and enable `.mdebug` parsing
when that metadata section is available.

Run `Diagnose-DKR-Recompile.cmd` after applying this version. It refreshes the
existing generated TOML without rebuilding the DKR ROM or toolchain.
