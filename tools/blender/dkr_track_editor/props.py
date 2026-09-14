"""Scene-level settings for the track editor."""

from __future__ import annotations

import bpy
from bpy.props import (
    BoolProperty, CollectionProperty, EnumProperty, FloatProperty, IntProperty,
    PointerProperty, StringProperty,
)

from . import catalog as catalog_module, level_types

#: Blender hands an enum callback's strings to C without taking a reference, so
#: a list built fresh each call can be collected while the menu still points at
#: it - the symptom being a picker that opens empty. Holding the last list
#: returned for each key is what keeps them alive.
_ITEMS = {}


def _keep(key, items):
    _ITEMS[key] = items
    return items


def category_items(self, context):
    """Categories with something usable in the chosen tab and level type.

    Counted the way the list will show them, so a category never opens onto
    nothing. ``ALL`` stays first whatever else changes: the tab and the level
    type both reset the choice to it, and a dynamic enum is stored by position.
    """
    try:
        catalog = catalog_module.load()
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        return _keep("category", [("ALL", "All", "Every object type")])
    counts = level_types.category_counts(
        catalog, level_types.current_key(self), self.slot,
        show_all=self.show_incompatible,
    )
    items = [("ALL", "All (%d)" % sum(counts.values()),
              "Every type in this tab")]
    for category in catalog.categories:
        if counts.get(category):
            items.append((category, "%s (%d)" % (category.title(), counts[category]),
                          "%d types" % counts[category]))
    return _keep("category", items)


def _reset_category(self, context):
    """A new tab or level type has a different set of categories."""
    self.category = "ALL"


def _level_items():
    items = [(level_types.NONE, "Not Chosen", "No level type yet", 0)]
    for number, key in enumerate(level_types.FAMILIES, start=1):
        family = level_types.FAMILIES[key]
        items.append((key, family.label, family.tooltip, number))
    return items


def _sub_items(family):
    return [(key, text, help_text, number)
            for number, (key, text, help_text)
            in enumerate(level_types.SUBTYPES[family])]


def texture_group_items(self, context):
    """The folders the ROM's 3D textures are extracted into.

    Imported here rather than at module scope because resolving them reaches for
    the asset tree, and this module is imported while the addon is still being
    registered.
    """
    items = [("ALL", "All", "Every texture in the ROM")]
    try:
        from . import prefs, textures as texture_catalogue  # noqa: PLC0415

        entries = texture_catalogue.catalogue(prefs.resolve(context))
        for group in texture_catalogue.groups(entries):
            count = sum(1 for entry in entries if entry.group == group)
            items.append((group, group.title(), "%d textures" % count))
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        pass
    return _keep("texture_group", items)


def texture_surface_items(self, context):
    """What the ground made of a newly applied texture behaves like.

    The same list the Set Surface Type operator offers, from the catalogue's
    ``SurfaceType``, because it is the same choice - made when the texture is
    applied rather than afterwards, since the surface type is part of what
    decides whether an entry can be reused or a new one is needed.
    """
    try:
        from .operators import geometry as geometry_ops  # noqa: PLC0415

        found = [(str(value), name, help_text)
                 for value, name, help_text in geometry_ops.surface_items()]
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        found = []
    return _keep("texture_surface", found or [("0", "Road", "")])


def texture_format_items(self, context):
    """The formats a texture a track brings with it may be written in.

    Not the whole of ``FORMAT_CODES``: the two colour-indexed ones need a
    palette out of ``ASSET_EMPTY_14``, and a track cannot add one of those.
    """
    from . import textures as texture_module  # noqa: PLC0415

    described = {
        "RGBA16": "Colour, one bit of alpha. What 1034 of the ROM's own use",
        "RGBA32": "Full colour and alpha. Four times the memory, so 32x32 at most",
        "IA16": "Greyscale with a full alpha channel",
        "IA8": "Greyscale with alpha, four bits each",
        "IA4": "Greyscale with one bit of alpha, four bits a texel",
        "I8": "Greyscale. Reaches 64x64, where colour stops at 64x32",
        "I4": "Greyscale, four bits a texel. The smallest",
    }
    items = []
    for name in texture_module.CUSTOM_FORMATS:
        code = texture_module.FORMAT_CODES[name]
        best = texture_module.largest_size(code) or (0, 0)
        items.append((
            str(code), name,
            "%s. Up to %dx%d" % (described.get(name, name), best[0], best[1]),
        ))
    return _keep("texture_format", items)


