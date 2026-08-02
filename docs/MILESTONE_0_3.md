# Milestone 0.3 — boot bridge and mappable controls

## Delivered boundary

This milestone creates the native boundary immediately before the decompiled DKR game loop:

```text
validated user ROM
    -> reopen + normalise + SHA-1 verify
    -> parse N64 header
    -> allocate 4 MiB host RDRAM
    -> fixed 60 Hz host tick
    -> remapped keyboard/controller input
    -> N64 button mask + signed stick packet
    -> DKR mainproc/thread bridge (next)
```

The host does not interpret MIPS instructions and is not an emulator. It also does not claim to run
the DKR title/menu state yet. Its purpose is to make ROM ownership, host memory, fixed-step timing and
controller input real and testable before the decompiled source is attached.

## Input contract

`InputManager::SampleController()` emits:

```cpp
struct N64ControllerState {
    std::uint16_t buttons;
    std::int8_t stickX;
    std::int8_t stickY;
};
```

The button layout matches the N64 `OSContPad.button` bit layout. Analogue values are radially
normalised after deadzone processing and clamped to `-80..80`, which is the range expected by the
original game-side joypad code.

The control profile is versioned JSON and uses stable symbolic SDL names rather than device-specific
numeric codes. Controller hot-plugging is supported. Player One is the only exposed profile in this
milestone; four-player profile/device assignment follows after the real game controller manager is
connected.

## ROM-source persistence

The generated O2R remains asset-free. A separate local configuration file stores:

- Absolute source ROM path.
- Validated SHA-1.
- Schema version.

Every boot session reloads the source and recalculates SHA-1. Moving, replacing or modifying the ROM
causes a clear reconnect/validation error instead of silently booting different data.

## Host memory

`BootSession` owns a cleared 4 MiB byte arena and the normalised ROM image for the lifetime of the
session. This is deliberately simple. The next milestone will place typed memory services in front of
the arena:

- N64 virtual/physical address translation.
- Segmented-address resolution.
- Big-endian typed reads/writes.
- Game heap arenas.
- PI DMA/resource requests.
- Message queues and thread scheduling.

## Exit criteria for the next milestone

The next milestone is complete only when:

1. The pinned DKR source is compiled as host C/C++ units rather than only downloaded.
2. `mainproc` can enter through a host-owned replacement for the N64 boot thread.
3. Core message queues, VI ticks and PI reads have host implementations.
4. DKR's joypad reads consume the same `N64ControllerState` generated here.
5. The process advances deterministically to the first graphics task or a precisely documented
   unsupported hardware boundary without crashing.

Rendering the title screen requires the subsequent F3DDKR bridge and extracted resources; reaching a
graphics-task boundary is not the same as rendering it.
