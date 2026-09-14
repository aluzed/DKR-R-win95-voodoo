# Custom tracks

Status: **in progress.** The asset-table layer is implemented; the Patch
Pipeline hooks and the UI are not yet wired.

## Why this needs no instruction patching

DKR resolves every level through one data-driven indirection
(`level_global_init` and `load_level_game` in the matching decomp):

```c
gTempAssetTable = (s32 *) asset_table_load(ASSET_LEVEL_HEADERS_TABLE);
for (i = 0; gTempAssetTable[i] != -1; i++) {}
i--;
if (levelId >= i) { /* out of range -> fall back to the hub */ }
offset = gTempAssetTable[levelId];
size   = gTempAssetTable[levelId + 1] - offset;
asset_load(ASSET_LEVEL_HEADERS, (u32) gCurrentLevelHeader, offset, size);
```

Three retail behaviours follow from that, and all three work in our favour:

- The level count is counted to the `-1` terminator, not compiled in.
- The range check derives its bound from the same count, so a longer table
  widens the accepted `levelId` range automatically.
- `gNumberOfWorlds` is a running maximum of each header's `world` field, so a
  header declaring a world beyond the retail maximum creates that world.

Publishing a longer table is therefore sufficient to add both tracks and
worlds. No bounds check is patched and no retail instruction is replaced.

## The offset rule

`size` comes from the difference between consecutive entries, so custom
offsets cannot be arbitrary tokens. Overwriting the retail end offset would
corrupt the size of the **last retail entry**.

Custom offsets therefore begin exactly *at* the retail end offset and advance
by exact entry sizes, so a payload behaves as though appended to the section:

```text
retail : [o0, o1, ..., o(n-1), oEnd, -1]                    -> n entries
result : [o0, o1, ..., o(n-1), oEnd, oEnd+s0, ..., E, -1]   -> n+K entries
```

Retail arithmetic is untouched, custom entry `j` lands at level id `n + j`,
and `offset >= oEnd` is the discriminator that routes a load to a mod payload
instead of the ROM.

## The `.dkrmap` format

A track is a directory named `*.dkrmap` under `custom-tracks/`, or a zip archive of the
same layout that the importer unpacks. The directory form is what an author
edits in place, so a reload picks up an editor's save without a repack.

```text
ancient-lake-remix.dkrmap/
  manifest.json
  header.bin
  objects.bin
  name.bin
  model.bin
  textures/
    0.bin
    1.bin
```

```json
{
  "schemaVersion": 1,
  "id": "ancient-lake-remix",
  "name": "Ancient Lake Remix",
  "author": "example",
  "adds": [
    { "section": "LEVEL_HEADERS",     "file": "header.bin" },
    { "section": "LEVEL_OBJECT_MAPS", "file": "objects.bin" },
    { "section": "LEVEL_NAMES",       "file": "name.bin" },
    { "section": "LEVEL_MODELS",      "file": "model.bin" },
    { "section": "TEXTURES_3D",       "file": "textures/0.bin" },
    { "section": "TEXTURES_3D",       "file": "textures/1.bin" }
  ]
}
```

`TEXTURES_3D` is the one section a track adds **many** entries to, and their
order in `adds` is their identity - see "A track's own artwork" below.

Payload paths are confined to the track directory: absolute paths and `..`
are rejected, so a manifest can never name an arbitrary file on the machine.

Section payloads are produced by the matching decomp's asset tool
(`tools/dkr_assets_tool_src`), which is what the `Hint((...))` annotations in
`include/level_object_entries.h` exist to drive. DKR-R never parses a level
format; it serves bytes.

## Hub doors

A hub gate is two separate objects in a level's object map, not one:

