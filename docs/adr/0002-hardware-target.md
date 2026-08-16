# ADR 0002 - Hardware target and Glide version

- **Status**: accepted
- **Date**: 2026-08-12
- **Ticket**: [E00-S05](../stories/E00-scoping/E00-S05-adr-hardware-target-glide.md)

## Context

"3dfx Voodoo compatible" names five generations of cards and two APIs. The choice
decides the rendering work directly — in particular the number of TMUs, which
determines whether two-texel combiners go through in one pass or two.

A constraint common to the whole range: **no hardware transformation**. The card
receives vertices already projected. The entire geometry pipeline stays on the
CPU, which is already the tight resource
([E00-S03](../research/cpu-budget.md)).

## Decision

| Item | Floor | Recommended |
|---|---|---|
| CPU | Pentium II 400 MHz | Pentium III 500 MHz and above |
| RAM | **64 MB** | 128 MB |
| 3dfx card | Voodoo 2, 8 MB, **2 TMUs** | Voodoo 2 12 MB or Voodoo 3 |
| API | **Glide 2.4x** (`glide2x.dll` 2.54) | the same |
| System | Windows 95 OSR2 or OSR2.5 | the same |
| Resolution | **640 × 480, 16-bit, double buffer + Z** | the same |

Every value is justified below by a measurement or a hardware constraint. None is
a matter of preference.

### RAM: 64 MB, and the figure is measured

[ADR 0003](0003-memory-budget.md) recorded on the machine: Windows 95 and its
drivers consume **15.6 MiB**, the 3dfx stack **872 KiB**, and **47.0 MiB** remain
available for a project budget of 32.8 MiB. On 32 MB only ~16 MiB would remain:
that target is not tenable and is not retained.

### Resolution: 640 × 480 in 16 bits, double buffered

The arithmetic is constrained by the card's frame buffer memory, which is separate
from the texture memory:

| Configuration | Requirement | Voodoo 2 8 MB (2 MB frame buffer) | Voodoo 2 12 MB (4 MB) |
|---|---:|---|---|
| Double buffer + Z | 1.76 MiB | ✅ 88 % used | ✅ 44 % |
| **Triple** buffer + Z | 2.34 MiB | ❌ **does not fit** | ✅ 59 % |

640 × 480 × 2 bytes = 614,400 bytes per buffer; three buffers (front, back,
depth) make 1.76 MiB.

**Triple buffering is therefore ruled out**, because it would exclude the 8 MB
Voodoo 2 which is the floor. That is no loss: triple buffering serves to smooth an
irregular frame rate, and E00-S03 announces a machine under strain — the latency
it adds would be paid without the benefit.

### TMUs: two required, two exploited, a single-TMU fallback

**Two TMUs are required at the floor.** A single TMU would impose a second pass on
every two-texel surface, hence a doubled fill budget — on a machine whose CPU is
already the limiting factor.

The rendering must nonetheless **stay correct on a single TMU**, through the
multipass fallback
([E05-S04](../stories/E05-glide/E05-S04-multitexture-two-tmus.md)): correct, not
fast. That is what lets a Voodoo 1 display the game without the project having to
support it.

### Texture memory: the game fits entirely resident

The neighbouring native port resolved each level down to its real textures
(`../../Diddy-Kong-Racing/docs/research/level-working-set.md`), over **65 levels
of which 62 are tracks and hubs** — that is, the same game and the same level set
as this port:

| Quantity | Measurement |
|---|---:|
| Total peak (worst track + permanent + grid) | **1,116 KB** |
| Peak with power-of-two padding | **1,225 KB** |

Set against the memory per TMU:

| TMU | Padded peak | Occupancy |
|---|---:|---:|
| 2 MB (Voodoo 2 8 MB) | 1.20 MiB | **60 %** |
| 4 MB (Voodoo 2 12 MB) | 1.20 MiB | 30 % |

