# E04-S01 — Render backend interface

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E01-S02, E00-S05 |
| **Blocks** | E04-S02, E04-S08, E05-S01 |

## Context

The project already has a high-level rendering abstraction:
`ultramodern::renderer::RendererContext`, with `send_dl`, `update_screen`,
`update_config` and `valid`. Two implementations exist — `RT64Renderer` and
`DiagnosticRenderer` — and a third, Glide, will join them.

But that interface is too high for what we need. It receives a raw RSP task and
leaves the implementation to do all the work. Today, `F3DDKRRT64Bridge` decodes the
display list **directly into RT64's structures**: it calls `RT64::State`, manipulates
`RT64::DisplayList`s, registers RT64 workload identities. The decoder and the render
engine are welded together.

They have to be separated, and hence a second, lower interface introduced: a backend
that receives already-transformed primitives and an abstract render state. The F3DDKR
decoder then becomes independent of the backend, and two implementations can consume
it: Glide, and a reference software rasteriser (E04-S08) that will serve as the
comparison oracle.

## Objective

To define `platform/render/backend.h`: the interface the F3DDKR decoder drives, and
that both Glide and the software rasteriser implement.

## Scope

**In:** the interface's definition, its data structures, its documentation.

**Out:** any implementation. This is a design ticket, and its deliverable is a
contract.

## Work

1. Read `f3ddkr_rt64.cpp` (39 KB) and record exactly what the decoder asks of the
   render engine — not what a render engine offers in general. The list of primitives
   to produce is read off the handlers declared by `f3ddkr_rt64.hpp`: matrices,
   vertices, triangles, filled rectangles, texture image, block load, texture offset,
   state words.
2. Design the interface around what Glide can do, since that is the hardest
   constraint. In particular, the backend receives vertices **already projected into
   screen coordinates**: no 3dfx card transforms. Transformation, lighting and
   clipping stay on the decoder's side (E04-S03, E04-S05).
3. Define the vertex: screen position, depth, colour, texture coordinates per texture
   unit. Align it with the structure Glide expects, to avoid a per-vertex copy — on a
   Pentium II, a per-vertex format conversion is a real cost.
4. Define the render state as a block of values, not as a series of calls: combiner,
   blending mode, depth test, alpha test, fog, bound texture, filtering, wrapping. A
   block lets the backend compare against the current state and emit only the changes
   — that is what makes state tracking cheap.
5. Define texture management as a handle cache: the decoder supplies a decoded
   texture and a key, the backend returns a handle and manages its placement in
   texture memory on its own (E05-S02).
6. Define a frame's cycle: begin, drawing sequences, end, presentation.
7. Write the interface's documentation, indicating for each element what Glide can do
   natively and what will have to be emulated. It is that document which will keep us
   from designing an interface Glide cannot honour.

## Acceptance criteria

- [x] `platform/render/backend.h` defines the complete interface.
- [x] Every element is justified by a real need recorded in `f3ddkr_rt64.cpp`, not by
      generality. The survey produced a constraint that decides the shape: **the DKR
      vertex carries no texture coordinates** — its ten bytes are `x, y, z` as signed
      16-bit and `r, g, b, a` as bytes — and the `s, t` arrive **per corner, at
      triangle time**. An indexed-vertex interface would therefore be wrong here; the
      expansion happens on the decoder's side.
- [x] The vertex structure avoids a per-vertex conversion towards Glide — and that is
      not merely documented: `backend_layout_check.c` checks **at compile time** that
      every field sits at `GrVertex`'s offset.
- [x] The render state is a comparable block, allowing differential emission. Also
      checked: the check refuses any padding, which would have `memcmp` comparing
      indeterminate bytes.
- [x] The interface is manifestly implementable by Glide: every element is annotated
      "NATIVE" or "TO EMULATE", with the corresponding Glide call. Only one falls in
      the second category — the filled rectangle, which Glide does not know and which
      the backend builds out of two triangles — plus the combiner, whose translation
      is E05-S03's work.
- [x] An empty implementation compiles and links — `backend_null.c`, which counts
      what it receives: a decoder that emits nothing and a backend that draws nothing
      look very much alike seen from the screen.
- [x] The interface exposes no type belonging to RT64, SDL2 or ImGui.

## Risks

An over-generic interface is paid for twice: when writing the Glide backend, which has
to emulate what the card does not do, and at run time, in per-primitive overhead.
Here, the interface must marry the card rather than abstract it.

An over-narrow interface, for its part, will prevent the reference software rasteriser
from serving as an oracle. The balance is found by writing both implementations in
one's head, not just one.

## References

- `runtime-recomp/src/game/f3ddkr_rt64.hpp` — the microcode's fourteen command
  handlers
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — the current decoder, welded to RT64
- `ultramodern/renderer_context.hpp` — the high interface, kept
- `docs/F3DDKR.md`
