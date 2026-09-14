"""Bring a picture into a track as artwork the ROM never had.

:mod:`..textures` holds the whole of the *format* side of this - what a
``TextureHeader`` is, what fits texture memory, what id a level model stores for
a texture its package ships. None of it needs Blender and all of it is tested
without one. This module is the other half: getting an author's JPEG or PNG,
whatever size and shape it is, down to something that side will accept.

**Resampling is the work, and it is brutal.** A level texture is loaded into the
RDP's 4 KiB of texture memory as a single block, so a colour image gets 2048
texels - 64x32. A 2752x1536 photograph is 4.2 million. There is no version of
this that keeps the picture *in the track*; the honest thing is to do the
reduction plainly, say what it did, and put the result in front of the author as
a thumbnail before they build a track around it.

**Why the PNGs beside the .blend rather than the file the author picked.** The
package has to be rebuildable from the ``.blend``, and the picked file might be
a JPEG on a drive that is not there any more. So the import writes what it
resampled, as a PNG, in a folder beside the scene, and everything afterwards -
the thumbnail, the material, the export - reads that.

It also writes the picture **at full resolution**, as a PNG in ``original/``
under that folder. The track never reads it. The export's high-resolution
texture pack does: RT64 draws it in place of the 64x32, which gets back what
the reduction threw away - see :mod:`..rice_pack`. Keeping it beside the scene
is what lets that pack be rebuilt from the ``.blend`` too.

**One way in, used twice.** :func:`add_image` is the whole import - resample,
check through the encoder, keep the original, record it. The Add Custom Texture
button calls it with a file the author picked; *Track From Mesh* calls it with
each image the mesh's materials draw. There is no second copy to drift.

**Ordinals are positions and positions are identity.** The runtime hands a
track's textures ids by their order in its manifest, and the level model names
them by the same order. So adding goes on the end, and removing has to move
every id behind it down one - which the mesh records, not the scene, so the two
are stepped together here.

That is also why removing stops short of a texture the geometry has already
taken a *table entry* for. A table index is a position too, and dropping an
entry would move every later one along with the faces that hold it, the
materials keyed by it and the surface types read off those materials. No other
operator in the addon removes a table entry - clearing a texture off faces
leaves the entry standing - so this one does not invent that either. It says
which mesh is holding on and what to do about it.
"""

from __future__ import annotations

import os
import shutil
import tempfile
import traceback

import bpy
from bpy.props import EnumProperty, StringProperty
from bpy_extras.io_utils import ImportHelper

from .. import textures as texture_module
from . import geometry

#: Where the resampled PNGs go, beside the ``.blend`` that names them.
FOLDER = "dkr_textures"

#: Where the full-resolution copies go, inside :data:`FOLDER`. Named like the
#: reduced PNG they belong to, so the pair is obvious in a file browser.
ORIGINALS = "original"


class CustomTextureError(Exception):
    """The picture cannot become a texture, with the reason an author can act on."""


# ---------------------------------------------------------------------------
# The scene's list, as the rest of the addon wants to see it
# ---------------------------------------------------------------------------

def entries(context) -> list:
    """The track's own textures as :class:`..textures.CustomTexture`.

    In collection order, which is ordinal order, which is the order the package
    writes them in. Nothing sorts this.
    """
    settings = getattr(context.scene, "dkr", None)
    if settings is None:
        return []
    found = []
    for ordinal, record in enumerate(settings.custom_textures):
        found.append(texture_module.CustomTexture(
            ordinal=ordinal,
            name=record.name,
            png=resolve(record.png),
            width=record.width,
            height=record.height,
            texture_format=record.format,
            render_mode=record.render_mode or "OPAQUE",
            source=record.source,
            original=resolve(record.original),
            nudge=record.nudge,
        ))
    return found


def resolve(stored: str) -> str:
    """A stored image path as something that can be opened.

    Paths are kept relative to the ``.blend``'s directory so that a scene and
    its pictures can be moved or handed to someone else together. Absolute is
    still accepted, because an unsaved scene has nothing to be relative to.
    """
    if not stored:
        return ""
    if os.path.isabs(stored):
        return stored
    if not bpy.data.filepath:
        return stored
    return os.path.normpath(
        os.path.join(os.path.dirname(bpy.data.filepath), stored)
    )


def by_id(context, texture_id):
    """The track's own texture an id names, or ``None`` if it names the ROM's."""
    ordinal = texture_module.custom_ordinal(texture_id)
    if ordinal is None:
        return None
    found = entries(context)
    return found[ordinal] if 0 <= ordinal < len(found) else None


