# E09-S05 — Packaging and distribution

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E01-S04, E06-S06, E09-S03, E09-S04 |
| **Blocks** | — |

## Context

The package has to install on a 1998 machine, which rules out modern formats: no archive
in a recent format, no installer requiring an absent runtime, no long file name if the
medium is FAT16.

Three of the project's rules stay whole and take precedence over any consideration of
convenience:

- **no asset redistributed.** No ROM, no save, no extracted asset — the package is
  scanned before distribution (`scripts/scan_for_game_assets.py`, required by
  `docs/ASSET_POLICY.md`);
- **the licences.** The runtime inherits N64ModernRuntime's GPL-3.0
  (`runtime-recomp/CMakeLists.txt:57-59`); to which are added the conditions of 3dfx's
  Glide sources;
- **the guard rails** of E01-S01 and E01-S04 apply to the binary actually distributed,
  not only to the development one.

## Objective

To produce a package installable on Windows 95, complete, verified, and conforming to the
distribution rules.

## Scope

**In:** packaging, installation, user documentation, the checks.

**Out:** the game's content.

## Work

1. Choose the format. A simple ZIP archive to unpack into a folder is probably sufficient
   and more robust than an installer; if an installer is retained, it must work with no
   modern dependency.
2. Compose the package: executable, redistributables decided by E01-S03, commented
   default configuration (E06-S05), documentation, licences.
3. Deal with Glide's DLLs. They are supplied by the card's driver and must not be
   redistributed; the package must state clearly what the user has to have installed, and
   the program must check it at launch (E05-S01).
4. Write the `README.TXT`: required configuration, installation, where to place one's ROM
   (E06-S06), settings available, known problems, accepted rendering differences
   (`docs/RENDER-DIFFERENCES.md`).
5. Respect the 8.3 naming constraints everywhere the medium may be FAT16.
6. Automate building the package, on the model of `scripts/Package-Windows.ps1` and
   `Package-Linux-AppImage.sh`, integrating the blocking checks into it: instruction set
   (E01-S01), PE imports (E01-S04), absence of assets (`scan_for_game_assets.py`), tests
   (E09-S03).
7. Check the installation on a pristine emulated machine, then on real hardware —
   unpacking, launching, a first game, with no development tool present.
8. Gather the licences: the runtime's GPL-3.0, the conditions of the 3dfx sources, and
   the notices already present in `THIRD_PARTY.md`.

## What was built (1 October 2026)

`scripts/Package-Win95.sh` writes `dist/win95/DKRR-W95.ZIP`, 3.1 MB:
`DKRR.EXE` (stripped, 7.4 MB), `README.TXT`, `CONFIG.TXT`
(`docs/CONFIGURATION.md`), `LICENSE.TXT`, `COPYING.TXT`, `THIRDPTY.TXT`, all
text in CRLF. No `DKRR.INI`: the game writes its commented template at first
start. The instruction-set check needs the symbols and runs on the build's
copy; the script proves the shipped `.text` identical to it.

## Acceptance criteria

- [ ] The package installs and launches on a pristine Windows 95 machine. *On the test
      machine*: unpacked into a new folder with a ROM beside it, it wrote its `DKRR.INI`,
      found the ROM and ran (1 October 2026). That machine is the development VM, not a
      pristine one.
- [x] No game asset is included, verified by `scan_for_game_assets.py` (with `--allow DKRR.EXE`, the one file past its 5 MiB bound, still checked for ROM headers) and by name for ROMs and `GLIDE2X.DLL`.
- [ ] The licences are complete and accurate, Glide sources included. *Shipped*: the
      port's MIT licence, the GPL-3.0 that N64ModernRuntime brings to the executable, and
      `THIRD_PARTY.md`. Not reviewed by anyone qualified, and nothing on the Glide headers'
      own terms is in the package yet.
- [x] The Glide DLLs are not redistributed, and their absence is reported clearly at
      launch: a message box naming the card, the driver and `GLIDE2X.DLL`, checked on the
      test machine with the DLL removed and put back.
- [x] The `README.TXT` covers required configuration, installation, ROM, settings, known
      problems and rendering differences (`packaging/win95/README.TXT`).
- [x] The file names are 8.3-compatible, checked by the package script.
- [x] Building the package is automated with all the blocking checks: `scripts/Package-Win95.sh` -- instruction set, imports, platform tests, assets, 8.3 names.
- [ ] The installation is verified on the emulator **and** on real hardware.

## Risks

A package that assumes the presence of a non-redistributable component — a version of
DirectX, a C runtime, a 3dfx driver — will fail for some of the users, on machines nobody
has access to in order to diagnose. Step 7's check on a pristine machine is what catches
those assumptions before they become failure reports.

## References

- `docs/ASSET_POLICY.md`, `scripts/scan_for_game_assets.py`
- `scripts/Package-Windows.ps1`, `scripts/Package-Linux-AppImage.sh`
- `RELEASE-VALIDATION.md`, `THIRD_PARTY.md`
- `runtime-recomp/CMakeLists.txt:57-59` — the runtime's GPL-3.0 licence
