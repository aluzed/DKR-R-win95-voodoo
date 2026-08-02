# Milestone 0.5 — First Native Game Boot

## Product rule

The release build must be one-click for the player:

1. Download the prebuilt application.
2. Launch it.
3. Select a supported, legally obtained Diddy Kong Racing ROM.
4. Let the launcher validate and prepare local game data inside the native window.
5. Select **Play**.

The player must not install WSL, Git, CMake, Visual Studio, Python, N64Recomp,
N64ModernRuntime, RT64, or the DKR decompilation project. Those are developer
and CI dependencies used to produce the release executable before publication.

## Developer pipeline versus player pipeline

### Developer/CI pipeline

The developer build performs the expensive and source-oriented work:

- Build the matching DKR ELF from the pinned decompilation revision.
- Generate native DKR CPU functions with N64Recomp.
- Recompile the graphics and audio RSP microcode.
- Compile N64ModernRuntime, librecomp, RT64, the generated DKR functions and
  project-specific patches into the final application.
- Package platform binaries and redistributable launcher assets.

Generated native code is not copyrighted ROM data and can be built into the
release, subject to the licences of the source and dependencies. The ROM and
ROM-derived game assets are never distributed.

### Player first-launch pipeline

The installed launcher performs only runtime data preparation:

- Select and validate the ROM.
- Normalise `.z64`, `.v64` or `.n64` byte order in memory.
- Store the ROM path and validated SHA-1.
- Create a versioned, atomic local cache only where performance requires it.
- Initialise saves, configuration, controller profiles and shader caches.
- Start the already-compiled game runtime.

No source checkout or compilation should happen on the player's machine.

## Milestone 0.5 implementation order

### 1. Promote generated DKR code into a real target

Replace the probe-only target with a permanent `dkr_recompiled_game` static
library built from `runtime-recomp/RecompiledFuncs`.

The generated source list must be deterministic and regenerated only by the
developer preparation command. A build-time manifest records the source ROM
revision, DKR decomp commit, N64Recomp commit and generated file count.

### 2. Create the game executable boundary

Add a real runtime target initially named `DKRPortGame` with:

- `GameRuntime.cpp`
- `GameRegistration.cpp`
- `PlatformCallbacks.cpp`
- `SaveCallbacks.cpp`
- `AudioCallbacks.cpp`
- `ControllerCallbacks.cpp`
- `RendererCallbacks.cpp`
- `FatalError.cpp`

The launcher and game may remain one executable if lifetime management is
reliable. A separate child process is acceptable for the first boot milestone
because a game crash cannot take the launcher and diagnostics down with it.
The final decision will be based on RT64 and runtime ownership constraints.

### 3. Register DKR with librecomp

Provide:

- The `0x80100400` ROM entrypoint.
- Recompiled function lookup tables.
- The canonical ROM image and size.
- Four MiB of RDRAM.
- Section and overlay lookup information.
- PI DMA access to the user-owned ROM.
- EEPROM/save callbacks.
- A controlled boot thread.

The first acceptance point is reaching DKR's initial game thread without an
unhandled function lookup, invalid memory access or deadlock.

### 4. Connect controls

The current remapping system already produces N64 button masks and signed stick
values. Route those packets into the runtime controller callback for player one.

The first game build must support:

- Keyboard.
- SDL game controllers.
- Hot-plugging.
- Analogue deadzone and inversion.
- Remapping from the native launcher.

The same control profile must be used in the launcher and game.

### 5. Establish audio output

Register the runtime audio callback and feed PCM to SDL with a bounded ring
buffer. Instrument the first audio task before attempting subjective fixes.

Initial acceptance criteria:

- Audio device opens without blocking boot.
- The game can submit samples continuously.
- No buffer overwrite, runaway latency or crackling during a ten-minute soak.
- Title/menu music and sound effects are audible once the audio RSP path runs.

### 6. Recompile DKR's RSP programs

Identify every graphics and audio microcode image submitted by the game and
produce pinned RSPRecomp configurations.

Graphics work centres on Rare's F3DDKR microcode. The implementation must map
its custom commands and state into the renderer without approximating away
behaviour required by the game.

Acceptance sequence:

1. First RSP task recognised.
2. First graphics task completes.
3. First VI framebuffer is presented.
4. First audio task completes.
5. Repeated tasks run without leaking memory or deadlocking.

### 7. Register RT64 and display the first frame

Start with original behaviour:

- 4:3 presentation.
- Original simulation rate.
- No widescreen patches.
- No interpolation.
- No texture replacement.
- No graphical enhancements except what is required for correct output.

The first visible target is the earliest stable DKR boot/title frame, followed
by correct menus and a controlled transition into a race.

### 8. Integrate the launcher preparation state

Replace the developer-facing **Start Game Host** flow with these player states:

- `ROM required`
- `Validating game`
- `Preparing local data`
- `Optimising shaders`
- `Ready to play`
- `Launching`
- `Game running`
- `Repair required`

Preparation runs on a worker thread and reports progress inside RmlUi. Every
stage is resumable and writes atomically. The **Play** button is enabled only
when the runtime and local data manifest are compatible.

## Release cache design

The cache key must include:

- ROM SHA-1.
- Application version.
- Resource schema version.
- Renderer cache version.
- Runtime/decomp compatibility version.

A failed or interrupted update must never destroy a previously working cache.
New data is written to a staging directory, validated and atomically promoted.

## Diagnostics

On a failed first boot, the launcher must retain:

- The last completed preparation stage.
- DKR and runtime dependency revisions.
- The first unhandled function or RSP task.
- Renderer initialisation result.
- Audio-device result.
- A redacted crash log.

The user-facing message remains plain English and provides **Retry**, **Repair**,
**Open Logs** and **Copy Diagnostics** actions.

## Milestone 0.5 acceptance gates

Milestone 0.5 is complete only when a Windows release build can:

1. Accept and validate the supported ROM from the native launcher.
2. Start the recompiled DKR entrypoint.
3. Run the initial N64 threads and ROM DMA path.
4. Display a stable DKR frame through RT64.
5. Produce recognisable game audio.
6. Accept remapped keyboard or controller input in the actual game.
7. Reach and leave the title/menu flow repeatedly.
8. Exit cleanly back to the launcher or desktop.
9. Repeat the process after restart without rebuilding or re-extracting.
10. Produce actionable logs for every failed boundary.

## Quality bar after first boot

A first frame is not a release. Following the first boot, testing expands to:

- Full title and menu flow.
- Character selection.
- One complete race.
- Save creation and reload.
- Every graphics effect used by the selected race.
- Audio timing and ten-minute soak tests.
- Input latency and analogue parity.
- Crash, sanitizer and deterministic-state testing.

The eventual stable release target is no known progression blocker, crash,
save corruption, major rendering fault, major audio fault or material gameplay
difference from the supported original ROM.
