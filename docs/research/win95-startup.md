# Windows 95 — the time from launch to the first image

Measured on the test machine (86Box, Pentium II 400, Voodoo 2), 1 October 2026,
from the runtime log's `[perf][startup]` events and the renderer's
`[game] mode=... at t=` line, both on the timing export's clock. A warm start:
the ROM identity cache already filled.

| Step | Before | After | What changed |
|---|---:|---:|---|
| Log ready | 0.79 s | 0.77 s | |
| Game registered | 1.89 s | 1.85 s | |
| ROM selected | 4.17 s | **1.87 s** | patch 0062: librecomp takes the runtime's hash rather than reading and hashing 12 MB again |
| Game thread started | 4.19 s | 1.88 s | |
| First image | 9.89 s | **5.16 s** | 0062; patch 0063, the RSP inverse square root table computed from an estimate, 1.1 s; the card opened beside the game's boot rather than before it, 1.25 s |

The first image was at 9.9 s in the morning's runs and is at 5.2 s now: 4.7 s
less, all of it waiting that the player sees as a black screen.

## How each was found

- **The ROM selection.** The log's own startup events: 2.3 s between
  `game-registered` and `runtime-rom-selected`, on every start. `select_rom`
  read and hashed the file the runtime had just identified by the same XXH3.
- **The table.** The sampler (`DKR_TRACE_SAMPLER=8`, the first eight seconds)
  put `recomp::rsp::constants_init()` at 41% of the executable's samples. Its
  inverse square roots were found by counting up from 1 << 17: some 25 million
  steps of 64-bit arithmetic. Starting from a floating-point estimate gives the
  same 512 entries — compared one by one on the host in a 32-bit build, where
  the counting took 57.7 ms and the estimate 0.07 ms.

- **The card's opening.** The same sampler run put 57% of the moving samples
  in `GLIDE2X.DLL`, on the graphics thread. Timed, `grSstWinOpen` takes 2.9 s
  -- the driver's own, not this port's to shorten. But ultramodern starts the
  VI thread, whose first refresh starts the game, only once the renderer is
  constructed, so the game's boot waited for the driver. `GlideRenderer` now
  opens the card in `update_screen`, right after `recomp::start_game`, on the
  same graphics thread: the two overlap, and the lists the game submits
  meanwhile wait in the action queue. Measured interleaved on one executable,
  `DKR_GLIDE_OPEN_EARLY=1` restoring the old order: the first image at 5.16 s
  and 5.18 s against 6.41 s, the same lists and triangles after a minute.

## What remains

The card's opening, 2.9 s, now bounds the first image: the game's boot ends
before it. Below that, only a faster driver or a card opened earlier still
-- in another thread, which Glide 2 is not documented to allow.

The first start, with an empty identity cache, adds 2.3 s of hashing to the
runtime's own inspection (E02-S04); it has no feedback beyond the window.
