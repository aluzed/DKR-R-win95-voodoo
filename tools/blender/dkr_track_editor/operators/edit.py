"""Placing objects and editing their fields."""

from __future__ import annotations

import bpy
from bpy.props import EnumProperty, StringProperty

from .. import catalog as catalog_module, level_types, prefs, scene
from ..gltf_io import MapObject


def _catalog():
    return catalog_module.load()


def _asset_tree(context):
    """Where to read artwork from, however the author configured it."""
    return prefs.resolve(context)


#: Types whose field must be unique among their peers, and what groups them.
#: A checkpoint index has to be unique within its (vehicleType, isAltCheckpoint)
#: chain - the rule holds across all 51 retail chains - and an AI node's id has
#: to be unique outright. A start position's racerIndex is unique within its
#: entrance, and a missing one starts that racer at the map origin.
UNIQUE_FIELDS = {
    "ASSET_OBJECT_CHECKPOINT": ("index", ("vehicleType", "isAltCheckpoint")),
    "ASSET_OBJECT_AINODE": ("nodeID", ()),
    "ASSET_OBJECT_SETUPPOINT": ("racerIndex", ("entranceID",)),
}


def _assign_free_index(context, object_id, fields):
    """Give a newly placed object an index nothing else is using.

    Without this the first two checkpoints an author places both take index 0
    and the track fails validation for a reason that has nothing to do with what
    they were trying to do.
    """
    spec = UNIQUE_FIELDS.get(object_id)
    if spec is None:
        return
    field, grouped_by = spec
    if field not in fields:
        return

    chain = tuple(fields.get(key) for key in grouped_by)
    taken = set()
    for obj in scene.iter_dkr_objects(context):
        if str(obj.get(scene.PROP_ID)) != object_id:
            continue
        if tuple(obj.get(key) for key in grouped_by) != chain:
            continue
        value = obj.get(field)
        if isinstance(value, int):
            taken.add(value)

    candidate = 0
    while candidate in taken:
        candidate += 1
    fields[field] = candidate


def object_type_items(self, context):
    """Every object type, grouped so the featured ones come first."""
    try:
        catalog = _catalog()
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        return [("NONE", "catalogue unavailable", "")]

    settings = context.scene.dkr if context and context.scene else None
    category = getattr(settings, "category", "ALL")

    types = list(catalog.types.values())
    if category and category not in ("ALL", ""):
        types = [t for t in types if t.category == category]
    types.sort(key=lambda t: (not t.featured, -t.retail_count, t.object_id))

    items = []
    for object_type in types:
        description = "%s | %s | %d in retail tracks" % (
            object_type.object_id, object_type.category, object_type.retail_count
        )
        label = object_type.label
        if object_type.featured:
            label += "  *"
        items.append((object_type.object_id, label, description))
    return _keep("object_type",
                 items or [("NONE", "no types in this category", "")])


class DKR_OT_place_object(bpy.types.Operator):
    """Add a DKR object at the 3D cursor"""

    bl_idname = "dkr.place_object"
    bl_label = "Place DKR Object"
    bl_options = {"REGISTER", "UNDO"}

    object_id: StringProperty(
        name="Object Type",
        description="ASSET_OBJECT_* identifier to place",
    )
    preset: StringProperty(
        name="Preset",
        description="A preset of the type, such as a weapon balloon's colour",
        default="", options={"SKIP_SAVE"},
    )

    @classmethod
    def description(cls, context, properties):
        """Each Place button says what it places, not what placing is."""
        try:
            object_type = _catalog().get(properties.object_id)
        except Exception:  # noqa: BLE001 - a tooltip must not raise
            object_type = None
        if object_type is None:
            return cls.__doc__
        return level_types.place_description(
            object_type, level_types.current_key(context.scene.dkr),
            level_types.preset(properties.preset) if properties.preset else None,
        )

    def execute(self, context):
        catalog = _catalog()
        object_id = self.object_id or context.scene.dkr.object_type
        object_type = catalog.get(object_id)
        if object_type is None:
            self.report({"ERROR"}, "unknown object type %r" % object_id)
            return {"CANCELLED"}

        fields = object_type.fresh_fields()
        chosen = level_types.preset(self.preset) if self.preset else None
        if chosen is not None and chosen.object_id == object_id:
            fields.update(chosen.fields)
        else:
            chosen = None
        _assign_free_index(context, object_id, fields)

        placed = MapObject(
            object_id=object_id,
            name=object_type.node_name,
            translation=scene.to_map(context.scene.cursor.location),
            fields=fields,
        )

        root = scene.ensure_root(context)
        empty = scene.create_empty(context, placed, catalog, root,
                                   _asset_tree(context),
                                   context.scene.dkr.slot)
        empty["dkr_order"] = scene.next_order(context)
        if chosen is not None:
            # A label for the outliner only; the node name the export writes
            # stays the type's own, in dkr_node_name.
            empty.name = chosen.label.replace(" ", "")

        for obj in context.selected_objects:
            obj.select_set(False)
        empty.select_set(True)
        context.view_layer.objects.active = empty

        shown = chosen.label if chosen else object_type.label
        key = level_types.current_key(context.scene.dkr)
        if empty.type == "EMPTY" and _asset_tree(context) is None:
            self.report(
                {"WARNING"},
                "placed %s as a marker; set the decomp asset path in the addon "
                "preferences to draw objects with their real artwork" % shown,
            )
        elif not level_types.visible(object_type, key):
            self.report({"WARNING"}, "placed %s, which a %s level does not use"
                        % (shown, level_types.label(key)))
        else:
            index = (" (racerIndex %d)" % fields["racerIndex"]
                     if "racerIndex" in fields else "")
            self.report({"INFO"}, "placed %s%s in the %s map"
                        % (shown, index, scene.slot_of(empty)))
        return {"FINISHED"}


