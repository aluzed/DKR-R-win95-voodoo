"""Gate for the layout builder: adding and removing geometry.

Phase 2 step 3 of docs/BLENDER_ADDON_PLAN.md. Steps 1 and 2 kept every array
where it was found, which is what made them cheap and safe. This is the step
that gives that up, so it needs its own gate.

Retail's own layout is not reproducible by rule - the padding between arrays
runs 0, 4, 8, 10, 12 and on to 770 bytes with no pattern - so "rebuild and
compare to the file" is not available here the way it was for step 1. What is
available is stronger than it sounds:

* **The re-batcher is the identity on untouched data.** Pulling a segment apart
  into loose faces and putting it back must give the same batches, the same
  vertex order and the same triangles - not merely an equivalent segment. All
  2292 retail segments, both revisions. That is what says the decomposition
  loses nothing, and it is only true because a face remembers which batch it
  came from; grouping on the rendering fields alone merges the 411 segments
  that hold two batches agreeing on all of them.
* **A rebuilt layout round trips its content.** Rebuild, encode, decode, and
  every value has to come back - vertices, colours, triangles, UVs, batches,
  boxes, the BSP and the PVS. Only the offsets are allowed to differ.
* **The window invariant is a postcondition.** The Blender importer resolves a
  face to its batch through the batch windows, so a gap or an overlap
  mis-assigns render flags with nothing raised. Checked on retail and on
  everything this module builds.

The four retail models holding a degenerate triangle go through this path
untouched, because nothing here involves Blender - which is the point of
gating the encoder separately from the addon.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_level_model_layout.py
"""

from __future__ import annotations

import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import (  # noqa: E402
    level_model, level_model_encoder, level_model_layout,
)

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")

#: Models carrying a triangle that names the same vertex twice. Blender cannot
#: hold one, so the addon round trip differs by exactly these; this path does
#: not go through Blender and must carry them.
DEGENERATE_MODELS = ("smokey", "darkmoon_caverns", "bluey", "temple_track")


def find_level_models():
    pattern = os.path.join(VANILLA, "*", "levels", "models", "*", "*.bin")
    return sorted(glob.glob(pattern))


def load(path):
    with open(path, "rb") as handle:
        return level_model.parse(level_model.decompress(handle.read()))


def segment_snapshot(segment):
    """Everything about a segment that is not its position in the file."""
    return (
        list(segment.vertices), list(segment.colours), list(segment.triangles),
        list(segment.uvs), segment.opaque_batches,
        [(b.texture_index, b.flags, b.misc, b.texture_offset, b.vertex_override,
          b.vertex_offset, b.face_offset, b.vertex_count, b.face_count)
         for b in segment.batches],
    )


def model_snapshot(model):
    return (
        [segment_snapshot(s) for s in model.segments],
        [tuple(b) for b in model.bounding_boxes],
        list(model.bsp),
        [(t.texture_id, t.raw_width, t.raw_height, t.format, t.surface_type)
         for t in model.textures],
        tuple(model.bounds),
        model.pvs,
    )


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

def check_rebatch_identity(path):
    """Decomposing and rebatching an untouched segment must change nothing."""
    model = load(path)
    for segment in model.segments:
        before = segment_snapshot(segment)
        faces, positions, colours = level_model_layout.decompose(segment)
        level_model_layout.rebatch_segment(segment, faces, positions, colours)
        if segment_snapshot(segment) != before:
            after = segment_snapshot(segment)
            return (
                "segment %d changed: %d batches and %d vertices became %d and %d"
                % (segment.index, len(before[5]), len(before[0]),
                   len(after[5]), len(after[0]))
            )
    return None


def check_windows_on_retail(path):
    """Retail already satisfies the invariant the re-batcher has to maintain."""
    problems = level_model_layout.check_windows(load(path))
    return problems[0] if problems else None


