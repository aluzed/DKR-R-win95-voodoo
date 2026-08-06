# ROM setup

## Accepted input

Milestone 0.4.4 recognises standard Nintendo 64 dump byte orders:

- `.z64` — big-endian
- `.v64` — byte-swapped
- `.n64` — little-endian

The first four bytes determine the real byte order; the extension is only a file-selection filter.

## Initial supported revision

```text
Game: Diddy Kong Racing
Region: US
Revision: 1.0 / v77
Canonical SHA-1: 0cb115d8716dbbc2922fda38e533b9fe63bb9670
Expected size: 12 MiB
```

## Native selection flow

1. Select **Choose ROM** in the native launcher.
2. The operating system opens its own file-selection window.
3. DKR-R rejects missing, oversized, truncated or non-N64 files.
4. The ROM is normalised in memory without modifying the source file.
5. SHA-1 is calculated on the canonical big-endian form.
6. The revision is matched against the supported-ROM registry.
7. The validation buffer is released.
8. The source path and validated SHA-1 are stored in local configuration for the boot host.

The source ROM is not renamed, moved, modified or copied into the port directory. Every game-host
session reopens the source, normalises it in memory and verifies SHA-1 again before use.

## Generated files

After a successful match:

```text
runtime/game/manifest.json
runtime/game/dkr.o2r
runtime/config/rom-source.json
```

The current `dkr.o2r` is a ZIP-compatible placeholder containing only `manifest.json` and
`LEGAL-NOTICE.txt`. It contains no textures, models, audio or other original game data. The separate
`rom-source.json` contains only the local source path, SHA-1 and schema version; it is never packaged.
A later milestone will replace the local manifest-only archive with user-side typed extraction.