def folder(context) -> str:
    """Where a resampled PNG is written.

    Beside the ``.blend`` when there is one, so the package travels with the
    scene. An unsaved scene gets Blender's temporary directory, which is honest
    about what it is: the author is told to save.
    """
    if bpy.data.filepath:
        return os.path.join(os.path.dirname(bpy.data.filepath), FOLDER)
    return os.path.join(bpy.app.tempdir, FOLDER)


def _unique(directory: str, stem: str) -> str:
    """A path in ``directory`` that is not taken.

    A re-import must not write over the PNG a previous one produced: the preview
    system caches a thumbnail against the path, so the old picture would go on
    being drawn for the new texture until Blender restarted.
    """
    candidate = os.path.join(directory, stem + ".png")
    suffix = 2
    while os.path.exists(candidate):
        candidate = os.path.join(directory, "%s-%d.png" % (stem, suffix))
        suffix += 1
    return candidate


def _slug(text: str) -> str:
    kept = [char if char.isalnum() else "-" for char in (text or "").lower()]
    return "".join(kept).strip("-").replace("--", "-") or "texture"


# ---------------------------------------------------------------------------
# The picture a material draws
# ---------------------------------------------------------------------------

def image_node(material):
    """The image node a material draws with, or ``None``.

    Walked back from the output rather than taken from the first image node in
    the tree, because a material often carries images nothing is linked to - a
    roughness map, a leftover - and the one that reaches the surface is the one
    the author sees.
    """
    tree = getattr(material, "node_tree", None) if material is not None else None
    if tree is None:
        return None
    output = next((node for node in tree.nodes
                   if node.type == "OUTPUT_MATERIAL" and node.is_active_output),
                  None)
    found = _walk(output, set())
    if found is not None:
        return found
    for node in tree.nodes:
        if node.type == "TEX_IMAGE" and node.image is not None:
            return node
    return None


def _walk(node, seen):
    if node is None or node.name in seen:
        return None
    seen.add(node.name)
    if node.type == "TEX_IMAGE" and node.image is not None:
        return node
    for socket in node.inputs:
        for link in socket.links:
            found = _walk(link.from_node, seen)
            if found is not None:
                return found
    return None


def image_of(material):
    """The picture a material draws, or ``None``."""
    node = image_node(material)
    return node.image if node is not None else None


def _image_file(image) -> str:
    try:
        path = bpy.path.abspath(image.filepath_from_user())
    except (AttributeError, RuntimeError, ValueError):
        return ""
    return path if path and os.path.isfile(path) else ""


def image_source(image) -> str:
    """What an image is, as :attr:`source` records it: a file, or a packed one.

    Also the key two materials are matched on, so that two materials showing
    one picture become one texture, and a conversion run twice reuses what the
    first run added instead of spending another of the 255 ordinals.
    """
    return _image_file(image) or "packed:%s" % image.name


#: Extensions an image datablock's name often keeps from its file, and which
#: would otherwise end up in the texture's name - ``road.png`` as ``road-png``.
_IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".tif", ".tiff",
                     ".exr", ".webp", ".hdr", ".dds", ".psd"}


def image_label(image) -> str:
    """What the panel calls a texture made from ``image``."""
    stem, extension = os.path.splitext(image.name)
    return stem if stem and extension.lower() in _IMAGE_EXTENSIONS else image.name


def same_source(one: str, other: str) -> bool:
    if not one or not other:
        return False
    return (os.path.normcase(os.path.normpath(one))
            == os.path.normcase(os.path.normpath(other)))


def image_path(image) -> str:
    """A file on disk holding an image's pixels, written out if it has none.

    A packed or generated image is saved through a copy of the datablock,
    because setting ``filepath_raw`` on the image itself and saving would
    unpack the one the scene is using.
    """
    path = _image_file(image)
    if path:
        return path
    out = os.path.join(bpy.app.tempdir or tempfile.gettempdir(),
                       "dkr-src-%s.png" % _slug(image.name))
    copy = image.copy()
    try:
        copy.file_format = "PNG"
        copy.filepath_raw = out
        copy.save()
    finally:
        bpy.data.images.remove(copy)
    return out


# ---------------------------------------------------------------------------
# Resampling
# ---------------------------------------------------------------------------