def check_rebuilt_round_trip(path):
    """A rebuilt layout must carry every value through encode and decode."""
    model = load(path)
    before = model_snapshot(model)

    level_model_layout.rebuild(model)
    problems = level_model_layout.check_windows(model)
    if problems:
        return "rebuilding broke the window invariant: %s" % problems[0]

    payload = level_model_encoder.encode(model)
    if len(payload) != model.blob_size:
        return "encoded %d bytes, layout said %d" % (len(payload), model.blob_size)
    if model.model_size != model.blob_size:
        return ("modelSize is %d but the blob is %d; the loader allocates its "
                "scratch from modelSize and would write over the model"
                % (model.model_size, model.blob_size))

    again = level_model.parse(payload)
    after = model_snapshot(again)
    if after == before:
        return None

    for index, (was, now) in enumerate(zip(before[0], after[0])):
        if was != now:
            names = ("vertices", "colours", "triangles", "uvs",
                     "opaque_batches", "batches")
            for part, name in enumerate(names):
                if was[part] != now[part]:
                    return "segment %d %s did not survive the rebuild" % (index, name)
            return "segment %d changed" % index
    for part, name in enumerate(("segments", "bounding boxes", "bsp", "textures",
                                 "bounds", "pvs")):
        if before[part] != after[part]:
            return "%s did not survive the rebuild" % name
    return "the model changed but no differing part was found"


def check_facets_match_retail(path):
    """The generated collision facets are the ones retail ships.

    They are authored adjacency the loader reads, not scratch it fills, so a
    rebuilt layout has to write them - and writing them wrongly would bound
    every triangle wrongly. The rule is held to retail's own data: 99.6% of
    all facets match, and the lowest single model, Snowflake Mountain Hub, is
    at 91% - 167 edges it leaves as walls where the game's rule joins them, for
    a reason not found yet. Ninety per cent per model is the floor.
    """
    with open(path, "rb") as handle:
        blob = level_model.decompress(handle.read())
    model = level_model.parse(blob)
    same = total = 0
    for segment in model.segments:
        mine = level_model_layout.collision_facets(segment)
        for face, triangle in enumerate(segment.triangles):
            if triangle[0] & level_model_layout.TRI_FLAG_NO_COLLISION:
                continue
            at = segment.collision_facets_ptr + face * level_model_layout.FACET_SIZE
            total += 1
            same += blob[at:at + 8] == mine[face * 8:face * 8 + 8]
    if total and same < 0.90 * total:
        return "only %d of %d facets match retail" % (same, total)
    return None


def check_rebuilt_facets(path):
    """A rebuilt layout writes the facets rather than leaving them zeroed."""
    model = load(path)
    level_model_layout.rebuild(model)
    payload = level_model_encoder.encode(model)
    for segment in level_model.parse(payload).segments:
        start = segment.collision_facets_ptr
        written = payload[start:start + len(segment.triangles) * 8]
        if written != level_model_layout.collision_facets(segment):
            return "segment %d's collision facets were not written" % segment.index
    return None


def check_added_triangle(path):
    """Adding a face must be refused in place and accepted by the builder."""
    model = load(path)
    index = next((i for i, s in enumerate(model.segments)
                  if s.batches and len(s.triangles) > 1), None)
    if index is None:
        return None
    segment = model.segments[index]

    faces, positions, colours = level_model_layout.decompose(segment)
    faces.append(level_model_layout.Face(
        faces[0].key, faces[0].vertices, faces[0].uvs, faces[0].flags
    ))
    level_model_layout.rebatch_segment(segment, faces, positions, colours)

    if len(segment.triangles) != len(faces):
        return ("after adding a face the segment holds %d triangles, expected %d"
                % (len(segment.triangles), len(faces)))

    # The in-place encoder must refuse this, or step 1's guarantee is hollow.
    stale = load(path)
    stale.segments[index].triangles.append((0, 0, 1, 2))
    if not level_model_encoder.check_layout(stale):
        return "the in-place encoder accepted a segment whose triangle count grew"

    level_model_layout.rebuild(model)
    payload = level_model_encoder.encode(model)
    again = level_model.parse(payload)
    if len(again.segments[index].triangles) != len(faces):
        return "the added face did not survive encode and decode"
    problems = level_model_layout.check_windows(again)
    if problems:
        return "adding a face broke the window invariant: %s" % problems[0]
    return None