#: Blender's enum callbacks hand their strings to C without taking a reference,
#: so a list built fresh on every call can be collected while the menu is still
#: using it - the documented symptom being a picker that opens empty. Holding the
#: last list returned for each key is what keeps them alive.
_ITEMS = {}


def _keep(key, items):
    """Hold a reference to what an enum callback returned, and return it."""
    _ITEMS[key] = items
    return items


class DKR_OT_set_enum_field(bpy.types.Operator):
    """Pick a value for an enum field from the full set the game defines"""

    bl_idname = "dkr.set_enum_field"
    bl_label = "Set Field"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    field: StringProperty(options={"HIDDEN"})

    def _items(self, context):
        obj = context.active_object
        if obj is None or scene.PROP_ID not in obj:
            return _keep("field", [("NONE", "no object selected", "")])
        catalog = _catalog()
        object_type = catalog.get(str(obj[scene.PROP_ID]))
        if object_type is None:
            return _keep("field", [("NONE", "type not in the catalogue", "")])
        field = object_type.field(self.field)
        if field is None:
            return _keep("field", [("NONE", "no such field", "")])
        members = catalog.enum_members(field)
        seen = set(field.values)
        return _keep("field", [
            (m, m, "used by retail tracks" if m in seen else "")
            for m in members
        ] or [("NONE", "this enum has no members", "")])

    value: EnumProperty(name="Value", items=_items)

    def invoke(self, context, event):
        # A dialog rather than a search popup: it draws the enum as an ordinary
        # dropdown, which types-to-filter the same way and does not depend on
        # the search UI reading the operator's other properties.
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        obj = context.active_object
        if obj is None or not self.field:
            return {"CANCELLED"}
        obj[self.field] = self.value
        # Nudge the UI so the new value shows without the author clicking away.
        obj.update_tag()
        for area in context.screen.areas:
            area.tag_redraw()
        return {"FINISHED"}


class DKR_OT_reset_field(bpy.types.Operator):
    """Put a field back to the value a freshly placed object would have"""

    bl_idname = "dkr.reset_field"
    bl_label = "Reset Field"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    field: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        obj = context.active_object
        if obj is None or scene.PROP_ID not in obj:
            return {"CANCELLED"}
        object_type = _catalog().get(str(obj[scene.PROP_ID]))
        field = object_type.field(self.field) if object_type else None
        if field is None:
            return {"CANCELLED"}
        obj[self.field] = field.fresh()
        for area in context.screen.areas:
            area.tag_redraw()
        return {"FINISHED"}