def resample(source: str, destination: str, width: int, height: int):
    """Read any image Blender reads, scale it, and write it as an 8-bit PNG.

    ``(was_width, was_height)`` comes back, so the operator can say what it took
    the picture down from.

    Blender is used for this and only this. It decodes JPEG, PNG, TGA and the
    rest, and it resamples; what it must not do is colour-manage on the way
    through, because the texels written afterwards are the bytes in this file.

    For the ordinary case it does not: an eight-bit image round-trips through
    ``scale`` and ``save`` byte for byte - measured on a 2752x1536 JPEG, whose
    channel means came back identical with and without the line below.
    ``Non-Color`` is set anyway because that guarantee is only for eight-bit
    images, and a float source (an EXR, an HDR) *is* transformed on the way out.
    """
    return _save_png(source, destination, (int(width), int(height)))


def keep_original(source: str, destination: str):
    """Put the picture beside its reduction at full size, as a PNG.

    A PNG is copied as it is - it is already what the pack holds, and a copy
    cannot change a pixel. Anything else goes through Blender exactly as
    :func:`resample` does, only without the scale.
    """
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    size = texture_module.png_size(source)
    if size is not None:
        shutil.copyfile(source, destination)
        return size
    return _save_png(source, destination, None)


def _save_png(source, destination, size):
    image = bpy.data.images.load(source, check_existing=False)
    try:
        was = (image.size[0], image.size[1])
        if was[0] <= 0 or was[1] <= 0:
            raise CustomTextureError(
                "%s has no pixels; Blender could not read it as an image"
                % os.path.basename(source)
            )
        try:
            image.colorspace_settings.name = "Non-Color"
        except (AttributeError, TypeError):
            pass  # A build without that name still writes the buffer as it is.
        # Changing the colour space frees the loaded pixels, and ``save`` does
        # not load them again - it fails with "does not have any image data".
        # ``scale`` does, so the original is scaled to the size it already is.
        image.scale(*(size if size is not None else was))
        image.file_format = "PNG"
        image.filepath_raw = destination
        image.save()
        return was
    finally:
        bpy.data.images.remove(image)


def _parse_size(text: str, texture_format: int, was):
    """The size to resample to: what the author typed, or the best fit."""
    text = (text or "").strip().lower().replace(" ", "")
    if not text:
        return texture_module.best_size(texture_format, was[0], was[1])
    for separator in ("x", "*", ","):
        if separator in text:
            left, _sep, right = text.partition(separator)
            try:
                return (int(left), int(right))
            except ValueError:
                break
    raise CustomTextureError(
        "%r is not a size; write it as WxH, for example 64x32, or leave it "
        "blank to take the largest the format allows" % text
    )


# ---------------------------------------------------------------------------
# Adding and taking away
# ---------------------------------------------------------------------------

def add_image(context, source: str, texture_format, size: str = "",
              name: str = None, note: str = None):
    """Make ``source`` one of the track's own textures: ``(entry, was)``.

    Resamples it, reads the result back through the addon's own encoder, keeps
    the original at full size, and appends the record. Either all of that
    happens or none of it: a failure leaves no file behind and no record, and
    raises :class:`CustomTextureError` with a reason an author can act on.

    ``name`` is what the panel calls it, the file's name by default. ``note`` is
    what :attr:`source` records, the file's path by default - a packed image
    has a better answer than the temporary file it was written out to.
    """
    settings = context.scene.dkr
    code = int(texture_format)
    if len(settings.custom_textures) >= texture_module.CUSTOM_ID_COUNT:
        raise CustomTextureError(
            "a track can add at most %d textures of its own"
            % texture_module.CUSTOM_ID_COUNT
        )
    if not source or not os.path.isfile(source):
        raise CustomTextureError("pick an image file")

    directory = folder(context)
    try:
        os.makedirs(directory, exist_ok=True)
    except OSError as error:
        raise CustomTextureError("could not write to %s: %s" % (directory, error))

    label = name or os.path.splitext(os.path.basename(source))[0]
    destination = _unique(directory, _slug(label))
    original = os.path.join(directory, ORIGINALS, os.path.basename(destination))

    try:
        was = _probe(source)
        width, height = _parse_size(size, code, was)
        texture_module.check_size(width, height, code)
        was = resample(source, destination, width, height)
        # Read it straight back through the addon's own decoder. Blender
        # writing a PNG the encoder cannot read is the failure that would
        # otherwise wait until export, with a track already built on it.
        texture_module.encode_texture(destination, code)
        keep_original(source, original)
    except (CustomTextureError, texture_module.TextureEncodeError) as error:
        _discard(destination)
        _discard(original)
        raise CustomTextureError(str(error))
    except Exception as error:  # noqa: BLE001 - Blender image errors vary
        traceback.print_exc()
        _discard(destination)
        _discard(original)
        raise CustomTextureError("could not read %s: %s"
                                 % (os.path.basename(source), error))

    record = settings.custom_textures.add()
    record.name = label
    record.source = note or source
    record.png = _stored_path(destination)
    record.original = _stored_path(original)
    record.width = width
    record.height = height
    record.format = code
    record.render_mode = "OPAQUE"
    record.nudge = 0
    return entries(context)[-1], was