def check_budget(path):
    """Every retail model fits, and reports headroom rather than a negative."""
    model = load(path)
    size = level_model_layout.runtime_size(model)
    if size > level_model_layout.BUDGET:
        return ("needs %d bytes of the %d budget at load; the formula or the "
                "budget is wrong" % (size, level_model_layout.BUDGET))
    if level_model_layout.headroom_triangles(model) <= 0:
        return "reports no headroom at all, which no retail track should"
    return None


def check_degenerate_carried(path):
    """The four models Blender cannot represent must still go through here."""
    stem = os.path.splitext(os.path.basename(path))[0]
    if not any(stem.startswith(name) for name in DEGENERATE_MODELS):
        return None
    model = load(path)
    degenerate = [
        (s.index, i) for s in model.segments
        for i, t in enumerate(s.triangles)
        if t[1] == t[2] or t[2] == t[3] or t[1] == t[3]
    ]
    if not degenerate:
        return None
    level_model_layout.rebuild(model)
    again = level_model.parse(level_model_encoder.encode(model))
    for segment_index, face in degenerate:
        triangle = again.segments[segment_index].triangles[face]
        if not (triangle[1] == triangle[2] or triangle[2] == triangle[3]
                or triangle[1] == triangle[3]):
            return ("the degenerate triangle at segment %d face %d did not "
                    "survive the rebuild" % (segment_index, face))
    return None


def check_range_refusals(path):
    """A coordinate or UV the format cannot hold must say so in track terms."""
    model = load(path)
    segment = next((s for s in model.segments if s.vertices and s.uvs), None)
    if segment is None:
        return None

    original = segment.uvs[0]
    segment.uvs[0] = ((40000, 0), (0, 0), (0, 0))
    try:
        level_model_encoder.encode(model)
    except level_model_encoder.LevelModelEncodeError as error:
        if "texel" not in str(error):
            return "a UV past s16 was refused without explaining the ceiling"
    except Exception as error:  # noqa: BLE001
        return ("a UV past s16 raised %s rather than a LevelModelEncodeError"
                % type(error).__name__)
    else:
        return "a UV past s16 was accepted"
    segment.uvs[0] = original

    segment.vertices[0] = (40000, 0, 0)
    try:
        level_model_encoder.encode(model)
    except level_model_encoder.LevelModelEncodeError:
        pass
    except Exception as error:  # noqa: BLE001
        return ("a vertex past s16 raised %s rather than a "
                "LevelModelEncodeError" % type(error).__name__)
    else:
        return "a vertex past s16 was accepted"
    return None


def check_collision_pressure(path):
    """Retail must be quiet, and a track of giant boxes must not be."""
    model = load(path)
    if level_model_layout.check_collision_pressure(model):
        return ("retail trips the collision pressure warning, so the threshold "
                "is wrong: %d oversized segments"
                % len(level_model_layout.oversized_segments(model)))

    if len(model.bounding_boxes) <= level_model_layout.OVERSIZED_BUDGET:
        return None
    # Stretch every box across the track and the warning has to appear.
    lower = (model.bounds[0], model.bounds[2], model.bounds[4])
    upper = (model.bounds[1], model.bounds[3], model.bounds[5])
    model.bounding_boxes = [lower + upper for _ in model.bounding_boxes]
    if not level_model_layout.check_collision_pressure(model):
        return "a model whose every segment spans the track raised no warning"
    return None


