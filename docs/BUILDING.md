# Building DKR Port Milestone 0.4.4

## Native Windows launcher

Extract the repository to a short writable path, preferably `C:\DKRPort`, then run:

```text
Build-Windows.cmd
```

The script detects or installs Git, native Windows CMake, Visual Studio 2022 C++ tools, a Windows SDK,
a pinned vcpkg checkout, SDL3 and RmlUi. It rejects MSYS/Cygwin CMake builds that cannot generate a
Visual Studio project.

After a successful build:

```text
Run-Windows.cmd
```

Output:

```text
dist\DKRPort-Windows-x64\DKRPort.exe
```

Useful options:

```text
Build-Windows.cmd -Clean
Build-Windows.cmd -Configuration Debug
Build-Windows.cmd -NoInstall
Build-Windows.cmd -SkipTests
```

Logs:

```text
build-logs\build-windows-*.log
build-logs\msbuild-*.log
build-logs\msbuild-*.binlog
```

## First DKR native-runtime preparation

Requirements:

- The launcher has validated and connected DKR US 1.0.
- Windows Subsystem for Linux with an Ubuntu/Debian distribution.
- Enough disk space for the DKR decomp, N64ModernRuntime and its pinned N64Recomp submodule, optional RT64, and build trees.
- Internet access for Git/dependency preparation.

Run:

```text
Build-DKR-Runtime.cmd
```

The script builds the matching DKR ELF under WSL, generates native C through the N64Recomp revision pinned by N64ModernRuntime and attempts a
Windows compile against N64ModernRuntime. It records exact resolved runtime commits in a local ignored
JSON file.

Optional flags:

```text
Build-DKR-Runtime.cmd -SkipApt
Build-DKR-Runtime.cmd -Force
Build-DKR-Runtime.cmd -SkipRecompile
Build-DKR-Runtime.cmd -SkipProbe
Build-DKR-Runtime.cmd -BuildRenderer
Build-DKR-Runtime.cmd -Jobs 12
```

`-BuildRenderer` also configures RT64 and is deliberately optional because RT64 adds a much larger
build and may expose renderer-specific prerequisites before DKR game registration exists.

A runtime-preparation failure is not necessarily a launcher/build-system failure. The first failing
N64Recomp symbol, generated source file, runtime API or RSP configuration becomes the next concrete
porting task. Upload the complete `prepare-dkr-runtime-*.log` and any `dkr-runtime-msbuild-*.log` when
reporting that boundary.

## Linux and macOS helper builds

The supplied Linux/macOS helpers currently build the dependency-free core and tests rather than the
complete native launcher/runtime. The target remains cross-platform, but Windows is the first
physically exercised launcher and runtime-preparation platform.
