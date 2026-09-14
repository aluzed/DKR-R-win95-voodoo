"""Lay a level model out afresh, for edits that change how much of it there is.

Step 3 of Phase 2. Steps 1 and 2 kept every array at the offset it was read
from, which is what let an author reshape a track for free - no count changed,
so no offset moved. Adding or removing a triangle breaks that: everything after
it shifts, and the file has to be built rather than patched.

Two jobs live here, and they are separate on purpose.

:func:`rebatch_segment` turns a flat list of faces back into batches. A batch is
one draw call over a contiguous window of the segment's vertices, and the
windows partition the vertex array exactly - measured across all 2292 retail
segments, zero gaps and zero overlaps - so no vertex is shared between batches
and a re-batcher duplicates rather than shares. That is not an optimisation to
undo later; the triangle's vertex index is a ``u8`` counted from the batch's own
window, so a shared vertex is not expressible.

:func:`rebuild` assigns fresh offsets. Retail's own layout is not reproducible
by rule - the padding between arrays runs 0, 4, 8, 10, 12 ... 770 bytes with no
pattern - so this writes a *valid* layout rather than *the* layout, and the
section order it uses is retail's, which is universal across all 55 models:
header, textures, segments, then per segment batches, triangles and vertices,
then bounding boxes, the BSP, the PVS and the collision facet reservations.

Three things are measured rather than assumed, and the module docstrings say so
where it matters:

* ``modelSize`` at 0x48 is the inflated length. All 55 models.
* A batch terminator holds ``(vertex count, triangle count)``. All 1146
  segments.
* Which batches are **opaque** is authored, not derivable. No flag bit separates
  the two sides of ``numberofOpaqueBatches`` - the intersection over all retail
  batches is empty - and neither does the texture format, where eight of the
  fourteen formats appear on both sides. So opacity travels with the face, from
  the batch it came from, and is never inferred.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from .level_model import (
    BATCH_SIZE, BOUNDING_BOX_SIZE, BSP_NODE_SIZE, ENDIAN, HEADER_SIZE,
    SEGMENT_SIZE, TEXTURE_INFO_SIZE, TRIANGLE_SIZE, VERTEX_SIZE, Batch,
    LevelModel, Segment, RENDER_NO_COLLISION, pvs_size,
)

#: Every array is put on a 16 byte boundary. Retail is less consistent - most
#: arrays land on 16, some only on 8, and the collision facet reservations often
#: on 2 - but over-aligning is always safe, and the cost is a few bytes per
#: array against a half megabyte budget.
ALIGNMENT = 16

#: One ``CollisionFacetPlanes``: a base plane index and three edge bisectors,
#: all ``u16``. Authored adjacency, not scratch - see :func:`collision_facets`.
FACET_SIZE = 8

#: The format's ceiling, from the ``u8`` batch-local vertex index. Nothing in
#: retail comes close - the widest batch across all 110 models is 24 vertices
#: and the mean is about 9 - which is why :data:`BATCH_VERTEX_TARGET` aims at
#: retail's shape instead and this stays a hard backstop.
MAX_BATCH_VERTICES = 256

#: What a rebuilt batch aims for. Retail's ceiling, not the format's: batches
#: this size are what the RSP vertex buffer was sized around, and a re-batcher
#: that produced 200-vertex batches would be legal and nothing like the data the
#: renderer was tuned for.
BATCH_VERTEX_TARGET = 24

#: ``LEVEL_MODEL_MAX_SIZE`` in ``tracks.c``. Covers the inflated blob *and*
#: everything the loader allocates past ``modelSize``.
BUDGET = 0x82A00

#: ``Triangle.flags``: this triangle is skipped when collision is derived.
TRI_FLAG_NO_COLLISION = 0x80

#: ``TriangleBatchInfo.flags``: the batch is a water surface, which the loader
#: counts into ``unk32`` and gives scratch for.
BATCH_FLAG_WAVES = 0x2000


class LayoutError(Exception):
    pass


def align(offset: int, to: int = ALIGNMENT) -> int:
    return (offset + to - 1) // to * to


# ---------------------------------------------------------------------------
# Faces, detached from their batching
# ---------------------------------------------------------------------------

class BatchKey(tuple):
    """Which draw call a face belongs to.

    ``opaque`` is in here because it decides which side of
    ``numberofOpaqueBatches`` a batch lands on, and because it cannot be
    recovered from anything else - see the module docstring.

    ``serial`` is what keeps an untouched segment untouched. Retail routinely
    holds two batches that agree on every rendering field - 411 of the 1146
    segments do - so grouping on the fields alone merges them, and a segment
    nobody edited comes back with fewer batches and its vertices re-duplicated.
    Carrying the batch a face came from means a decompose and rebatch of an
    unedited segment is the identity. A face the author adds takes the serial of
    the batch its material maps to, so it joins that draw call; passing ``None``
    asks for the merging behaviour instead.
    """

    __slots__ = ()

    def __new__(cls, texture_index, flags, misc, texture_offset,
                vertex_override, opaque, serial=None):
        return tuple.__new__(cls, (int(texture_index), int(flags) & 0xFFFFFFFF,
                                   int(misc), int(texture_offset),
                                   int(vertex_override), bool(opaque),
                                   None if serial is None else int(serial)))

    texture_index = property(lambda self: self[0])
    flags = property(lambda self: self[1])
    misc = property(lambda self: self[2])
    texture_offset = property(lambda self: self[3])
    vertex_override = property(lambda self: self[4])
    opaque = property(lambda self: self[5])
    serial = property(lambda self: self[6])


class Face:
    """One triangle, holding pool vertex indices rather than batch-local ones."""

    __slots__ = ("key", "vertices", "uvs", "flags")

    def __init__(self, key: BatchKey, vertices, uvs, flags: int = 0):
        self.key = key
        #: Three indices into the segment's vertex pool.
        self.vertices = tuple(int(v) for v in vertices)
        #: ``((s0, t0), (s1, t1), (s2, t2))``, raw fixed point.
        self.uvs = tuple(tuple(int(c) for c in pair) for pair in uvs)
        self.flags = int(flags) & 0xFF


def decompose(segment: Segment) -> Tuple[List[Face], List, List]:
    """Pull a segment apart into faces and one flat vertex pool.

    The inverse of :func:`rebatch_segment`, and the reason the re-batcher can be
    tested without Blender anywhere near it: decomposing and rebatching an
    untouched segment has to give the same segment back.
    """
    faces: List[Face] = []
    for index, batch in enumerate(segment.batches):
        key = BatchKey(batch.texture_index, batch.flags, batch.misc,
                       batch.texture_offset, batch.vertex_override,
                       index < segment.opaque_batches, index)
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face >= len(segment.triangles):
                break
            flags, vi0, vi1, vi2 = segment.triangles[face]
            base = batch.vertex_offset
            uvs = segment.uvs[face] if face < len(segment.uvs) else ((0, 0),) * 3
            faces.append(Face(key, (base + vi0, base + vi1, base + vi2), uvs, flags))
    return faces, list(segment.vertices), list(segment.colours)


def rebatch_segment(segment: Segment, faces: Sequence[Face], positions: Sequence,
                    colours: Sequence,
                    target: int = BATCH_VERTEX_TARGET) -> None:
    """Rebuild a segment's batches, triangles and vertices from loose faces.

    Faces are grouped by :class:`BatchKey`, opaque groups first, and each group
    is cut into batches once it would outgrow ``target`` vertices. Order within
    a group is the order the faces arrive in, so an untouched segment comes back
    unchanged rather than merely equivalent.
    """
    if target < 3 or target > MAX_BATCH_VERTICES:
        raise LayoutError(
            "a batch target of %d vertices cannot hold a triangle, or exceeds "
            "the %d the u8 index allows" % (target, MAX_BATCH_VERTICES)
        )

    groups: Dict[BatchKey, List[Face]] = {}
    order: List[BatchKey] = []
    for face in faces:
        if face.key not in groups:
            groups[face.key] = []
            order.append(face.key)
        groups[face.key].append(face)

    # Opaque first, because numberofOpaqueBatches is a split point and not a
    # label: render_level_segment draws [0, k) then [k, n).
    order.sort(key=lambda key: not key.opaque)

    new_batches: List[Batch] = []
    new_vertices: List = []
    new_colours: List = []
    new_triangles: List = []
    new_uvs: List = []
    opaque_count = 0

    for key in order:
        for chunk in _chunk(groups[key], target):
            batch = Batch(key.texture_index, len(new_vertices), len(new_triangles),
                          key.flags, key.vertex_override, key.misc,
                          key.texture_offset)
            local: Dict[int, int] = {}
            for face in chunk:
                indices = []
                for pool_index in face.vertices:
                    if pool_index not in local:
                        if not 0 <= pool_index < len(positions):
                            raise LayoutError(
                                "a face names vertex %d, which the pool of %d "
                                "does not hold" % (pool_index, len(positions))
                            )
                        local[pool_index] = len(local)
                        new_vertices.append(tuple(positions[pool_index]))
                        new_colours.append(tuple(
                            colours[pool_index] if pool_index < len(colours)
                            else (0, 0, 0, 0)
                        ))
                    indices.append(local[pool_index])
                new_triangles.append((face.flags, indices[0], indices[1], indices[2]))
                new_uvs.append(face.uvs)
            batch.vertex_count = len(new_vertices) - batch.vertex_offset
            batch.face_count = len(new_triangles) - batch.face_offset
            if batch.vertex_count > MAX_BATCH_VERTICES:
                raise LayoutError(
                    "a batch ended up addressing %d vertices; the triangle's "
                    "vertex index is a u8 counted from the batch, so %d is all "
                    "it can reach" % (batch.vertex_count, MAX_BATCH_VERTICES)
                )
            new_batches.append(batch)
            if key.opaque:
                opaque_count += 1

    segment.batches = new_batches
    segment.vertices = new_vertices
    segment.colours = new_colours
    segment.triangles = new_triangles
    segment.uvs = new_uvs
    segment.opaque_batches = opaque_count
    _sync_segment(segment)


def _chunk(faces: Sequence[Face], target: int) -> Iterable[List[Face]]:
    """Cut a group into batches, closing one before its vertices outgrow ``target``."""
    current: List[Face] = []
    seen: set = set()
    for face in faces:
        fresh = {v for v in face.vertices if v not in seen}
        if current and len(seen) + len(fresh) > target:
            yield current
            current, seen = [], set()
            fresh = set(face.vertices)
        current.append(face)
        seen |= fresh
    if current:
        yield current


# ---------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------

def _sync_segment(segment: Segment) -> None:
    """Bring a segment's count fields and terminator in line with its lists.

    The terminator holds the end offsets, so each batch's span is the difference
    to the next entry - which makes the terminator ``(vertex count, triangle
    count)``, true in all 1146 retail segments.
    """
    segment.vertex_count_field = len(segment.vertices)
    segment.triangle_count_field = len(segment.triangles)
    segment.batch_count_field = len(segment.batches)
    terminator = Batch(0xFF, len(segment.vertices), len(segment.triangles), 0)
    segment.batch_terminator = terminator


def check_windows(model: LevelModel) -> List[str]:
    """Every batch window must tile its segment exactly, with nothing shared.

    This is a postcondition of the re-batcher rather than an observation about
    retail. The Blender importer resolves a face to its batch through these
    windows, so a gap or an overlap silently mis-assigns render flags and
    mis-groups invisible walls, with nothing raised anywhere.
    """
    problems = []
    for segment in model.segments:
        if not segment.batches:
            continue
        cursor = 0
        for index, batch in enumerate(segment.batches):
            if batch.vertex_offset != cursor:
                problems.append(
                    "segment %d batch %d starts at vertex %d, expected %d; batch "
                    "windows have to tile the segment with no gap or overlap"
                    % (segment.index, index, batch.vertex_offset, cursor)
                )
            if batch.vertex_count > MAX_BATCH_VERTICES:
                problems.append(
                    "segment %d batch %d addresses %d vertices, past the %d a u8 "
                    "index reaches"
                    % (segment.index, index, batch.vertex_count, MAX_BATCH_VERTICES)
                )
            cursor = batch.vertex_offset + batch.vertex_count
        if cursor != len(segment.vertices):
            problems.append(
                "segment %d batches cover %d vertices but it holds %d"
                % (segment.index, cursor, len(segment.vertices))
            )

        cursor = 0
        for index, batch in enumerate(segment.batches):
            if batch.face_offset != cursor:
                problems.append(
                    "segment %d batch %d starts at face %d, expected %d"
                    % (segment.index, index, batch.face_offset, cursor)
                )
            cursor = batch.face_offset + batch.face_count
        if cursor != len(segment.triangles):
            problems.append(
                "segment %d batches cover %d faces but it holds %d"
                % (segment.index, cursor, len(segment.triangles))
            )

        if not 0 <= segment.opaque_batches <= len(segment.batches):
            problems.append(
                "segment %d says %d of its %d batches are opaque"
                % (segment.index, segment.opaque_batches, len(segment.batches))
            )
    return problems


#: How near two corners have to be to be one: within 3 units on every axis.
#: The game's own test, ``NEARBY`` in ``object_models.c:656``.
EDGE_TOLERANCE = 3


def collision_facets(segment: Segment) -> bytes:
    """The ``CollisionFacetPlanes`` array for one segment, as the file holds it.

    Not scratch, whatever the reservation looks like. ``track_init_collision``
    (``tracks.c:3064-3223``) derives one plane per triangle and then *reads*
    each facet: the triangle's ``basePlaneIndex``, and for each edge the plane
    of the triangle across it, from which it builds the plane bounding that
    edge - a bisector with the neighbour, or a wall straight up from an edge
    that names its own triangle. Left zeroed, every triangle points at the first
    one's plane and every edge at nothing useful, and a racer falls through a
    floor with no hole in it.

    The rule is the one the game itself uses when it builds facets for an
    object model (``object_models.c:559-676``): planes are numbered by
    triangle, skipping those flagged not to collide; an edge's neighbour is the
    first triangle, in a batch that collides, with an edge whose corners are
    the same vertices or within :data:`EDGE_TOLERANCE` of them, either way
    round; an edge with none names its own triangle. Held to the 55 retail
    level models it reproduces 99.6% of their 90,617 facets exactly - exact
    positions alone give 99.3%, and refusing folds sharper than a right angle
    drops it to 88%, so that is not what retail did either.
    """
    ordinal: Dict[int, int] = {}
    faces = []
    for batch in segment.batches:
        collides = not (batch.flags & RENDER_NO_COLLISION)
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face >= len(segment.triangles):
                break
            flags, a, b, c = segment.triangles[face][:4]
            corners = [batch.vertex_offset + index for index in (a, b, c)]
            if max(corners) >= len(segment.vertices):
                continue
            faces.append((face, corners, collides))
            if not flags & TRI_FLAG_NO_COLLISION:
                ordinal[face] = len(ordinal)

    def at(index):
        return segment.vertices[index][:3]

    def cell(index):
        # Wider than twice the tolerance, so near corners share a cell or
        # sit in adjacent ones.
        return tuple(int(value) // 8 for value in at(index))

    def near(i, j):
        if i == j:
            return True
        a, b = at(i), at(j)
        return all(abs(a[axis] - b[axis]) <= EDGE_TOLERANCE for axis in range(3))

    # Every collidable edge, filed under the cell of each of its corners, so a
    # lookup around one corner finds it whichever way round it runs.
    edges: Dict[tuple, List[Tuple[int, int, int]]] = {}
    for face, corners, collides in faces:
        if face not in ordinal or not collides:
            continue
        for side in range(3):
            p, q = corners[side], corners[(side + 1) % 3]
            for corner in {cell(p), cell(q)}:
                edges.setdefault(corner, []).append((face, p, q))

    out = bytearray(len(segment.triangles) * FACET_SIZE)
    for face, corners, _collides in faces:
        if face not in ordinal:
            continue  # derives no plane, and the loader skips its facet
        own = ordinal[face]
        row = [own]
        for side in range(3):
            a, b = corners[side], corners[(side + 1) % 3]
            cx, cy, cz = cell(a)
            best = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for other, p, q in edges.get((cx + dx, cy + dy, cz + dz), ()):
                            if other == face or (best is not None and other >= best):
                                continue
                            if (near(a, p) and near(b, q)) or (near(a, q) and near(b, p)):
                                best = other
            row.append(ordinal[best] if best is not None else own)
        struct.pack_into(ENDIAN + "4H", out, face * FACET_SIZE, *row)
    return bytes(out)


def rebuild(model: LevelModel) -> int:
    """Give every array a fresh offset and resize the blob. Returns the size.

    Section order is retail's, which is the same in all 55 models: header,
    texture table, segment array, then each segment's batches, triangles and
    vertices, then the bounding boxes, the BSP, the PVS, and finally one
    collision facet reservation per segment.
    """
    problems = check_windows(model)
    if problems:
        raise LayoutError(problems[0])

    model.texture_count_field = len(model.textures)
    model.segment_count_field = len(model.segments)
    for segment in model.segments:
        _sync_segment(segment)

    offset = align(HEADER_SIZE)
    model.textures_ptr = offset
    offset = align(offset + len(model.textures) * TEXTURE_INFO_SIZE)
    model.segments_ptr = offset
    offset = align(offset + len(model.segments) * SEGMENT_SIZE)

    for segment in model.segments:
        segment.batches_ptr = offset
        offset = align(offset + (len(segment.batches) + 1) * BATCH_SIZE)
        segment.triangles_ptr = offset
        offset = align(offset + len(segment.triangles) * TRIANGLE_SIZE)
        segment.vertices_ptr = offset
        offset = align(offset + len(segment.vertices) * VERTEX_SIZE)

    model.bounding_boxes_ptr = offset
    offset = align(offset + len(model.bounding_boxes) * BOUNDING_BOX_SIZE)
    model.bsp_ptr = offset
    offset = align(offset + len(model.bsp) * BSP_NODE_SIZE)

    # The PVS is one bitmask per segment, so its size is fixed by the segment
    # count. A caller that changed that count and carried the old bitfield
    # forward would get a file that is internally consistent and that the game
    # reads past the end of, which is why this refuses rather than pads: the
    # symptom would be segments winking out with nothing to point at.
    pvs = model.pvs or b"\xff" * pvs_size(len(model.segments))
    if len(pvs) != pvs_size(len(model.segments)):
        raise LayoutError(
            "the PVS is %d bytes but %d segments need %d; regenerate it when "
            "the segment count changes"
            % (len(pvs), len(model.segments), pvs_size(len(model.segments)))
        )
    model.bitfields_ptr = offset
    offset = align(offset + len(pvs))
    model.unk_c_ptr = offset

    for segment in model.segments:
        segment.collision_facets_ptr = offset
        offset = align(offset + len(segment.triangles) * FACET_SIZE)

    model.blob_size = offset
    model.model_size = offset

    # Both are authored and have to be written: the PVS, and the collision
    # facets, which the loader reads rather than fills (collision_facets).
    model.gaps = []
    model.opaque = [(model.bitfields_ptr, pvs)]
    for segment in model.segments:
        model.opaque.append((segment.collision_facets_ptr, collision_facets(segment)))
    return offset


# ---------------------------------------------------------------------------
# Budget
# ---------------------------------------------------------------------------

def collidable_triangles(segment: Segment) -> int:
    """Triangles the loader will build collision planes for."""
    count = 0
    for batch in segment.batches:
        if batch.flags & RENDER_NO_COLLISION:
            continue
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face < len(segment.triangles):
                if not segment.triangles[face][0] & TRI_FLAG_NO_COLLISION:
                    count += 1
    return count


def runtime_size(model: LevelModel) -> int:
    """Upper bound on blob plus everything the loader allocates past it.

    The loader grows its arena from ``modelSize``, per segment: two bytes a
    triangle for ``unk10``, the collision planes, then two bytes per water batch
    for ``unk34``, each 16-aligned. ``track_init_collision`` returns
    ``counter * 0x10`` and shares edge bisector planes between neighbouring
    triangles, so four planes a triangle is the worst case and the true figure
    is lower - which is the safe direction for a warning.

    Calibrated against retail: nothing exceeds :data:`BUDGET`, Bluey the largest
    at 68% and the median track at 30%.
    """
    total = model.blob_size
    for segment in model.segments:
        total += align(2 * len(segment.triangles))
        total += 64 * collidable_triangles(segment)
        waves = sum(1 for b in segment.batches if b.flags & BATCH_FLAG_WAVES)
        total += align(2 * waves)
    return total


#: ``collision.c`` keeps ``LevelModelSegment *segments[10]`` and stops filling
#: it at ten. Candidates are chosen by bounding box overlap with the query
#: region, so a segment with an oversized box wins a slot from anywhere on the
#: track and holds it.
COLLISION_CANDIDATES = 10

#: A segment box spanning more than this fraction of the track's longest axis is
#: treated as oversized. Retail's median is 0.14 and its 90th percentile 0.27,
#: so this sits well clear of ordinary data.
OVERSIZED_BOX = 0.5

#: Oversized segments tolerated before it is worth saying something. Retail's
#: worst multi-segment model has two, and 40 of the 55 have none, so this is
#: more than double anything shipped and still half the candidate list.
OVERSIZED_BUDGET = 5


def oversized_segments(model: LevelModel) -> List[int]:
    """Segments whose bounding box spans most of the track.

    These are the ones that crowd collision. Candidate selection in
    ``collision.c`` takes the first ten segments whose box overlaps the query
    region and stops, so a segment stretched across the map overlaps every
    query and permanently holds a slot. A handful of them fills the list, and
    the geometry a racer is actually standing on stops being considered - the
    symptom is falling through the floor somewhere else entirely, which points
    nowhere near the cause.

    One giant polygon is enough to do this to its segment, which is why this
    only becomes reachable at step 3.
    """
    bounds = model.bounds
    if not bounds or len(bounds) < 6:
        return []
    track = max(bounds[1] - bounds[0], bounds[3] - bounds[2],
                bounds[5] - bounds[4])
    if track <= 0 or len(model.bounding_boxes) < 2:
        # A single-segment model spans itself; there is nothing to crowd.
        return []
    found = []
    for index, box in enumerate(model.bounding_boxes):
        span = max(box[3] - box[0], box[4] - box[1], box[5] - box[2])
        if span > track * OVERSIZED_BOX:
            found.append(index)
    return found


def check_collision_pressure(model: LevelModel) -> List[str]:
    """Warnings about geometry that will crowd the collision candidate list."""
    oversized = oversized_segments(model)
    if len(oversized) <= OVERSIZED_BUDGET:
        return []
    return [
        "%d segments have a bounding box spanning more than half the track "
        "(%s). Collision considers at most %d segments at a time, chosen by box "
        "overlap, so these hold slots everywhere and can push the geometry a "
        "racer is standing on out of the list. The symptom is falling through "
        "the floor far from these segments. No retail track has more than two"
        % (len(oversized), ", ".join(str(i) for i in oversized[:8]),
           COLLISION_CANDIDATES)
    ]


# ---------------------------------------------------------------------------
# Re-segmentation
# ---------------------------------------------------------------------------

#: Triangles a rebuilt segment aims for. Retail averages roughly 48 on Ancient
#: Lake and 15 on Pirate Lagoon, so this sits inside the range the game's own
#: data uses rather than above it.
SEGMENT_TRIANGLE_TARGET = 48

#: Splitting stops here however many triangles are left. A segment holding one
#: triangle costs a bounding box, a BSP node and a row of the PVS, and the PVS
#: grows with the square of the segment count.
MIN_SEGMENT_TRIANGLES = 8


def resegment(model: LevelModel,
              target: int = SEGMENT_TRIANGLE_TARGET,
              batch_target: int = BATCH_VERTEX_TARGET) -> int:
    """Partition a model's geometry into fresh segments. Returns the count.

    This is what a track grown outside its inherited segmentation needs. The
    three steps before it edit a segmentation; none of them makes one, so a
    shape extruded a long way stays in the segment it grew from and stretches
    that segment's box across the map - which crowds the ten-slot collision
    candidate list and inherits visibility bits that say nothing about the new
    ground.

    Everything one-per-segment is regenerated together, because a changed
    segment count invalidates all of it: bounding boxes, the BSP, and the PVS,
    whose size is fixed by the count. The layout is rebuilt too, so a caller
    goes straight to :func:`level_model_encoder.encode` and must **not** call
    :func:`rebuild` afterwards.

    Two deliberate choices:

    * **The PVS comes out all-visible.** Computing real visibility needs a
      solution this module does not have; every bit set is conservative -
      nothing is culled that should be drawn - and costs draw calls rather
      than correctness.
    * **Identity is destroyed.** Which segment a triangle belongs to changes,
      so any ``(segment, vertex)`` mapping a caller holds is stale. That is why
      this is explicit rather than something an export does quietly.
    """
    faces, positions, colours = _dissolve(model)
    if not faces:
        return len(model.segments)

    # A model built from nothing has no bounds yet, and without them the split
    # below cannot divide space: it cuts by count into slices that each span
    # the track, and every one of them then crowds collision. The first island
    # exported from a mesh came out as eight segments all oversized.
    if not model.bounds or not any(model.bounds):
        used = [positions[index] for face in faces for index in face.vertices]
        xs, ys, zs = zip(*(position[:3] for position in used))
        model.bounds = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

    # Comfortably inside OVERSIZED_BOX, because a box is measured from the
    # segment's vertices while the split is made on face centroids, and a
    # triangle reaches past its own centre.
    span = max(model.bounds[1] - model.bounds[0],
               model.bounds[3] - model.bounds[2],
               model.bounds[5] - model.bounds[4]) if model.bounds else 0
    max_extent = span * OVERSIZED_BOX * 0.5 if span > 0 else None
    groups = _partition(faces, positions, max(1, target), max_extent)

    model.segments = []
    for index, group in enumerate(groups):
        segment = Segment(index)
        local, pool_positions, pool_colours = {}, [], []
        rekeyed = []
        for face in group:
            mapped = []
            for pool_index in face.vertices:
                if pool_index not in local:
                    local[pool_index] = len(pool_positions)
                    pool_positions.append(positions[pool_index])
                    pool_colours.append(colours[pool_index])
                mapped.append(local[pool_index])
            rekeyed.append(Face(face.key, mapped, face.uvs, face.flags))
        rebatch_segment(segment, rekeyed, pool_positions, pool_colours,
                        batch_target)
        model.segments.append(segment)

    _rebuild_boxes(model)
    model.bsp = build_bsp(model.bounding_boxes)
    model.pvs = b"\xff" * pvs_size(len(model.segments))
    rebuild(model)
    return len(model.segments)


#: ``LevelModel`` fields with no reader anywhere in the decomp that are still
#: given retail's value. Zero would almost certainly do - but the decomp still
#: has unmatched assembly, so a grep for readers is evidence and not proof, and
#: matching what every retail track ships costs nothing. If a reader is ever
#: found, this is the one place to change.
BLANK_HEADER_UNKNOWNS = {
    "unk1C": 1,
    "unk26": 9,
}


def blank_model(textures: Sequence = (), bounds=None) -> LevelModel:
    """A level model for a track with no ancestor, ready to be filled.

    The three steps of Phase 2 all start from a decoded retail model, so every
    field nobody sets was inherited. A model built from nothing has no such
    source, and something has to decide what the rest of the segment struct
    holds. This does, so that no caller has to name those fields - the same
    reason ``SurfaceType`` belongs in the catalogue rather than in a table
    copied into a UI module.

    Hand the result faces through :func:`rebatch_segment` and then call
    :func:`resegment`; the layout, the boxes, the BSP and the PVS all follow.

    **What the answer rests on.** The loader in ``tracks.c`` assigns ``unk10``,
    ``collisionPlanes``, ``unk30``, ``unk32``, ``unk34`` and ``unk38`` right
    after inflating, so whatever the file holds there is discarded - which is
    why retail's values look like leftovers, and why zero is fine. Of the rest:

    * ``unk8``, ``unk28``, ``unk2A``, ``unk3C`` and the padding have **no reader
      anywhere in the decomp**. Zero.
    * ``unk2C`` is read only by ``calc_dyn_lighting_for_level_segment``, which
      is marked ``UNUSED`` and exists only as unmatched assembly. Zero.
    * ``hasWaves`` is **not** scratch. ``tracks.c`` tests it to decide whether a
      segment carries water, so it is authored - and zero, no water, is both
      the right default for a new track and what every Ancient Lake segment
      holds.
    * ``collisionFacets`` is fixed up from the file, so the asset has to reserve
      its array. :func:`rebuild` does that; nothing here has to.
    """
    model = LevelModel()
    model.textures = list(textures)
    model.segments = [Segment(0)]
    model.bounding_boxes = [tuple(bounds) if bounds else (0, 0, 0, 0, 0, 0)]
    model.bounds = (0, 0, 0, 0, 0, 0)
    model.pvs = b"\xff" * pvs_size(1)
    model.bsp = build_bsp(model.bounding_boxes)
    for name, value in BLANK_HEADER_UNKNOWNS.items():
        setattr(model, name, value)
    # A new track has no animated textures until something says otherwise, and
    # the count is real rather than decorative - the renderer walks that many.
    model.animated_texture_count = 0
    rebuild(model)
    return model


def _dissolve(model: LevelModel):
    """Every face in the model, against one flat pool of vertices.

    A :class:`BatchKey`'s ``serial`` is only unique inside its own segment, so
    it is widened to ``(segment, batch)`` here. Without that, batches from two
    different source segments would collide and be merged into one draw call
    the moment they landed in the same new segment.
    """
    faces: List[Face] = []
    positions: List = []
    colours: List = []
    seen: Dict[Tuple, int] = {}

    for segment in model.segments:
        local, pool, pool_colours = _dissolve_segment(segment)
        offset = {}
        for index, position in enumerate(pool):
            colour = tuple(pool_colours[index])
            key = (tuple(position), colour)
            if key not in seen:
                seen[key] = len(positions)
                positions.append(tuple(position))
                colours.append(colour)
            offset[index] = seen[key]
        for face in local:
            widened = BatchKey(
                face.key.texture_index, face.key.flags, face.key.misc,
                face.key.texture_offset, face.key.vertex_override,
                face.key.opaque,
                None if face.key.serial is None
                else (segment.index << 16) | (face.key.serial & 0xFFFF),
            )
            faces.append(Face(widened, [offset[v] for v in face.vertices],
                              face.uvs, face.flags))
    return faces, positions, colours


def _dissolve_segment(segment: Segment):
    faces, positions, colours = decompose(segment)
    return faces, positions, colours


def _centroid(face: Face, positions) -> Tuple[float, float, float]:
    points = [positions[index] for index in face.vertices]
    return tuple(sum(point[axis] for point in points) / 3.0 for axis in range(3))


def _partition(faces: List[Face], positions, target: int,
               max_extent: Optional[float] = None) -> List[List[Face]]:
    """Recursive median split on the widest axis, by face centroid.

    Median rather than midpoint, so a dense corner of a track does not produce
    one enormous segment and a row of empty ones. The BSP built over the result
    is no worse than retail's: walking every retail model, 199 of 3911
    segment-to-split relationships already have the segment outside its own
    half-space, so exact containment is not a property the format has.

    **Two stop conditions, and the spatial one is the point.** Splitting only on
    triangle count leaves a sparse region as one segment stretched across the
    map - forty triangles spanning six thousand units, which is exactly the
    shape that crowds the collision candidate list and the shape re-segmenting
    exists to remove. So a group keeps splitting while it is either too
    populous or too wide.

    What cannot be fixed here is a segment made large by a single triangle
    larger than ``max_extent``. Splitting never separates a triangle from
    itself, so that box stays big; the honest limit is one face.
    """
    if len(faces) <= 1:
        return [faces]

    centroids = [_centroid(face, positions) for face in faces]
    extent = max(max(c[axis] for c in centroids) - min(c[axis] for c in centroids)
                 for axis in range(3))
    too_many = len(faces) > max(target, MIN_SEGMENT_TRIANGLES)
    # The spatial condition stops at the floor too. Splitting a handful of
    # triangles because they are spread out trades one wide segment for several
    # nearly as wide - a box is a fraction of the whole model, so on a small
    # model every piece is a large fraction of it however finely it is cut, and
    # more segments is strictly worse: each one costs a box, a BSP node and a
    # row of the PVS, and only ten can be collision candidates at once.
    too_wide = (max_extent is not None and extent > max_extent
                and len(faces) > MIN_SEGMENT_TRIANGLES)
    if not (too_many or too_wide):
        return [faces]

    axis = max(range(3), key=lambda a: (max(c[a] for c in centroids)
                                        - min(c[a] for c in centroids)))
    order = sorted(range(len(faces)), key=lambda i: centroids[i][axis])
    middle = len(order) // 2
    left = [faces[i] for i in order[:middle]]
    right = [faces[i] for i in order[middle:]]
    if not left or not right:
        return [faces]
    return (_partition(left, positions, target, max_extent)
            + _partition(right, positions, target, max_extent))


def _rebuild_boxes(model: LevelModel) -> None:
    """Boxes from the new segments, and the header bounds from all of them."""
    from . import level_model_edit

    model.bounding_boxes = []
    for segment in model.segments:
        box = level_model_edit.segment_box(segment)
        model.bounding_boxes.append(box or (0, 0, 0, 0, 0, 0))
    bounds = level_model_edit.model_bounds(model)
    if bounds is not None:
        model.bounds = bounds


def build_bsp(boxes: Sequence[Sequence[int]]) -> List[Tuple[int, int, int, int, int]]:
    """A BSP over the segment boxes, one node per segment.

    The format has **no separate leaf nodes**: the array holds one node per
    segment, ``segmentIndex`` names the segment the node *is*, and ``leftNode``
    / ``rightNode`` are child indices with -1 for no subtree. So this is a
    binary search tree over the segments, and every segment appears exactly
    once - which all 110 retail models satisfy and which the gate checks.

    ``splitValue`` is the node's own segment's lower edge on the split axis,
    which is what 1854 of retail's 2192 reachable nodes do.
    """
    nodes: List[Optional[Tuple[int, int, int, int, int]]] = [None] * len(boxes)
    if not boxes:
        return []

    def build(indices: List[int]) -> int:
        if not indices:
            return -1
        axis = 0
        best = -1.0
        for candidate in range(3):
            lows = [boxes[i][candidate] for i in indices]
            highs = [boxes[i][candidate + 3] for i in indices]
            spread = max(highs) - min(lows)
            if spread > best:
                best, axis = spread, candidate
        indices.sort(key=lambda i: boxes[i][axis])
        middle = len(indices) // 2
        here = indices[middle]
        left = build(indices[:middle])
        right = build(indices[middle + 1:])
        nodes[here] = (left, right, axis, here, int(boxes[here][axis]))
        return here

    root = build(list(range(len(boxes))))
    if root != 0:
        # Node 0 is the root the game descends from, so the tree is renumbered
        # rather than the game asked to start elsewhere.
        order = [root] + [i for i in range(len(boxes)) if i != root]
        moved = {old: new for new, old in enumerate(order)}
        renumbered: List[Tuple[int, int, int, int, int]] = [None] * len(boxes)
        for old, node in enumerate(nodes):
            if node is None:
                continue
            left, right, axis, segment_index, split = node
            renumbered[moved[old]] = (
                moved.get(left, -1) if left >= 0 else -1,
                moved.get(right, -1) if right >= 0 else -1,
                axis, segment_index, split,
            )
        nodes = renumbered
    return [node or (-1, -1, 0, 0, 0) for node in nodes]


def headroom_triangles(model: LevelModel) -> int:
    """How many more collidable triangles fit, which is the unit an author adds.

    One costs 16 bytes as a ``Triangle``, 8 as its facet reservation, 2 in
    ``unk10`` and 64 in collision planes. A triangle that opts out of collision
    costs 26 rather than 90, so this is the pessimistic count.
    """
    return max(0, (BUDGET - runtime_size(model)) // 90)
