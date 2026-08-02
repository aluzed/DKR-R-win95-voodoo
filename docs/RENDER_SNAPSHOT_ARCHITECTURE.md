# Renderer snapshot ownership

Modern presentation uses two distinct ownership stages. Neither stage retains
a pointer into the game's live RDRAM after its owner releases that memory.

## Submission stage

The project Patch Pipeline applies
`patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`.
Every graphics task is queued with an owned 8 MiB RDRAM image and an `OSTask`
copy. The graphics worker retains that image until `RT64Renderer::send_dl`
returns. `RendererSnapshotScope` exposes it to the F3DDKR decoder and restores
both RT64 RDRAM pointers on every exit path.

This image is an isolated decode workspace rather than an interpolation
endpoint. The bridge uses `0x7FE000..0x7FFFFF` to translate Rare's packed
vertices, and RT64 can copy native framebuffer readback into the workspace.
Those writes are allowed because the CPU has already continued on live RDRAM;
they cannot feed back into simulation or corrupt a later graphics task.

## Decoded renderer stage

RT64 decodes the task synchronously into its owned `Workload` data: draw
commands and ranges, transforms, vertices, texture/load identities, viewport,
fog, lights, framebuffer storage, material/RDP state, and interpolation maps.
Its `WorkloadQueue` is a four-slot ring. A compile-time assertion prevents this
runtime from enabling Modern work against an RT64 revision with fewer than four
owned slots. RT64's cursor mutexes, condition variables, workload IDs, and
renderer/present fences control reuse.

The project therefore does not add a second competing renderer queue. Modern
semantic IDs are written while F3DDKR records the current RT64 workload; RT64
then pairs the two owned endpoint workloads. Missing history, scene changes,
identity failures, discontinuities, queue pressure, or snapshot audit failures
must present the newest authored endpoint without extrapolation.

## Semantic transform sidecar

DKR allocates object matrices from two fixed N64 frame heaps and reuses object
addresses after destruction. A physical matrix address is therefore not a
stable interpolation identity by itself. Patch Pipeline hooks at scene load,
object spawn/free, and the decompiled `render_object` boundary construct IDs
from the scene generation, object address, lifetime generation, object and
behaviour IDs, plus the matrix ordinal within that object. Bodies, limbs,
wheels, propellers, and other attachments consequently remain distinct.

No extra commands are written into DKR's fixed 4,500-command display-list
buffer. Instead, the host records address-to-ID bindings in a sidecar while the
game authors the frame. A hook at `gfxtask_run_xbus` freezes that sidecar in
the same FIFO order as the graphics task. `TaskIdentityScope` transfers the
matching immutable map to the graphics worker for exactly one F3DDKR decode.
This prevents a delayed worker from reading identities belonging to a newer
frame that reused the same N64 matrix heap.

Accurate mode never records or selects these IDs. Invalid object ranges,
unbalanced nesting, task-order mismatches, queue overflow, and 32-bit hash
collisions all resolve to `G_EX_ID_IGNORE`; a collision also removes bindings
already authored by the first colliding owner. These are presentation-only
fallbacks and never alter simulation memory.
