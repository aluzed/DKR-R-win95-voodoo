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