**The worst level fits entirely in a 2 MB TMU**, with 40 % of headroom. Texture
memory management (E05-S02) can therefore aim at total residency per level rather
than a cache with eviction mid-race — which removes one source of complexity and
one source of stutter.

### API: Glide 2.4x

The ticket asked for an argument based on the real coverage of the open 3dfx
sources, and not on the commercial documentation of the period. The `README` of
the [sezero/glide](https://github.com/sezero/glide) repository gives 3dfx's
internal names:

```
sst1:  Voodoo Graphics
sst96: Voodoo Rush
cvg:   Voodoo 2
h3:    Voodoo Banshee/Voodoo 3
```

and the trees build:

| Tree | Targets present |
|---|---|
| `glide2x` | `sst1`, `cvg`, `h3` |
| `glide3x` | `sst1`, `cvg`, `h3`, `h5` |

**The received idea that Glide 2.4 is the only route to the Voodoo 1 is therefore
false**: `glide3x` has an `sst1` target. Conversely, `glide2x` covers the Voodoo
3. Both trees cover the whole of the chosen target, and the choice is not decided
on the hardware.

It is decided on three facts measured on the machine:

1. **Glide 2.54 is proved end to end.**
   [E09-S01](../stories/E09-qa/E09-S01-emulated-test-environment.md) displays a
   Gouraud triangle; `tools/win95/probes/` queries the hardware and measures the
   memory through the same DLL.
2. **Glide 3.x requires a valid HWND.** `tools/win95/probes/glide3_probe.c` loads
   `glide3x.dll`, finds all its exports, but `grSstWinOpen` refuses: "*need to use
   a valid window handle*". Glide 2.x accepts `0` and takes the screen full-screen.
   Choosing Glide 3 would therefore couple the rendering bring-up to
   [E06-S01](../stories/E06-platform/E06-S01-win32-window-and-message-loop.md),
   whereas today it can be independent of it.
3. **Both DLLs are installed** by 3dfx's reference driver for the Voodoo 2
   (`GLIDE2X.DLL` 398,848 B, `GLIDE3X.DLL` 425,472 B, both dated 11 October
   1998). The choice therefore carries no distribution cost.

**What Glide 3 would have brought**, and which must be recorded since it is what
would reopen the decision: `grVertexLayout`, which allows the vertex format to be
*declared* instead of filling a fixed 60-byte structure. That is exactly the trap
which cost time in E09-S01 — a red vertex coming out green because `ooz` and `a`
sit between the colours and `oow`. On a machine where the CPU is the limiting
factor, reducing the per-vertex work is not cosmetic.

**The API is therefore placed behind
[E04-S01](../stories/E04-hle-f3ddkr/E04-S01-render-backend-interface.md)'s backend
interface**, so that a Glide 3 backend can be added without touching the F3DDKR
decoder.

## What the test machine says about itself

`tools/win95/probes/glide_hwinfo.c` queries `grSstQueryHardware`, by the same path
the engine will use (E05-S01).

### Correction of 14 August 2026 — the machine really is a Voodoo 2

The previous version of this section concluded that 86Box's configuration file
"lied", Glide reporting type `0` while the file announced `type = 2`. **That
conclusion was wrong, and so was the method.**

There are two Voodoo sections in `86box.cfg`. The one 86Box reads carries the
instance suffix — `[3dfx Voodoo Graphics #1]` — and it said `type = 1`, that is
**Obsidian SB50 + Amethyst**, a two-TMU Voodoo 1. 86Box honoured it faithfully.
The other section, written by hand, announced `type = 2` and was simply never
read.

The machine is now configured on this ADR's **floor** card, and the settings
dialog — the only authoritative source, as E09-S01 had already established —
confirms it (the emulator's interface is in French here; the labels are
translated):

```text
Voodoo type            : 3Dfx Voodoo 2
Frame buffer memory    : 2 MB
Texture memory         : 2 MB
```

### And Glide 2.54 still reports type 0

`grSstQueryHardware`'s report on this machine, once the Voodoo 2 is in place
(quoted with the probe's current wording):

```text
Glide version : 2.54
boards detected : 1
board 0
  type          : 0 (Voodoo Graphics (Voodoo 1))
  frame buffer  : 2 MB
  FBI revision  : 261
  TMUs          : 2
  TMU 0 memory   : 2 MB
  TMU 1 memory   : 2 MB
```

The type stays `0`, and the FBI revision stays `261` — **exactly the same values
as on the Obsidian**. Only the memory sizes have changed.

**The consequence for E05-S01 is concrete: on this platform,
`grSstQueryHardware` does not allow a Voodoo 1 to be told from a Voodoo 2.**
Glide 2.x's `GrSstType` does not separate the two — `GR_SSTTYPE_VOODOO` covers
the family, and `glide2x` 2.54 *is* the Voodoo 2 driver. Run-time detection must
therefore rest on something other than the type: the number of TMUs and the memory
per TMU are usable; the exact model is not.

A reservation on scope: this report is the **emulation's**. On real hardware the
FBI revision differs between the two generations, and could discriminate. That is
to be checked in E09-S04, and it is one more reason not to rest the detection on
it.

### What this changes in E09-S04's weight

The previous version concluded that "the test machine does not exercise the paths
specific to the Voodoo 2", which increased the weight of validation on real
hardware. **That is no longer true**: the machine now emulates a Voodoo 2, and on
the floor configuration — 2 MB of frame buffer and 2 MB per TMU, this ADR's two
tightest constraints. E05-S02's texture budget will therefore be tried against the
real limit, and not against twice it.

## Validation configuration on real hardware

E09-S04 will declare the release good on:

| | Validation configuration |
|---|---|
| **Floor** | Pentium II 400 MHz, 64 MB, **Voodoo 2 8 MB** (2 TMUs × 2 MB), Windows 95 OSR2.5 |
| **Recommended** | Pentium III 500 MHz, 128 MB, **Voodoo 2 12 MB** or Voodoo 3 2000, Windows 95 OSR2.5 |

The floor configuration is the one that counts: it is what puts the 2 MB per TMU
and the 2 MB of frame buffer memory to the test, hence this ADR's two tightest
constraints.

## What would reopen this decision

- **An overrun of the texture budget discovered in E05-S02.** The 1.20 MiB peak
  comes from a static analysis of the neighbouring port; if the real decoding
  (E04-S07) produces more — mipmaps, uncompressed formats — total residency falls
  and a cache is needed. Beyond 2 MB per level, the floor moves to the 12 MB
  Voodoo 2.
- **A measurable cost of filling `GrVertex`**, recorded in
  [E08-S03](../stories/E08-perf/E08-S03-vertex-path-optimisation.md). That is the
  argument that would tip the choice towards Glide 3 and its `grVertexLayout`.
- **E00-S03's go/no-go**, which has not yet come: the CPU floor above is
  **provisional**. E00-S03 measured a combined factor of ~38× between host and
  target, but the verdict awaits a real play session
  ([E02-S06](../stories/E02-system/E02-S06-game-bring-up.md)). If that verdict is
  negative, this ADR raises the floor or records the exit — it does not work
  around the figure.
- A request for Voodoo 4/5 support, which would impose Glide 3 (`h5`).

## References

- [`docs/adr/0003-memory-budget.md`](0003-memory-budget.md) — 47 MiB available, Glide's cost
- [`docs/research/cpu-budget.md`](../research/cpu-budget.md) — factor of 38×, go/no-go pending
- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — 1,116 KB peak over 65 levels
- `tools/win95/probes/glide_hwinfo.c` — hardware query
- `tools/win95/probes/glide3_probe.c` — Glide 3.x trial
- [sezero/glide](https://github.com/sezero/glide) — `glide2x` and `glide3x` trees, real targets