class DKR_CustomTexture(bpy.types.PropertyGroup):
    """One image the track ships itself, as the scene remembers it.

    The PNG is the record, not the file the author picked: it has already been
    resampled to a size the RDP can load and written where a re-export can find
    it, so the package can be rebuilt from a ``.blend`` alone. ``source`` is
    kept only so the panel can say where the picture came from.
    """

    name: StringProperty(name="Name", default="Texture")
    source: StringProperty(name="From", default="")
    #: Relative to the directory the ``.blend`` is in, so the scene and its
    #: pictures move together. Plain text rather than a ``FILE_PATH``: Blender's
    #: ``//`` prefix is what a path property understands, and only from 4.5
    #: onwards and only when the property opts in - a warning on every assign
    #: for anyone on 4.2, which is the version this addon says it needs.
    #: :func:`..operators.custom_textures.resolve` is the other half.
    png: StringProperty(name="Image", default="")
    #: The same picture at full resolution, as a PNG, stored the way ``png`` is.
    #: The track never reads it; the high-resolution texture pack the export
    #: writes beside the ``.dkrmap`` does. Empty for a texture added before the
    #: pack existed, which the export then rebuilds from ``source`` if it can.
    original: StringProperty(name="Original", default="")
    width: IntProperty(default=0)
    height: IntProperty(default=0)
    #: A ``FORMAT_CODES`` value, stored as the number the file stores.
    format: IntProperty(default=1)
    render_mode: StringProperty(default="OPAQUE")
    #: Which invisible bit the export flips to tell this texture apart from
    #: another that reduced to the same pixels; 0 for none. See
    #: :func:`..textures.nudge_texels`.
    nudge: IntProperty(default=0)


class DKR_ValidationEntry(bpy.types.PropertyGroup):
    severity: StringProperty(default="info")
    message: StringProperty(default="")
    object_id: StringProperty(default="")
    #: Comma-separated document positions, so the result can select what it is
    #: about rather than leaving the author to hunt for it.
    objects: StringProperty(default="")