def discard_last(context, count: int) -> None:
    """Take back the last ``count`` textures added, files and all.

    For an operation that added some and then failed: the scene goes back to
    what it held before, rather than keeping pictures nothing draws. Only ever
    the tail - an ordinal is an identity, and the tail is the one place removal
    renumbers nothing.
    """
    settings = context.scene.dkr
    for _each in range(max(0, int(count))):
        position = len(settings.custom_textures) - 1
        if position < 0:
            return
        record = settings.custom_textures[position]
        _discard(resolve(record.png))
        _discard(resolve(record.original))
        settings.custom_textures.remove(position)


def original_for(context, ordinal: int) -> str:
    """The full-resolution PNG of one of the track's textures, or ``""``.

    A texture added before the pack existed has no copy. If the file it was
    added from is still where it was, the copy is made now, so an old scene
    gets its pack on the next export without being asked anything.
    """
    settings = context.scene.dkr
    if not 0 <= ordinal < len(settings.custom_textures):
        return ""
    record = settings.custom_textures[ordinal]
    path = resolve(record.original)
    if path and os.path.isfile(path):
        return path
    source = record.source
    if not source or not os.path.isfile(source):
        return ""
    reduced = resolve(record.png)
    target = os.path.join(os.path.dirname(reduced) or folder(context), ORIGINALS,
                          os.path.basename(reduced) or "%d.png" % ordinal)
    try:
        keep_original(source, target)
    except Exception:  # noqa: BLE001 - no copy means no HD, not a failed export
        traceback.print_exc()
        _discard(target)
        return ""
    record.original = _stored_path(target)
    return target


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

def _format_items(self, context):
    """The same list the scene's own Format menu offers, from one place."""
    from .. import props  # noqa: PLC0415 - registered after this module loads

    return props.texture_format_items(self, context)


class DKR_OT_add_custom_texture(bpy.types.Operator, ImportHelper):
    """Add an image of your own to this track's artwork"""

    bl_idname = "dkr.add_custom_texture"
    bl_label = "Add Custom Texture"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ""
    filter_glob: StringProperty(
        default="*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tif;*.tiff;*.exr;*.webp",
        options={"HIDDEN"},
    )

    texture_format: EnumProperty(
        name="Format",
        description="How the texture is stored, which sets how large it can be",
        items=_format_items,
    )

    size: StringProperty(
        name="Size",
        description=(
            "Width and height to resample to, as WxH. Both have to be powers "
            "of two no larger than 64. Blank takes the largest the format "
            "allows, in the shape closest to the picture's"
        ),
        default="",
    )

    def invoke(self, context, event):
        settings = context.scene.dkr
        self.texture_format = settings.custom_format
        self.size = settings.custom_size
        return ImportHelper.invoke(self, context, event)

    def execute(self, context):
        settings = context.scene.dkr
        settings.custom_format = self.texture_format
        settings.custom_size = self.size

        try:
            entry, was = add_image(context, self.filepath,
                                   int(self.texture_format), self.size)
        except CustomTextureError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        # Select it in the browser so the next click is Apply rather than a
        # hunt through fourteen hundred thumbnails for the one just added.
        settings.texture_id = entry.index

        if not bpy.data.filepath:
            self.report(
                {"WARNING"},
                "%s was written to Blender's temporary folder because this "
                "scene has never been saved. Save the .blend and add it again, "
                "or the picture will be gone next session"
                % os.path.basename(entry.png),
            )
        self.report(
            {"INFO"},
            "added %s at %dx%d, down from %dx%d" % (entry.name, entry.width,
                                                    entry.height, was[0], was[1]),
        )
        return {"FINISHED"}


def _probe(source: str):
    """The picture's size before anything is done to it."""
    image = bpy.data.images.load(source, check_existing=False)
    try:
        return (image.size[0], image.size[1])
    finally:
        bpy.data.images.remove(image)


def _stored_path(path: str) -> str:
    """Relative to the ``.blend``'s directory where possible, so the scene moves."""
    if not bpy.data.filepath:
        return path
    try:
        relative = os.path.relpath(path, os.path.dirname(bpy.data.filepath))
    except ValueError:
        return path  # A different drive on Windows has no relative path.
    if relative.startswith(".."):
        return path  # Outside the scene's folder; it would not travel anyway.
    return relative.replace(os.sep, "/")


