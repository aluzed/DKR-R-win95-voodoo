# E02-S05 — Saves: EEPROM and Controller Pak

| | |
|---|---|
| **Epic** | E02 — Windows 95 system substrate |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | S |
| **Depends on** | E02-S03 |
| **Blocks** | E09-S03 |

## Context

DKR saves its progress in EEPROM, and the game manages four virtual Controller Paks.
DKR-R implements all of that in code belonging to the project —
`virtual_pak.cpp` (24 KB), `save_manager.cpp` (25 KB), `dkr_save_codec.cpp` (17 KB) —
and not in a dependency. It is portable C++ whose logic has no reason to change.

What changes belongs entirely to file writing:

- where the save files live — there is no `%APPDATA%` under Windows 95, and the
  application's folder is the usage of the period;
- the file names — the file system may be FAT16 in 8.3;
- write robustness — on this class of machine, a sudden power-off during a write is a
  common scenario, not an edge case.

The graphical save manager (`save_manager`, T.T.'s Save Manager) depends on ImGui and
disappears with it (E07-S02). The codec stays.

## Objective

To make the EEPROM and Controller Pak saves work under Windows 95, keeping the file
formats compatible with DKR-R's.

## Scope

**In:** storage, location, robustness, format compatibility.

**Out:** the graphical save-management interface (E07-S02).

## Work

1. Record how `librecomp` and `virtual_pak.cpp` determine where the files live, and
   introduce a path suited to Windows 95: the application's folder by default,
   overridable by the configuration (E06-S05).
2. Check that every file name produced fits in 8.3, or that the behaviour on FAT16 is
   verified rather than assumed.
3. Make the write atomic: write into a temporary file, flush the buffers, then
   replace. Check that the replacement behaves as expected under Windows 95 —
   `MoveFileEx` with replacement is not available there, so the sequence must be
   written by hand.
4. Keep DKR-R's file format, so that a save is transportable between the modern
   version and the Win95 one. That is also what will allow a game state to be prepared
   on the modern host in order to test a precise situation on the target — a real
   saving of time in QA.
5. Check the 18 existing test suites that bear on saving (`dkr_save_codec_tests.cpp`,
   `save_manager_tests.cpp`) and run them on the target.
6. Deal with the backup save: keep the previous copy, so that an interrupted write
   does not destroy a player's progress.
7. Check the behaviour when writing to a full disk and to a read-only medium — the
   message must be comprehensible, not a crash.

## Acceptance criteria

- [x] The EEPROM saves work under Windows 95: writing, reading back, persisting
      across a reboot. The first two through `save_manager_tests` on the machine; the
      third through a save written in one session, the machine shut down cleanly, then
      read back and re-encoded byte for byte in the next session.
- [x] The four virtual Controller Paks work, self-test included. A report from
      14 August 2026, `DKRR.EXE --self-test-pak` on the machine:

      ```text
      [boot][pak] recovered controller pak 4 from backup
      [test][pak] PASS: round-trip and backup recovery
      ```

      The check bears on the **real game binary**, not on a separate suite, and it
      exercises recovery from the backup copy — that is, the durable-write sequence
      itself.
- [x] The file names are 8.3-compatible, or the FAT16 behaviour is verified — both:
      long names work on this volume, and the names the layer builds fit in 8.3 so as
      to stay usable elsewhere.
- [x] The write is **as atomic as Windows 95 allows** — there is no atomic
      replacement — and a backup copy is kept. What the sequence guarantees and what
      it does not is written down, and **the power cut is now caused rather than
      simulated**: `kill -9` on the emulator, after more than 350 write rounds.

      ScanDisk found only one damaged file — `PWRCUT.TMP`, the one the sequence
      sacrifices — and round 370's save was read back intact after the reboot, without
      even falling back on the backup copy.

      That report does not prove the window is never hit: it is a draw, and the cut
      fell during the writing of the `.TMP`, which occupies most of each round. It
      proves that the sequence holds under a real cut, and that the volume repairs
      itself at the next boot.
- [x] A save produced by modern DKR-R is read by the Win95 version, and conversely —
      and the result is stronger than asked for: **both builds produce the same
      bytes**.

      | | SHA-256 |
      |---|---|
      | written by the host build | `2673ca1a…4dae47bc` |
      | written on Windows 95 | `2673ca1a…4dae47bc` |

      Each reads the other's back, decodes and re-encodes without a byte of
      difference. The witness (`tools/win95/witnesses/save_interchange.cpp`) lays
      **asymmetric** patterns in the 16- and 32-bit fields — `0x1234` and not `0x1221`
      — because a byte swap on a symmetric value does not show, and that is the main
      risk when the same structure is encoded by two different compilers.
- [x] `dkr_save_codec_tests` and `save_manager_tests` pass on the target — a report
      from 13 August 2026, the four phases then PASS. It took lifting three obstacles
      that only execution revealed: `<fstream>` unloadable, streams opened on a `path`
      going through the wide API, and `MoveFileExW` called directly. See
      [docs/research/win95-wide-streams.md](../../research/win95-wide-streams.md).
- [x] The codes are distinguished, carry a text, and **both edge cases are provoked
      on the machine** — medium absent (error 21) and disk full.

      The second required a volume one could fill: the transfer disk has half a
      gigabyte free, hence a 1.44 MB floppy filled in advance from the host and mounted
      as A:.

      ```text
      target             : A:\FULL.DAT
      size requested     : 2097152 bytes
      code returned      : 4
      text               : disk full
      verdict            : DISK FULL, correctly named
      ```

      The write attempted is larger than **the whole volume**, and not merely than the
      space remaining: a write that only just fitted would prove nothing reproducible,
      the free space depending on whatever is lying around on the medium.

## State as of 2026-08-13 — the write layer is delivered and measured

`platform/win95/fileio.{h,cpp}`, with its `test_fileio.cpp` suite (host **and**
`FILEIOT.EXE`). Report:
[`docs/research/win95-fileio.md`](../../research/win95-fileio.md).

**The ticket was right about `MoveFileEx`** — unlike two of E02-S03's assumptions,
which measurement disproved. But the form of its unavailability escapes both of the
repository's guard rails: `MoveFileExA` is exported, it has **real code**, and it
refuses all the same with `ERROR_CALL_NOT_IMPLEMENTED`. Neither the import check nor
the stub survey could see it. It is a **third category**, which only execution
reveals, and it is recorded in `tools/win95/exports/stubs/PROVENANCE.md`.

The write sequence is therefore manual, and its window accepted. **The guarantee
offered is not "the last write is never lost" but "a valid save is never lost"**:
losing the last race is annoying, losing the entire progress is unforgivable. `.TMP`
is never read back, because nothing proves it complete and DKR-R's format carries no
checksum.

| | |
|---|---|
| Suite on the host | 40 checks, 0 failures, clean under ASan |
| Suite on the target | **40 checks, 0 failures** |
| Long names on FAT16 | they work; the 8.3 alias truncates the extension |
| Refused write | error 21 `ERROR_NOT_READY`, distinct and usable |

The power cuts are not waited for but **simulated**, by building by hand the
intermediate states the sequence passes through — like E01-S03's wraparound.

### `<filesystem>`: the repository's rule was wrong, and expensive

This repository banned `<filesystem>` wholesale, attributing thirteen absent symbols
to it. Measurement says otherwise — full report in
[`docs/research/win95-filesystem.md`](../../research/win95-filesystem.md):

| | Blocking symbols | Does it load? |
|---|---:|---|
| `#include <filesystem>` alone | **0** | yes |
| a `std::filesystem::path` object | 1, a **stub** | **yes** |
| a call to `exists()` | 17, of which **7 absent** | **no** |

And `path` does more than load: it **works**, verified on the machine —
construction, `parent_path`, `filename`, `extension`, concatenation, all correct. It
is string manipulation, and string manipulation asks nothing of the system.

The gap in cost between the rule and reality is two orders of magnitude:

| | Occurrences | To do |
|---|---:|---|
| `std::filesystem::path` — the **type** | **250** | **nothing** |
| the operations | ~140 | to route through `fileio.h` |

Banning the header would have made us rewrite 250 uses for no gain, **and** led us to
believe the problem solved while the 140 that matter remained. The subset checker now
watches the operations, and them alone, with a self-test that exercises it in both
directions.

An immediate effect: **`ultramodern` no longer has a single forbidden include**, and
its ratchet has been removed — the checker flagged it itself.

### The seam is delivered

`platform/win95/fileio.hpp` diverts the four operations `librecomp`'s core uses —
`exists`, `remove`, `create_directories`, `copy_file` — **without touching the type**:
the signatures keep `std::filesystem::path`, and the 250 uses of the type stay
intact. A single suite exercises the equivalence of the two branches,
`std::filesystem` on the host and the `...A` APIs on the target: **12 checks, 0
failures on both sides**.

One of them deserves quoting, because it is not obvious: `remove` returns **false**
on an already-absent file, without that being an error. The caller wanted it gone, and
it is gone; but nothing was deleted, and `std::filesystem::remove` says so. A seam
that returned true would look correct and would make any code counting deleted files
lie.

Two traps met while wiring it are recorded in the report: a **false positive from the
instruction-set verifier**, which was disassembling the exception tables lodged in
`.text` — fixed — and the fact that **`std::random_device` does not work on this
target**, its path going through a stub.

### `librecomp`'s core is wired

Patch **0018**: the ten operation calls outside the mod system go through a seam,
`recomp::fs`, which the target fills with this ticket's layer. The type is not
touched.

| | |
|---|---|
| Operations routed in `librecomp`'s core | **10 out of 10** |
| `librecomp` on the 64-bit host | 26 units out of 26 |
| Modern target's suites | 18 out of 18 |
| Win95 target's suites | 4 out of 4 |

`copy_options` deliberately does not enter the seam: `overwrite_existing` is the only
form used, so it is **named** rather than parameterised. A seam that reproduces an
option nobody passes will be wrong the day somebody passes it.

What remains in `librecomp` is **the mod system** — 12 reports, with four more
operations among them `directory_iterator` — which
[E00-S01](../../research/win95-blockers.md) already names as the part this port does
not need. And two `<mutex>` in `recomp.cpp` and `pi.cpp`, which belong to E02-S02.

## Risks

Losing a player's progress is a port's least forgivable defect. Atomicity and the
backup copy are not comfort: on a 1998 machine without a UPS, the interrupted write
will happen.

## References

- `runtime-recomp/src/game/virtual_pak.cpp`, `save_manager.cpp`,
  `dkr_save_codec.cpp`
- `runtime-recomp/tests/dkr_save_codec_tests.cpp`, `save_manager_tests.cpp`
- `Build-Linux.sh:36` — Controller Pak self-test