| Object | Field | Meaning |
|---|---|---|
| `LevelObjectEntry_Exit` | `destinationMapId` | target level id (`ASSET_LEVEL_HEADERS` index) |
| | `overworldSpawnIndex` | where the player appears in the overworld |
| | `returnSpawnIndex` | where the player returns to in the hub |
| | `radius` | activation radius |
| `LevelObjectEntry_Door` | `balloonCount` / `localBalloons` | balloons required to open |
| | `keyID` | key requirement |
| | `modelIndex`, `textID`, `scale` | appearance |

`destinationMapId` is a `u8`. Retail uses 34 level ids, so roughly 220 remain.

## Progression

The retail EEPROM is 512 bytes, bit-packed with per-slot checksums, and fixed
at `kWorldCount = 6` / `kCourseCount = 34` (`dkr_save_codec.hpp`). A seventh
world does not fit, and it is not a matter of effort - there are no spare
bytes.

Custom progression therefore belongs in a DKR-R-owned sidecar beside the
virtual EEPROM, never inside the retail image. This also means installing a
custom world can never corrupt a real Adventure save.

## Hook design

Two interceptions are needed, and both follow the pattern already used by
`rev_a_asset_mutex.cpp`: an `instructionPatches` entry blanks the retail call,
and a `functionHooks` entry at the same VRAM performs the work natively,
delegating to the retail routine whenever the fast path does not apply.

### 1. Publish the extended table

`asset_table_load(assetIndex)` returns a freshly allocated copy of one asset
table. The hook runs at its epilogue, and when the request was
`ASSET_LEVEL_HEADERS_TABLE` it allocates a longer buffer through
`mempool_alloc_safe`, writes `build_extended_table()` into it, and replaces the
return value in `context->r2`.

The retail allocation is left to the memory pool. Only two call sites exist,
both once per level load, so the discarded buffer is not a live leak.

### 2. Route custom offsets away from the ROM

`asset_load(assetIndex, address, assetOffset, size)` DMAs from the cartridge.
A custom offset is deliberately beyond the section, so the DMA must never run:
the hook copies `payload_for()` into `address` and reports success instead.
When `payload_for()` returns null the retail loader is invoked unchanged.

Hooking the two call sites in `level_global_init` and `load_level_game` is
preferred over hooking inside `asset_load`, because a hook cannot skip a
function body but a blanked `jal` can be replaced wholesale.

### Symbols

```text
level_global_init   = 0x8006A6B0     asset_table_load   = 0x80076C58
asset_load          = 0x80076E68     mempool_alloc_safe = 0x80070C9C
gTempAssetTable     = 0x80121160     gNumberOfLevelHeaders = 0x80121170
```

The two `jal asset_load` instruction addresses still have to be read from the
generated disassembly; every other address above comes from the matching
decomp symbol file. The v1.1 policy is produced by
`scripts/generate_revision_policy.py` and is never hand-authored.

## Verified end to end

A smoke test installed one track whose payload is a byte copy of Ancient
Lake's retail level header, taken straight out of the ROM's asset sections.
Running the built `DKR-R.exe` against the US v1.0 ROM produced:

```text
[custom-tracks] loaded Ancient Lake Clone by smoke test
[custom-tracks] section 0: 65 retail + 1 added
```

The count matches the ROM independently: `ASSET_LEVEL_HEADERS_TABLE` holds 68
entries, terminated at index 66, which is the 65 levels `level_global_init`
counts, and the decomp extracts exactly 65 level header files. The second line
repeats once per `asset_table_load` call, which is once at level table init and
again for each level load.

What this does *not* yet prove: nothing navigates to the added index. The table
grows, but a menu entry or a hub door still has to point at it.

## Sections a track can replace or extend

The hooks resolve an asset section index to a payload slot, so all four level
aspects work through one mechanism (`AssetSectionsEnum` in the decomp's
`include/asset_enums.h`):

| Table / data | Section | Manifest name |
|---|---|---|
| 3 / 2 | 3D textures | `TEXTURES_3D` |
| 20 / 21 | object maps | `LEVEL_OBJECT_MAPS` |
| 22 / 23 | headers | `LEVEL_HEADERS` |
| 24 / 25 | names | `LEVEL_NAMES` |
| 26 / 27 | models | `LEVEL_MODELS` |

