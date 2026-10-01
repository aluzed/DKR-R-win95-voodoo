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

The card is now open at 3.8 s on the timing export's clock and the first list
arrives at 5.2 s: the game's boot, sharing one processor with the driver, is
what bounds the first image now, not the card. Its profile -- inflating
assets (`gzip_inflate_codes`), the audio's initialisation, reading the ROM --
is the next thing to look at.

The first start, with an empty identity cache, adds 2.3 s of hashing to the
runtime's own inspection (E02-S04); it has no feedback beyond the window.

## Where the ROM reads go, and two attempts rejected

Read on demand (patch 0061), the ROM costs disk time rather than memory.
Counted with the processor's cycle counter in `do_rom_read`, the first minute
of attract mode copied 2.3 MB to the guest in about 640 million cycles, 1.6 s,
of which **99% was reading the file**: some 2.4 million cycles, 6 ms, for each
16 KiB block the cache did not hold, on 86Box's emulated disk. The copy itself
is noise. Two changes were measured, interleaved, and not kept:

- **Copying whole words** instead of `MEM_B` byte by byte: 678 and 643
  million cycles against 651 for the bytes -- no difference.
- **64 KiB blocks** instead of 16 KiB, the same megabyte of cache: 170 misses
  instead of 267, but 627 and 649 million cycles reading against 632, and the
  same pauses between lists. The cost is per access to the disk, not per byte.

The reads fall in the loading pauses, where a held ROM read nothing; before
patches 0061 and 0062 the same start read and hashed the whole 12 MB instead,
2.3 s. Whether a level's load is slower than with the ROM held is not
measured: the build no longer has a way to hold it, and a period disk is not
86Box's (`docs/TEST-ENVIRONMENT.md`).