def check_resegment(path):
    """Re-segmenting must keep every triangle and fix the crowding."""
    blob, model = None, load(path)
    before_triangles = sum(len(s.triangles) for s in model.segments)
    before_positions = {tuple(v) for s in model.segments for v in s.vertices}
    if before_triangles == 0:
        return None

    count = level_model_layout.resegment(model)
    if count != len(model.segments):
        return "resegment reported %d segments but built %d" % (
            count, len(model.segments)
        )

    after_triangles = sum(len(s.triangles) for s in model.segments)
    if after_triangles != before_triangles:
        return "%d triangles became %d" % (before_triangles, after_triangles)
    after_positions = {tuple(v) for s in model.segments for v in s.vertices}
    if after_positions != before_positions:
        lost = len(before_positions - after_positions)
        gained = len(after_positions - before_positions)
        return "vertex positions changed: %d lost, %d invented" % (lost, gained)

    problems = level_model_layout.check_windows(model)
    if problems:
        return "batch windows broken after resegment: %s" % problems[0]

    # Collision pressure is the harm, and it is what has to be gone. "No
    # oversized segment at all" is the wrong bar: a box is measured as a
    # fraction of the whole model, so on a small model every piece is a large
    # fraction of it however finely it is cut, and a segment made large by one
    # large triangle cannot be split away from itself.
    pressure = level_model_layout.check_collision_pressure(model)
    if pressure:
        return "resegment left collision pressure: %s" % pressure[0]

    if len(model.pvs) != level_model.pvs_size(len(model.segments)):
        return "the PVS was not resized with the segment count"

    payload = level_model_encoder.encode(model)
    again = level_model.parse(payload)
    if sum(len(s.triangles) for s in again.segments) != before_triangles:
        return "triangles were lost through encode and decode"
    return None


def check_resegment_bsp(path):
    """The generated tree must satisfy the invariant retail satisfies."""
    model = load(path)
    if sum(len(s.triangles) for s in model.segments) == 0:
        return None
    level_model_layout.resegment(model)

    if len(model.bsp) != len(model.segments):
        return "the BSP holds %d nodes for %d segments" % (
            len(model.bsp), len(model.segments)
        )

    seen, stack = set(), [0]
    named = []
    while stack:
        node = stack.pop()
        if node in seen or not 0 <= node < len(model.bsp):
            continue
        seen.add(node)
        left, right, split_type, segment_index, _split = model.bsp[node]
        if split_type > 2:
            return "node %d has split axis %d" % (node, split_type)
        if not 0 <= segment_index < len(model.segments):
            return "node %d names segment %d" % (node, segment_index)
        named.append(segment_index)
        stack += [left, right]

    if len(seen) != len(model.bsp):
        return "%d of %d nodes are unreachable from the root" % (
            len(model.bsp) - len(seen), len(model.bsp)
        )
    if len(set(named)) != len(named):
        return "a segment is named by two nodes; retail never repeats one"
    if set(named) != set(range(len(model.segments))):
        return "%d segments are named by no node" % (
            len(model.segments) - len(set(named))
        )
    return None