The texture pair is the same numbering read from the other end of the enum, and
note its order is data-then-table, the reverse of the four level pairs.

## A track's own artwork

A `.dkrmap` can add textures the ROM does not hold, and the mechanism is the
one above rather than a new one. `textures_sprites.c` reaches the 3D texture
list exactly the way `level_global_init` reaches the level list:

```c
gTextureAssetTable[TEX_TABLE_3D] = asset_table_load(ASSET_TEXTURES_3D_TABLE);
for (i = 0; table[i] != -1; i++) {}          // count, then i--
if (assetIndex >= gTextureTableSize[..]) { } // range check, from the table
assetOffset = table[assetIndex];
assetSize   = table[assetIndex + 1] - assetOffset;   // size BY DIFFERENCE
asset_load(ASSET_TEXTURES_3D, dest, assetOffset, assetSize);
```

So publishing a longer table grows the texture count and the range check
together, and an appended payload loads. Nothing else changes.

### What a texture payload is

The bytes `dkr_assets_tool`'s `BuildTexture::build` writes for an uncompressed
texture: a 32-byte `TextureHeader` and then the image, row-major and
unswizzled. The Blender addon writes them itself, so no C++ toolchain stands
between an author and a track; `tools/blender/tests/test_custom_textures.py`
holds every field and every texel conversion to the decomp's own.

Two limits are the hardware's, not the format's, and both are refused at import
rather than discovered as a corrupt road in game:

- The RDP has **4 KiB of texture memory** and `material_init` loads a level
  texture as one block, so a 16-bit format stops at 2048 texels - **64x32**,
  not 64x64. The eight-bit formats reach 64x64 and the four-bit ones are capped
  by the wrap limit instead.
- `material_init`'s mask loop only walks the powers of two **up to 64**, so a
  side larger than that gets `G_TX_CLAMP` and `G_TX_NOMASK` however the flags
  are set: the texture stretches once across each face instead of tiling.

Colour-indexed formats are excluded. Their palettes are loaded from
`ASSET_EMPTY_14` by a byte offset into that section, and a track cannot add
one.

The reduction those limits force is undone outside the track, not inside it.
The Blender addon's export also writes `<track>-hd.zip` beside the package: a
Rice texture pack of the author's originals, each named by the identity RT64
computes for the payload the package ships. The track neither needs nor
references it, and without it draws exactly what it always did. See
`docs/TEXTURE_PACKS.md`.

A payload is also refused if it is shorter than 40 bytes or not a multiple of
16. `load_texture` reads `sizeof(TempTexHeader)` - 40 bytes - before it knows
how large the texture is, and it puts the display list it builds at
`align16(tex + assetSize)` inside an allocation of exactly that size plus the
lists.

### The id a level model stores, and why it is a placeholder

A `TextureInfo` in a level model stores an index into the global list, and
`tracks.c` resolves it with `load_texture(id | 0x8000)`. A shipped texture's
index is **the ROM's retail texture count plus its ordinal**, and that count is a
property of the cartridge: 1401 in the US v1.0 extraction and 1416 in Rev A,
both counted from the asset tables. So the exporter cannot know it. It writes
`0x7000 + ordinal` instead, and DKR-R substitutes the real index as the model
is served, exactly as it already patches a header's model and object-map
fields.

**Order in the manifest is the ordinal, and therefore the identity.**
Reordering the `TEXTURES_3D` entries repaints the track with no error anywhere.

An id that cannot be resolved - a track added by a rescan after boot, or a
model naming more textures than its package ships - is rewritten to texture 0
rather than left alone. Leaving it would send `load_texture` past the end of the
table: it range-checks such an index, sets `id = 0`, and then indexes with the
unclamped value anyway.