class DKR_OT_select_by_type(bpy.types.Operator):
    """Select every object of the active object's type"""

    bl_idname = "dkr.select_by_type"
    bl_label = "Select Same Type"
    bl_options = {"REGISTER", "UNDO"}

    object_id: StringProperty(options={"HIDDEN"})

    @classmethod
    def description(cls, context, properties):
        if not properties.object_id:
            return cls.__doc__
        try:
            object_type = _catalog().get(properties.object_id)
        except Exception:  # noqa: BLE001 - a tooltip must not raise
            object_type = None
        name = object_type.label if object_type else properties.object_id
        return "Select every %s in the scene" % name

    def execute(self, context):
        target = self.object_id
        if not target:
            active = context.active_object
            if active is None or scene.PROP_ID not in active:
                self.report({"ERROR"}, "no active DKR object")
                return {"CANCELLED"}
            target = str(active[scene.PROP_ID])

        count = 0
        for obj in scene.iter_dkr_objects(context):
            match = str(obj[scene.PROP_ID]) == target
            obj.select_set(match)
            count += int(match)
        self.report({"INFO"}, "selected %d %s" % (count, target))
        return {"FINISHED"}


class DKR_OT_refresh_artwork(bpy.types.Operator):
    """Redraw every placed object with its sprite or model"""

    bl_idname = "dkr.refresh_artwork"
    bl_label = "Refresh Object Artwork"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        from .. import gltf_io, preview

        prefs.invalidate()
        tree = prefs.resolve(context)
        if tree is None:
            self.report(
                {"ERROR"},
                "no extracted decomp assets found; set the path in the addon "
                "preferences",
            )
            return {"CANCELLED"}

        catalog = _catalog()
        root = scene.ensure_root(context)
        context.view_layer.update()

        # Everything each object is has to be read before any is removed: the
        # slot and the parent live on the object, and reading them afterwards
        # reads an empty scene and rebuilds nothing. A parent that is itself a
        # DKR object is about to be replaced, so only a grid root is kept.
        existing = sorted(scene.iter_dkr_objects(context),
                          key=lambda o: o.get("dkr_order", 1 << 30))
        records = []
        for obj in existing:
            parent = obj.parent
            if parent is not None and scene.is_dkr_object(parent):
                parent = None
            records.append((scene.read_object(obj, catalog), scene.slot_of(obj),
                            parent, obj.matrix_world.copy(), obj.get("dkr_order")))

        # An object's Blender type is fixed at creation, so an Empty cannot grow
        # a mesh. Rebuilding is the only way to give artwork to objects that
        # were placed before the asset path was known.
        for obj in existing:
            bpy.data.objects.remove(obj, do_unlink=True)

        rebuilt = []
        for placed, slot, parent, matrix, order in records:
            obj = scene.create_empty(context, placed, catalog, root, tree, slot)
            if parent is not None:
                obj.parent = parent
            obj.matrix_world = matrix
            if order is not None:
                obj["dkr_order"] = order
            rebuilt.append(obj)

        drawn = sum(1 for o in rebuilt if preview.PROP_PREVIEW in o)
        self.report(
            {"INFO"},
            "redrew %d object(s), %d with artwork from %s"
            % (len(rebuilt), drawn, tree.label),
        )
        return {"FINISHED"}


class DKR_OT_set_slot(bpy.types.Operator):
    """Move the selected objects to the other object map"""

    bl_idname = "dkr.set_slot"
    bl_label = "Object Map"
    bl_options = {"REGISTER", "UNDO"}

    slot: EnumProperty(
        name="Object Map",
        items=[
            ("structure", "Structure", "The track: checkpoints, spawns, scenery"),
            ("collectables", "Collectables", "Pickups: coins, balloons"),
            ("TOGGLE", "Toggle", "Swap each selected object to the other map"),
        ],
        default="TOGGLE",
    )

    def execute(self, context):
        targets = [o for o in context.selected_objects if scene.is_dkr_object(o)]
        if not targets:
            active = context.active_object
            if active is None or not scene.is_dkr_object(active):
                self.report({"ERROR"}, "no DKR object selected")
                return {"CANCELLED"}
            targets = [active]

        for obj in targets:
            if self.slot == "TOGGLE":
                current = scene.slot_of(obj)
                obj[scene.PROP_SLOT] = (
                    scene.SLOT_COLLECTABLES
                    if current == scene.SLOT_STRUCTURE
                    else scene.SLOT_STRUCTURE
                )
            else:
                obj[scene.PROP_SLOT] = self.slot

        for area in context.screen.areas:
            area.tag_redraw()
        self.report({"INFO"}, "moved %d object(s)" % len(targets))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_place_object,
    DKR_OT_set_slot,
    DKR_OT_set_enum_field,
    DKR_OT_reset_field,
    DKR_OT_select_by_type,
    DKR_OT_refresh_artwork,
)
