# F3DDKR renderer bridge

Diddy Kong Racing uses Rare's F3DDKR display-list microcode. DKR-R recompiles
the matching RSP microcode and translates the game-specific commands into RT64
workloads in `runtime-recomp/src/game/f3ddkr_rt64.cpp`.

The bridge validates matrix, vertex, triangle, texture and nested display-list
ranges before submission. Invalid data is rejected with a bounded error rather
than being allowed to address host memory. Presentation groups attach stable
semantic identities to moving objects, vehicle parts, billboards, shadows and
animated surfaces so RT64 can interpolate between owned workload endpoints.

The decoder reads an immutable 8 MiB task snapshot. It may use the snapshot's
reserved translation workspace, but it never writes renderer data back to live
simulation RDRAM. Accurate mode bypasses Modern semantic identities and retains
the original display cadence.

Direct edits to RT64 or generated RSP/CPU sources are forbidden. Renderer
dependency changes belong in `patches/rt64`; RSP and game hook changes belong in
the recomp policy and are regenerated through the Patch Pipeline.