### How the id is reachable inside a compressed model

A level model arrives compressed - `track_init_level_model` does `asset_load`
and then `gzip_inflate` - and a four-byte field inside a Huffman-coded DEFLATE
block has no byte offset to patch.

The answer is DEFLATE's own. Block type `00` is *stored*: byte-aligned and
verbatim, and `gzip_inflate_block` dispatches to `gzip_inflate_stored` for it
exactly as it does to the Huffman decoders for the other two. So the exporter
writes the model's header and texture table as one stored block and compresses
the rest. The stream stays a legal DEFLATE stream, the game inflates it with the
code it always used, and the ids sit at a fixed offset:

```text
0..4    container: uncompressed size (LE u32), then the tag 0x09
5       the stored block's BFINAL/BTYPE byte, whose remaining five bits the
        reader discards - which is what "stored is byte-aligned" means
6..7    LEN, little endian        8..9  NLEN, LEN's complement
10..    the model's own first LEN bytes, uncompressed
```

The prefix is about two kilobytes at the largest possible texture table,
against models of a hundred to five hundred. Every packed model pays it,
including remixes with no artwork of their own, because one format that is
always patchable is worth more than two that differ in a way nothing downstream
can see.

### The texture table is published once

`tex_init_textures` runs once at boot, from `thread3_main`. The level tables are
rebuilt at every level load, so a rescan renumbers them; this one cannot be
renumbered afterwards. A track installed mid-session therefore has no textures
in the published table at all, and its model's ids are reset to texture 0.
DKR-R has to be relaunched after installing a track that ships artwork - HD
pack or the 64x32 in the `.dkrmap`, it makes no difference.

This is the one step no amount of UI removes. Track Lab makes it *one* click,
not zero: when a track ships an HD pack that the current session cannot load
yet, its row shows **HD textures: restart to load** and a single **Restart &
play in HD** button arms the track, switches to Modern, turns on auto-boot, and
relaunches straight into it. `custom_tracks::track_textures_published()` is what
tells the UI which state the track is in. Every launch after that is zero
clicks: the armed track and auto-boot persist in `custom-tracks-state.txt`.

### Making one

```sh
blender --background --factory-startup \
    --python tools/blender/make_texture_demo_track.py -- \
    --image path/to/picture.jpg --out build/my-track.dkrmap
```

That script drives the same operators the sidebar does, in the same order, and
prints the package back from its own bytes. In Blender, the Textures panel's
"This track's own artwork" section is the same four steps by hand.

## A level has two object maps, and they must stay separate

`init_track` spawns from both, with different roles:

```c
init_track(geometry, skybox, players, vehicle, entrance,
           header->collectables,   // 0x36 -> track_spawn_objects(.., 1)
           header->unkBA);         // 0xBA -> track_spawn_objects(.., 0)
```

Ancient Lake splits as:

| Header field | Map | Contents |
|---|---|---|
| `0xBA` | 5 | 98 objects: checkpoints, spawn points, cameras, scenery |
| `0x36` | 73 | 86 objects: coins, weapon balloons, fish |

A manifest entry for `LEVEL_OBJECT_MAPS` therefore must carry a `slot`:

```json
{ "section": "LEVEL_OBJECT_MAPS", "slot": "structure",    "file": "objects_structure.bin" },
{ "section": "LEVEL_OBJECT_MAPS", "slot": "collectables", "file": "objects_collectables.bin" }
```

An entry without one is refused rather than guessed at. Exporting the two maps
merged into a single payload is the failure this guards: the 184 combined
objects would all spawn with the collectables flag, checkpoints and spawn
points included, while the retail structure map kept spawning beside them.

## The header's object map is patched, not authored

`LevelHeader` names its object map by index:

```c
/* 0x34 */ s16 geometry;      // ASSET_LEVEL_MODELS
/* 0x36 */ s16 collectables;  // ASSET_LEVEL_OBJECT_MAPS
```

