# Build validation record

Validation date: **1 August 2026**

## Release candidate

This record covers **DKR Port 1.0.0-rc** and the unified one-window launcher/runtime release.

## User-confirmed gameplay baseline

Interactive Windows testing confirmed:

- Diddy Kong Racing and Nintendo/Rare intro sequences render correctly;
- original audio is synchronized and stable during races;
- menus, Start Game, Options, and Adventure mode are reachable;
- multiple races can be completed in Adventure mode;
- gameplay runs at the original 30 FPS cadence and correct simulation speed;
- vehicle wheels, plane/hovercraft propellers, fans, and other billboards render;
- no observed gameplay, graphics, or audio faults remained before launcher integration.

## Unified application validation

- SDL2 now owns one cross-platform window from startup through RT64 gameplay.
- The startup surface provides ROM selection, remembered validation state, presentation settings, controls, and legal guidance.
- The in-game ImGui/RT64 overlay is opened by F1 or controller Back/View and is created lazily so closed-overlay gameplay adds no per-VI UI work.
- Escape also opens and closes the overlay before events reach focused ImGui controls.
- The transparent overlay was captured over live widescreen gameplay, and its Exit to Desktop confirmation now opens in the correct ImGui parent scope and shuts the runtime down cleanly.
- The launcher, overlay, control remapping surface, and narrow 800x600 layout were exercised without horizontal clipping.
- Runtime settings persist independently of ROM data.
- Keyboard handling uses SDL scancodes on both Windows and Linux.
- The master volume setting is applied in the host audio output path without changing original audio timing.
- Windows Release is linked as a GUI subsystem executable, so no diagnostic console is shown to players.

## Scheduler robustness

The launch regression was reproduced without renderer trace logging: DKR's idle thread could spin inside the runtime's lock-free external-message dequeue while hundreds of valid VI/AI/SP/DP messages were already queued. That timing-sensitive failure caused the apparent black-screen crash, full CPU fan load, audio starvation, and 10 FPS presentation; diagnostic logging could mask it.

Patch Pipeline change `0016-use-reliable-external-message-fifo.patch` replaces only the host external hardware-event queue with a mutex/condition-variable FIFO while preserving the existing queue interface and game-side timing. Generated sources and protected dependencies were not edited manually.

The final no-trace 34-second stress run completed 953 F3DDKR tasks, delivered all 1,916 SP and 953 DP events without a blocked DP send, held DKR's original `video-delta=2` cadence, and stopped cleanly. Five additional Escape-overlay lifecycle runs passed consecutively.

## Build evidence

- Windows MSVC Release: `build/dkr-runtime-rt64/bin/Release/DKRPort.exe`
- Windows subsystem: `IMAGE_SUBSYSTEM_WINDOWS_GUI`
- Linux GCC Release: `build/dkr-runtime-linux/bin/Release/DKRPort`
- Linux renderer backends: Vulkan with SDL2 window integration
- N64Recomp policy regeneration: successful, 3,826 functions
- Protected dependency and generated directories: no manual edits

## Package evidence

Windows:

- `dist/DKRPort-1.0.0-rc2-Windows-x64.zip`
- Size: 13,924,002 bytes
- SHA-256: `8B874C7DAC0B5F358E7108F2DD85DAC0F1A3746175F51EDB412F07F4FAABEAD0`
- Includes the runtime, SDL2, DXC runtime DLLs, README, project licence, GPL copying notice, and third-party notices.
- ZIP entry scan found no `.z64`, `.v64`, or `.n64` file.
- The packaged executable passed the Memory Pak round-trip and backup-recovery self-test.

Linux:

- `dist/DKRPort-1.0.0-rc2-Linux-x86_64.AppImage`
- Size: 17,459,704 bytes
- SHA-256: `7E6D336D1100C96A34923F3B9CAB659DC1CEB10CA1C8730988F24692911785D7`
- AppImage extraction verified `AppRun`, `usr/bin/DKRPort`, desktop metadata, icon, licences, and bundled non-system dependencies.
- Extracted AppImage scan found no `.z64`, `.v64`, or `.n64` file.
- The AppImage executable passed the Memory Pak round-trip and backup-recovery self-test under Ubuntu 24.04/WSL2.

## Remaining release gate

The overlay, widescreen Adventure transition, HUD restoration, maximum-detail models, wheels, and clean desktop exit are visually confirmed in the current unified Windows session. Before promoting RC2 to the final public tag, complete one race from the packaged Windows ZIP with a physical controller and listen through the full intro-to-character-select transition once; those two hardware/listening checks cannot be certified by unattended automation.