class DKR_SceneSettings(bpy.types.PropertyGroup):
    """Everything the sidebar needs to remember between clicks."""

    source_path: StringProperty(
        name="Source",
        description="The object map this scene was imported from",
        default="",
        subtype="FILE_PATH",
    )

    asset_root: StringProperty(
        name="Asset Tree",
        description=(
            "Extracted decomp asset version directory the artwork is read from"
        ),
        default="",
        subtype="DIR_PATH",
    )

    geometry_path: StringProperty(
        name="Geometry",
        description="The level model loaded as reference geometry",
        default="",
        subtype="FILE_PATH",
    )

    # -- the Level Type ----------------------------------------------------
    #
    # Written only by dkr.set_level_type and the importers, so a change always
    # passes through the confirmation that says what it affects. The three
    # sub-selectors are separate static enums rather than one that changes
    # meaning with the family, because Blender stores an enum by position.

    level_type: EnumProperty(
        name="Level Type",
        description=(
            "What kind of level this is. Everything in the addon follows it: "
            "what can be placed, the start grid, validation and the header"
        ),
        items=_level_items(),
        default=level_types.NONE,
        update=_reset_category,
    )
    challenge_type: EnumProperty(
        name="Challenge",
        description=(
            "The game treats these three differently (race types 64, 65, 66), "
            "and some objects only work in one of them"
        ),
        items=_sub_items(level_types.CHALLENGE),
        default=level_types.BATTLE,
        update=_reset_category,
    )
    special_type: EnumProperty(
        name="Kind",
        description="The level types the game uses for scenes, menus and testing",
        items=_sub_items(level_types.SPECIAL),
        default=level_types.CUTSCENE,
        update=_reset_category,
    )
    boss: EnumProperty(
        name="Boss",
        description=(
            "Which boss the player races. Written to the header as "
            "/boss-race-id (byte 0xB8), and the race is run in its vehicle"
        ),
        items=[
            (boss_id, text, "%s. The retail race uses the %s"
             % (boss_id, level_types.vehicle_name(vehicle).lower()), number)
            for number, (boss_id, text, vehicle) in enumerate(level_types.BOSSES)
        ],
        default="BOSS_RACE_TRICKY1",
    )
    vehicles: EnumProperty(
        name="Vehicles",
        description=(
            "The vehicles the track allows. The player picks from them, and "
            "the bots race in the player's vehicle"
        ),
        items=[(vehicle, name, "", 1 << number)
               for number, (vehicle, name) in enumerate(level_types.PLAYER_VEHICLES)],
        options={"ENUM_FLAG"},
        default={"VEHICLE_CAR"},
    )
    default_vehicle: EnumProperty(
        name="Default Vehicle",
        description="The vehicle selected when the player enters the track",
        items=[(vehicle, level_types.vehicle_name(vehicle), "", number)
               for number, (vehicle, _name) in enumerate(level_types.PLAYER_VEHICLES)],
        default="VEHICLE_CAR",
    )
    laps: IntProperty(
        name="Laps",
        description="How many laps the race has, from 1 to 9",
        default=3, min=1, max=9,
    )
    vehicle_override: EnumProperty(
        name="Vehicle Override",
        description=(
            "These are not player vehicles: the bots never use them, and no "
            "retail track lists them. For debugging only. Replaces the default "
            "vehicle chosen in Level Type"
        ),
        items=[("NONE", "None", "Use the vehicles chosen in Level Type", 0)]
        + [(vehicle, vehicle, "", number)
           for number, vehicle in enumerate(level_types.DEBUG_VEHICLES, start=1)],
        default="NONE",
    )
    show_special: BoolProperty(
        name="Special (advanced)",
        description=(
            "The level types the game uses for scenes, menus and testing. "
            "Not normal game modes"
        ),
        default=False,
    )
    show_incompatible: BoolProperty(
        name="Show Incompatible Types",
        description=(
            "Also list the types this level type does not use, marked with a "
            "warning. They can still be placed; validation warns about them"
        ),
        default=False,
        update=_reset_category,
    )

    category: EnumProperty(
        name="Category",
        description=(
            "Narrow the list. Only categories with something usable in this "
            "tab and level type are offered"
        ),
        items=category_items,
    )

    object_type: StringProperty(
        name="Object Type",
        description="The type the Place button will add",
        default="ASSET_OBJECT_GROUNDZIPPER",
    )

    track_name: StringProperty(
        name="Track Name",
        description="Shown in game and in the mod list",
        default="",
    )
    track_id: StringProperty(
        name="Track Id",
        description=(
            "Directory name and manifest id: lowercase words joined by hyphens. "
            "Left blank, it is derived from the track name"
        ),
        default="",
    )
    track_author: StringProperty(name="Author", default="")

    slot: EnumProperty(
        name="Object Map",
        description=(
            "Which of the level's two object maps a newly placed object joins, "
            "and which types the Place list shows. The game loads both maps "
            "the same way, so the split is kept per object rather than inferred"
        ),
        items=[
            ("structure", "Structure",
             "The track itself: start positions, checkpoints, zippers, doors, "
             "scenery. What you place from this tab goes into the structure "
             "object map"),
            ("collectables", "Collectables",
             "Pickups: bananas, coins, weapon balloons, keys. What you place "
             "from this tab goes into the collectables object map"),
        ],
        default="structure",
        update=_reset_category,
    )

    show_raw: BoolProperty(
        name="Show Raw Bytes",
        description=(
            "Show the pad and unk fields. They exist so an entry encodes to the "
            "bytes the game expects, and nobody has identified what the unk ones "
            "do - a checkpoint has 15 of them around the 4 that matter"
        ),
        default=False,
    )

    # -- the texture browser ---------------------------------------------
    #
    # A track can draw with any texture the ROM holds, not only the ones its
    # base model shipped with, so what is remembered here is a choice out of
    # 1401 rather than out of a table.

    texture_id: IntProperty(
        name="Texture",
        description=(
            "Index into the ROM's 3D texture list - what a level model's "
            "texture table actually stores. -1 means nothing is chosen"
        ),
        default=-1,
    )

    texture_query: StringProperty(
        name="Search",
        description=(
            "Narrow the textures by name. Every word has to appear, so "
            "\"ice wall\" finds the icy walls and not every wall"
        ),
        default="",
        options={"TEXTEDIT_UPDATE"},
    )

    texture_group: EnumProperty(
        name="Set",
        description="Which of the extraction's texture folders to browse",
        items=texture_group_items,
    )

    texture_surface: EnumProperty(
        name="Surface",
        description=(
            "What the ground made of this texture behaves like. It is stored on "
            "the texture table entry, so applying the same picture with two "
            "surface types makes two entries - which is how the game gets one "
            "image that is road in one place and grass in another"
        ),
        items=texture_surface_items,
    )

    texture_mapping: EnumProperty(
        name="Mapping",
        description="What to do with the UVs of the faces being retextured",
        items=[
            ("KEEP", "Keep The Mapping",
             "Leave the picture covering the same ground as the one it "
             "replaces, rescaled for the new texture's size. A face that had no "
             "texture has no mapping to keep, so project those instead"),
            ("PROJECT", "Project Flat",
             "Plant the texture on the world along whichever axis each face "
             "most faces. This is what new geometry needs: a face extruded out "
             "of the track inherits UVs that are well-formed and mean nothing"),
        ],
        default="KEEP",
    )

    texture_scale: FloatProperty(
        name="Units Per Repeat",
        description=(
            "How much ground one repeat of the texture covers when projecting. "
            "Retail's median is 268 map units, measured over 257,035 textured "
            "triangle edges"
        ),
        default=256.0,
        min=1.0,
        soft_max=2048.0,
    )

    # -- the track's own artwork -----------------------------------------
    #
    # Position in this collection is the texture's identity: the runtime hands
    # out ids by position within a track's manifest, and the level model refers
    # to them by the same position. Reordering it repaints the track, which is
    # why nothing here offers to sort or move an entry.

    custom_textures: CollectionProperty(type=DKR_CustomTexture)

    custom_format: EnumProperty(
        name="Format",
        description=(
            "How the image is stored. The choice sets the largest size it can "
            "be: the RDP has 4KB of texture memory and a level texture is "
            "loaded into it as one block"
        ),
        items=texture_format_items,
    )

    custom_size: StringProperty(
        name="Size",
        description=(
            "Width and height to resample to, as WxH. Both have to be powers "
            "of two no larger than 64 - material_init clamps anything else "
            "instead of tiling it. Blank picks the largest the format allows"
        ),
        default="",
    )

    has_validated: BoolProperty(default=False)
    results: CollectionProperty(type=DKR_ValidationEntry)


CLASSES = (
    DKR_CustomTexture,
    DKR_ValidationEntry,
    DKR_SceneSettings,
)


def register_pointers():
    bpy.types.Scene.dkr = PointerProperty(type=DKR_SceneSettings)


def unregister_pointers():
    if hasattr(bpy.types.Scene, "dkr"):
        del bpy.types.Scene.dkr
