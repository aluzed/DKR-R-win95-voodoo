# E09-S05 — Packaging and distribution

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | TODO |
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

## Acceptance criteria

- [ ] The package installs and launches on a pristine Windows 95 machine.
- [ ] No game asset is included, verified by `scan_for_game_assets.py`.
- [ ] The licences are complete and accurate, Glide sources included.
- [ ] The Glide DLLs are not redistributed, and their absence is reported clearly at
      launch.
- [ ] The `README.TXT` covers required configuration, installation, ROM, settings, known
      problems and rendering differences.
- [ ] The file names are 8.3-compatible.
- [ ] Building the package is automated with all the blocking checks.
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
