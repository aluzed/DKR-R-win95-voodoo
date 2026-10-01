# E02-S04 — ROM access and peripheral-bus DMA

| | |
|---|---|
| **Epic** | E02 — Windows 95 system substrate |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E00-S06, E02-S01 |
| **Blocks** | E02-S06 |

## Context

The game reads its data by DMA from the cartridge. `librecomp` translates those
transfers into accesses to the ROM image, which it presumably loads entirely into
memory — 12 MB for DKR, which is painless on a modern machine and represents nearly
20 % of a 64 MB machine's budget.

Two other points change in nature on the target:

- **ROM validation.** Computing the SHA-1 of 12 MB, instantaneous today, takes a
  noticeable time on a Pentium II. To be measured, and made visible to the user if
  necessary.
- **Provenance.** The current SDL launcher presents a graphical file selector; it
  disappears with SDL2 (E06-S06).

The neighbouring native port met exactly this problem and solved it with a ROM
address space over files, read by seek and read rather than loaded into memory — an
approach directly transposable.

## Objective

To make ROM access conform to E00-S06's memory budget, without changing the
transfers' semantics as the game sees them.

## Scope

**In:** loading the ROM, validating it, and the DMA path.

**Out:** the user's file selection (E06-S06) and the saves (E02-S05).

## Work

1. Record how `librecomp` loads the ROM and serves the DMA transfers, and measure the
   real memory footprint.
2. Apply E00-S06's decision: a complete image in memory, or reading piece by piece.
   If it is reading piece by piece, implement a block cache sized on the observed
   access patterns — the cache's size is chosen on a measurement, not on an intuition.
3. Measure the transfers' cost. The game loads data during screen transitions; a 1998
   disk access is slow, and a level load that takes ten seconds is a visible
   regression even if the rendering is perfect.
4. Measure the SHA-1 validation time on the target. If it exceeds a handful of
   seconds, provide a progress indicator, or a cached result keyed by path, size and
   date — never a bypass of the validation itself.
5. Deal with byte order. The ROM may be supplied as `.z64`, `.n64` or `.v64`; the
   normalisation already exists in the runtime (the README mentions a SHA-1 "after
   byte-order normalisation") — check that it does not assume the whole image is in
   memory.
6. Deal with Windows 9x file paths: short names, code-page paths, the absence of a
   working Unicode API (E01-S03).
7. Check that the DMA completion behaviour — the message posted to the requesting
   thread — stays identical to the modern host's.

## Acceptance criteria

- [x] The ROM's memory footprint respects E00-S06's budget -- since 1 October 2026,
      patch 0061: a big-endian ROM (`.z64`) is read on demand through a 1 MiB block
      cache instead of held whole. Measured in a race on the test machine: 33.6 MB of
      free physical memory instead of 23.0 MB, the frame period unchanged. The cache's
      size was measured, not guessed: 64 KiB took 2438 misses in 6144 reads, 256 KiB 746,
      1 MiB 408, with no difference in the frame period; 1 MiB was kept. The three
      formats benefit alike: a `.v64` or `.n64` is first normalised into a big-endian
      copy under `dkr-runtime-data\rom-cache`, and that copy is what is read -- a race
      from a `.v64` measured 33.3 MB free, from a `.n64` 33.7 MB.
- [x] The game loads and starts from a valid ROM under emulated Windows 95 -- every run
      since September; from a ROM found beside the game since 1 October (E06-S06).
- [ ] A level's load time is measured and compared against the modern host's.
      *Measured on the target* (1 October 2026, ROM read on demand): the game sends
      no display list while it loads, and the renderer now prints every gap over
      200 ms and every change of the level header (`[game] no list for ...`,
      `[game] level header=...`). Entering a race at Ancient Lake froze the screen
      for 0.98 s; an attract-mode level, 1.07 s; steady play never reaches 200 ms.
      These are under 86Box, whose disk is the host's file: a 1998 disk would add
      its seek time to the misses, about 400 for a race. *Not compared*: this
      checkout has no build of the modern host to compare with.
- [ ] The SHA-1 validation's duration is measured; if it is perceptible, it is
      accompanied by user feedback. *Measured* (1 October 2026): hashing the ROM takes
      2.3 s, on the first start only -- the identity cache now works on this target, and
      later starts take 15 ms. That first start has no feedback beyond the window.
- [x] The three ROM formats are accepted -- `.z64`, `.v64`, `.n64`, each started on the
      test machine (E06-S06). All three are read on demand (patch 0061), the
      byte-swapped two from their normalised copy.
- [x] Windows 9x paths are handled, including with short names: the ROM search and the
      runtime use the narrow `...A` APIs and `path.string()` throughout -- the wide forms
      are stubs here, the cause of two defects fixed on 1 October -- with ROMs found at
      `D:\\DKR.Z64`, `D:\\NOROM\\DKR.N64` and `D:\\DKRTEST\\DKR.Z64`.
- [ ] The DMA completion semantics are unchanged.

## Risks

Reading piece by piece introduces latency where there was none. If a transfer is
served during a game frame rather than during a loading screen, it produces a
stutter. Spot the mid-game transfers before choosing the strategy, not afterwards.

## References

- `docs/ROM_SETUP.md`
- `README.md` — the SHA-1 expected after normalisation
- `../../Diddy-Kong-Racing/docs/stories/E02-assets/E02-S04-chargeur-assets-fichier.md`
  — the same problem, already dealt with on the native port's side
