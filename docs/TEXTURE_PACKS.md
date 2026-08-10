# CRT filters and texture packs

These features belong to the **Modern** preset. Accurate always uses the
original game textures and presentation so it remains the release reference.

## CRT display filters

Graphics includes six optional built-in PNG masks. A mask is drawn after the
game image and before DKR-R's settings and performance overlays, so controls and
telemetry remain readable. Scaling can stretch one mask over the viewport or
tile it at native pixel size. Filter Density controls compositing strength and
can be changed while the game is running.

Use **Import Custom CRT Filter** to copy a PNG (maximum 64 MB) into the local
`filters` settings directory. DKR-R decodes and uploads a filter on first use,
then retains the resource so switching does not destroy a texture referenced by
an in-flight frame.

## Native RT64 and Rice texture packs

Use **Graphics > Custom Texture Packs > Import Texture Pack** with a `.zip` or
`.rtz` archive. DKR-R validates its format and lists it with a compatibility
result. Native RT64 archives are retained as archives. Rice archives are
converted transactionally into a managed directory; a failed conversion is
removed before it can be enabled.

A native pack must contain a parseable RT64 `rt64.json` database, either at the
archive root or within one containing directory. Referenced replacements use
formats supported by the pinned RT64 loader. Native and converted Rice packs
can be enabled and disabled independently.

Live changes are queued to RT64's presentation thread. RT64 stops its texture
stream workers, clears the previous replacement mapping, installs the validated
set, preloads required entries, and re-resolves resident textures. If a new set
is rejected, DKR-R attempts to restore the previous set and does not retry a
failed transaction every frame.

## Rice compatibility bridge

Rice PNG names such as `Game#CRC#format#size_rgb.png`, `_a.png`, and
`_all.png` are supported directly. During import DKR-R:

- validates every archive path and Rice identity;
- groups variants by the full identity, including optional palette hashes;
- combines `_rgb` with the red channel of `_a` as alpha, matching Rice's
  channel convention;
- makes unpaired `_rgb` images explicitly opaque and prefers complete `_all`
  images when supplied;
- generates a collision-checked RT64 database and stable 64-bit native aliases;
  and
- reports source-image, identity, and merged-pair coverage in the Graphics UI.

The RT64 patch pipeline computes the same Rice identity from DKR's live texture
load operation. It selects the alias only when an enabled pack contains that
replacement, so Accurate mode and native RT64 hashes remain unchanged when no
Rice pack is active. Rice identity calculation is cached by native texture
identity while the enabled-replacement check remains live for hot-swapping.

The supplied `diddy_kong_racing_pm_.zip` validation archive passes with 475
source PNGs, 328 unique replacement identities, and 131 reconstructed
RGB/alpha pairs. All 328 generated RT64 aliases and Rice identities are unique,
and every merged output was verified pixel-for-pixel. The archive is not
bundled with DKR-R.

## Other Project64-era formats

Jabo packs use `pack.xml` and opaque plugin-specific hashes. DKR-R detects and
retains them for inspection but does not guess a replacement mapping. A wrong
database can replace unrelated textures or cause intermittent corruption.
Convert and verify those assets with RT64's texture tooling so the result
includes `rt64.json`, then import that native archive.

## Storage and release safety

- Windows: `%APPDATA%\DKRPort\texture-packs`
- Linux: `$XDG_CONFIG_HOME/dkr-port/texture-packs` or
  `~/.config/dkr-port/texture-packs`
- Portable Windows: `dkr-runtime-data\texture-packs` beside the executable

Imported filters and packs remain user data and are never copied into a DKR-R
release. Rice conversion writes only normalized generated filenames into a
temporary directory beneath `texture-packs`. Absolute paths, parent traversal,
duplicate variants, unsafe image dimensions, alias collisions, archives over
4 GB, unreadable ZIPs, and malformed native databases are rejected before
renderer activation.