def check_resegment_relieves_pressure(path):
    """The case re-segmenting exists for: geometry grown out of its segment.

    Whole batches are moved a long way, which is what extruding does - each
    triangle stays its own size, but the segment it belongs to now spans the
    map. That is the shape that crowds the ten-slot collision candidate list,
    and re-segmenting has to be what removes it. Building the failure here
    rather than only checking retail is what makes this a demonstration instead
    of a regression guard.
    """
    model = load(path)
    if len(model.segments) < 8:
        return None
    stretched = [i for i in range(0, len(model.segments), 3)][:7]
    moved = False
    for index in stretched:
        segment = model.segments[index]
        for batch in segment.batches[:max(1, len(segment.batches) // 2)]:
            for slot in range(batch.vertex_offset,
                              batch.vertex_offset + batch.vertex_count):
                if slot >= len(segment.vertices):
                    break
                x, y, z = segment.vertices[slot]
                segment.vertices[slot] = (
                    max(-32768, min(32767, x + 7000)), y,
                    max(-32768, min(32767, z - 7000)),
                )
                moved = True
    if not moved:
        return None

    from dkr_track_editor import level_model_edit
    level_model_edit.recompute_bounds(model)
    if not level_model_layout.check_collision_pressure(model):
        return None  # this model did not end up crowded; nothing to relieve

    before = sum(len(s.triangles) for s in model.segments)
    level_model_layout.resegment(model)
    if level_model_layout.check_collision_pressure(model):
        return ("re-segmenting left %d oversized segments on geometry it is "
                "meant to fix" % len(level_model_layout.oversized_segments(model)))
    if sum(len(s.triangles) for s in model.segments) != before:
        return "triangles were lost while relieving pressure"
    if level_model_layout.check_windows(model):
        return "batch windows broken while relieving pressure"
    level_model_encoder.encode(model)
    return None


def check_blank_model(_path=None):
    """A model built from nothing must be a valid model.

    Everything else here starts from a decoded retail model, so any field
    nobody sets was inherited. This is the one path with no ancestor, and it is
    what arbitrary-mesh import stands on.
    """
    textures = [level_model.TextureRef(12, 32, 32, 17, 1)]
    model = level_model_layout.blank_model(textures)
    if len(model.segments) != 1:
        return "a blank model came out with %d segments" % len(model.segments)
    if model.model_size != model.blob_size:
        return "modelSize %d does not match the blob %d" % (
            model.model_size, model.blob_size
        )

    key = level_model_layout.BatchKey(0, 0, 0, 0, 0, True, 0)
    positions = [(0, 0, 0), (1000, 0, 0), (1000, 0, 1000), (0, 0, 1000)]
    colours = [(255, 255, 255, 255)] * 4
    uvs = ((0, 0), (1024, 0), (1024, 1024))
    faces = [level_model_layout.Face(key, (0, 1, 2), uvs, 0),
             level_model_layout.Face(key, (0, 2, 3), uvs, 0)]
    level_model_layout.rebatch_segment(model.segments[0], faces, positions,
                                       colours)
    level_model_layout.resegment(model)

    again = level_model.parse(level_model_encoder.encode(model))
    if sum(len(s.triangles) for s in again.segments) != 2:
        return "the two triangles did not survive encode and decode"
    if level_model_layout.check_windows(again):
        return "a blank model produced broken batch windows"
    if len(again.pvs) != level_model.pvs_size(len(again.segments)):
        return "the PVS is not one bitmask per segment"
    if len(again.bsp) != len(again.segments):
        return "the BSP does not hold one node per segment"
    if tuple(again.bounds) != (0, 1000, 0, 0, 0, 1000):
        return "bounds came out %r, not the geometry's own" % (again.bounds,)
    if again.textures[0].surface_type != 1:
        return "the texture's surface type was not carried through"

    segment = again.segments[0]
    if segment.has_waves != 0:
        return ("hasWaves is authored, not scratch, and a new track has no "
                "water; it came out %d" % segment.has_waves)
    return None


CASES = (
    ("a model builds from nothing", check_blank_model),
    ("rebatch is the identity", check_rebatch_identity),
    ("retail satisfies the window rule", check_windows_on_retail),
    ("a rebuilt layout round trips", check_rebuilt_round_trip),
    ("an added face is built, not patched", check_added_triangle),
    ("every model fits the memory budget", check_budget),
    ("collision facets follow retail's rule", check_facets_match_retail),
    ("a rebuilt layout writes the facets", check_rebuilt_facets),
    ("degenerate triangles survive", check_degenerate_carried),
    ("out-of-range values are refused", check_range_refusals),
    ("collision pressure is calibrated", check_collision_pressure),
    ("resegment keeps every triangle", check_resegment),
    ("the generated BSP is well formed", check_resegment_bsp),
    ("resegment relieves crowding", check_resegment_relieves_pressure),
)


def main():
    models = find_level_models()
    if not models:
        print("no extracted level models found under %s" % VANILLA)
        return 0

    failures = []
    for label, case in CASES:
        bad = []
        for path in models:
            problem = case(path)
            if problem:
                bad.append((os.path.basename(path), problem))
        print("%-38s %s" % (label, "PASS" if not bad else "FAIL (%d)" % len(bad)))
        for name, problem in bad[:5]:
            print("    %s: %s" % (name, problem))
        failures += bad

    worst = max(
        (level_model_layout.runtime_size(load(p)) for p in models), default=0
    )
    print("")
    print("level models checked: %d" % len(models))
    print("worst runtime footprint: %d of %d budget (%.1f%%)"
          % (worst, level_model_layout.BUDGET,
             100.0 * worst / level_model_layout.BUDGET))
    if failures:
        print("%d case failure(s)" % len(failures))
        return 1
    print("the layout builder reproduces every model it takes apart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