Those indices do not exist until the extended tables are built, so an author
cannot write them. The runtime rewrites them as the header is served, pointing
each at what the same track supplied (`sibling_index`):

| Offset | Field | Patched when the track ships |
|---|---|---|
| `0x34` | `geometry` | a `LEVEL_MODELS` payload |
| `0x36` | `collectables` | `LEVEL_OBJECT_MAPS` slot `collectables` |
| `0xBA` | `unkBA` | `LEVEL_OBJECT_MAPS` slot `structure` |

**A track must not try to compute these**: whatever they contain is
overwritten. Everything else in the header, `skybox` included, is the author's
to write. A Phase 1 remix ships no model, so its authored `geometry` survives
and keeps pointing at the retail track it was built on.

Known ordering caveat: `objects.c` loads the object-map table *after* the
header. If a header is served before that table has been built even once,
`sibling_index` returns -1 and the field is left as authored rather than
filled with a guess.

## Installing

Track Lab's **IMPORT A COPY** button takes a folder through the system picker.
Point it at the `.dkrmap`, at the track's own folder, or at the folder that
holds both the `.dkrmap` and its `<track>-hd.zip`; a `.zip` of the `.dkrmap`
(optionally wrapping the pack too) also works. The manifest is validated before
anything is copied.

If the track declares an `hdTexturePack` and the matching `<track>-hd.zip` is
found beside it, DKR-R imports that pack in the same gesture -
`texture_packs::import_archive` with a `TrackPackOwner`, so the pack is born
enabled, filed against the track (`Origin::TrackPack`), and kept out of the
texture-pack browser's default list. A pack whose stamped `textureDigest` does
not match the manifest's is left out with a note; the track still plays, in
64x32.

Track Lab draws in the Accurate profile as well now. The list, the import and
arming all work there; what stays impossible in Accurate is a custom track
actually *loading*, because importing, arming or playing one switches the
profile to Modern first (with a note saying so). Copying a folder into
`custom-tracks/` by hand still works.

## Two traps worth knowing

**Policy edits need a recompile, not just a rebuild.** Patch Pipeline hooks are
injected while N64Recomp translates the ELF. Adding hooks to
`dkr.us.v77.recomp-policy.json` and rebuilding does nothing until N64Recomp
runs again for that revision - the generated `RecompiledFuncs` still carry the
old code. The first smoke test failed exactly this way: v80 had been
regenerated after the policy edit and v77 had not, and the run used v77.

**`mods/` belongs to librecomp.** N64ModernRuntime ships its own mod system,
it is live in DKR-R, and it scans `mods/` for its `.nrm` format. A `.dkrmap`
placed there is reported as `Mod is missing a mod.json` to the user. Custom
tracks therefore live in `custom-tracks/` beside it.

**Regenerating the v80 policy loses v80-only entries.**
`scripts/generate_revision_policy.py` translates the v1.0 policy forward, so
anything that exists only in the v1.1 policy - the fourteen Rev A asset mutex
hooks and their instruction patches - is dropped. Merge them back after
regenerating, or Rev A silently loses its uncontended DMA fast path.

## Remaining work

1. **Removing an installed track.** Track Lab can enable/disable but not delete.
   When it can, it has to call `texture_packs::forget_track_pack(track_id)`
   alongside, or a track's HD pack is left enabled and ownerless. The runtime
   side of that call already exists.
2. **Detection also at arm time.** The HD pack sibling is resolved when a track
   is installed or rescanned, not when it is armed, so a track and its pack
   arriving separately (track first, pack later) is not picked up until the
   next rescan. Cheap to add.
3. **Online and Accurate policy.** Map identity is already part of the online
   contract (`dkr_netplay_gameplay_level_begin` seeds from the selected map),
   so a custom track must either enter the session handshake or be refused
   online. Custom tracks belong to Modern, as texture packs do.
