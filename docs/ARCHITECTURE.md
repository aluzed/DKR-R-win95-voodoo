# Architecture

## 1. Native launcher

The shipping front end is one SDL3 window rendered with RmlUi. It handles ROM selection, validation,
settings, diagnostics and mappable controls without a browser or local web server.

```text
SDL3 window/input/file picker
        ↓
RmlUi launcher and configuration pages
        ↓
ROM validation + local configuration + input profiles
```

## 2. Validated ROM boundary

The launcher accepts `.z64`, `.v64` and `.n64`, normalises byte order in memory and checks the US 1.0
SHA-1. Only the selected path and expected hash are stored locally. Each host/runtime start must reopen
and revalidate the source.

## 3. Existing ROM-backed host

The current `BootSession` is an integration test harness. It provides:

- N64 header parsing;
- a cleared 4 MiB RDRAM compatibility arena;
- fixed 60 Hz host ticks;
- live N64-format buttons and analogue packets.

It does not execute MIPS instructions or the decompiled game loop.

## 4. Native DKR execution route

Milestone 0.4 adopts a static-recompilation boundary for the original executable while retaining the
DKR decomp as authoritative metadata, symbols, structure definitions and patch reference.

```text
User-owned DKR US 1.0 ROM
        +
Pinned DKR decomp → matching ELF and symbols
        ↓
N64Recomp → native C CPU functions
        ↓
N64ModernRuntime
  ultramodern: threads, queues, timers, VI, controllers, audio, RSP task dispatch
  librecomp: generated-code bridge, PI DMA, overlays and cartridge saves
        ↓
DKR-specific host callbacks and patches
        ↓
RSP recompilation
  Rare F3DDKR graphics microcode
  DKR audio microcode
        ↓
RT64 renderer + host audio + existing remappable input
```

This route avoids manually porting every libultra call in the completed decomp before the first boot.
The decomp remains essential because N64Recomp needs a matching ELF/metadata and because readable DKR
source is the best reference for patches, graphics commands, audio tasks and gameplay validation.

## 5. Runtime work area

`runtime-recomp/` contains only source-controlled preparation code and documentation. Generated CPU
functions, ROMs, extracted assets and dependency checkouts are ignored and excluded from release ZIPs.

`Build-DKR-Runtime.cmd` creates the local working boundary and compiles `DKRRuntimeProbe`. That probe
proves that generated CPU functions and N64ModernRuntime can share one Windows build; it is not itself
the final game executable.

## 6. Next executable layer

The first real boot executable needs:

- a `recomp::GameEntry` for DKR and the correct entrypoint;
- ROM/PI, save, time, input and audio callbacks;
- generated-function registration and patch libraries;
- F3DDKR and audio RSP microcode output;
- an RT64 render context registered with ultramodern;
- launcher-to-runtime handoff and clean return/error handling.