def _discard(path: str) -> None:
    try:
        if path and os.path.isfile(path):
            os.remove(path)
    except OSError:
        pass


class DKR_OT_remove_custom_texture(bpy.types.Operator):
    """Remove one of this track's own textures, renumbering the rest"""

    bl_idname = "dkr.remove_custom_texture"
    bl_label = "Remove Custom Texture"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        settings = getattr(context.scene, "dkr", None)
        return bool(settings and len(settings.custom_textures))

    def execute(self, context):
        settings = context.scene.dkr
        position = texture_module.custom_ordinal(settings.texture_id)
        if position is None or position >= len(settings.custom_textures):
            self.report(
                {"ERROR"},
                "pick one of this track's own textures first; the browser is "
                "currently on one of the ROM's, which a track cannot remove",
            )
            return {"CANCELLED"}
        going = texture_module.custom_id(position)
        name = settings.custom_textures[position].name

        # A texture the geometry has taken a table entry for cannot be removed
        # here, and the reason is not squeamishness. A table index is a
        # position: dropping an entry moves every later one, so every face
        # holding a higher index, every material keyed by one, and every
        # surface type read off those materials would all have to move with it.
        # The addon has no operation that removes a table entry - clearing a
        # texture off faces leaves the entry standing - so inventing one here,
        # in the operator least likely to be tested, is how a track quietly
        # starts drawing its neighbour's pictures.
        using = _table_users(context, going)
        if using:
            self.report(
                {"ERROR"},
                "%s already has a place in the texture table of %s, and removing "
                "it would move every entry after it - repainting whatever "
                "draws them. Point those faces at another texture first: "
                "pick one, Select, then Apply"
                % (name, " and ".join(sorted(using))),
            )
            return {"CANCELLED"}

        # A later texture named by the *base* table is the other thing that
        # cannot move. Those ids are written in the model file on disk - a
        # track converted from a mesh keeps its textures there - and renumbering
        # reaches only what the mesh carries on top of that file.
        held = _base_holders(context, going)
        if held:
            self.report(
                {"ERROR"},
                "removing %s would renumber the textures after it, and the "
                "model file of %s names some of those by number. Remove from "
                "the end of the list instead"
                % (name, " and ".join(sorted(held))),
            )
            return {"CANCELLED"}

        # Nothing refers to it by table entry, so all that is left is the ids
        # of the textures behind it in the queue, which each move down one.
        moved = _renumber(context, going)

        settings.custom_textures.remove(position)
        settings.texture_id = -1

        if moved:
            self.report({"INFO"},
                        "removed %s; %d later texture(s) moved up" % (name, moved))
        else:
            self.report({"INFO"}, "removed %s" % name)
        return {"FINISHED"}


def _table_users(context, texture_id: int) -> set:
    """The geometry whose texture table - base or added - has this texture."""
    found = set()
    for obj in geometry.geometry_objects(context):
        for record in geometry.texture_table(obj):
            if int(record.get("id", 0)) == int(texture_id):
                found.add(obj.data.name)
                break
    return found


def _base_holders(context, going: int) -> set:
    """The geometry whose base table names one of this track's textures after
    ``going`` - an id :func:`_renumber` cannot reach."""
    found = set()
    for obj in geometry.geometry_objects(context):
        for record in geometry.base_textures(obj):
            identifier = int(record.get("id", 0))
            if texture_module.is_custom_id(identifier) and identifier > going:
                found.add(obj.data.name)
                break
    return found


def _renumber(context, going: int) -> int:
    """Move every recorded id past ``going`` down one, and say how many moved.

    The ids live on the mesh and the ordinals live in the scene, so this is the
    one place the two have to be kept in step by hand. Only the id changes; no
    entry is added or dropped, so every table index stays exactly where it was.
    """
    moved = 0
    for obj in geometry.geometry_objects(context):
        extras = geometry.extra_textures(obj)
        if not extras:
            continue
        changed = False
        for record in extras:
            identifier = int(record.get("id", 0))
            if texture_module.is_custom_id(identifier) and identifier > going:
                record["id"] = identifier - 1
                changed = True
                moved += 1
        if changed:
            geometry.set_extra_textures(obj, extras)
    return moved


CLASSES = (
    DKR_OT_add_custom_texture,
    DKR_OT_remove_custom_texture,
)
