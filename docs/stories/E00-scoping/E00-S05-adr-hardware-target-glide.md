# E00-S05 — ADR: hardware target and Glide version

| | |
|---|---|
| **Epic** | E00 — Scoping, measurements and decisions |
| **Status** | DONE |
| **Priority** | P0 |
| **Estimate** | S |
| **Depends on** | E00-S03, E00-S04 |
| **Blocks** | E05-S01, E05-S02, E05-S04, E09-S01, E09-S04 |

## State as of 2026-08-12 — the ADR is written

[`docs/adr/0002-hardware-target.md`](../../adr/0002-hardware-target.md).

| Item | Floor | Recommended |
|---|---|---|
| CPU | Pentium II 400 MHz *(provisional)* | Pentium III 500 MHz and above |
| RAM | **64 MB** — 47.0 MiB free, measured | 128 MB |
| Card | Voodoo 2 8 MB, **2 TMUs** | Voodoo 2 12 MB or Voodoo 3 |
| API | **Glide 2.4x** (`glide2x.dll` 2.54) | the same |
| Resolution | **640 × 480, 16-bit, double buffer + Z** | the same |

**Three decisions rest on a calculation, not on a preference:**

- **Triple buffering ruled out**: 2.34 MiB against 2 MB of frame buffer memory on
  the 8 MB Voodoo 2. Keeping it would exclude the floor.
- **Total texture residency**: the peak measured by the neighbouring port is
  1,225 KB padded over 65 levels, that is **60 % of a 2 MB TMU**. E05-S02 can aim at
  per-level residency rather than a cache with eviction.
- **Two TMUs required**, a single TMU remaining a correct but slow multipass
  fallback.

**The Glide 2.4 choice is argued on the sources**, as asked. `sezero/glide`'s
`README` shows that `glide2x` builds for `sst1`, `cvg` and `h3`, and `glide3x` for
`sst1`, `cvg`, `h3` and `h5`: **both cover the whole target**, and the received idea
that "Glide 2.4 is the only route to the Voodoo 1" is false.

What decided it is measured on the machine: Glide 2.54 is proved end to end, while
**Glide 3 requires a valid HWND** — `grSstWinOpen` refuses with "need to use a valid
window handle", which would couple the rendering bring-up to E06-S01.
`grVertexLayout` remains the argument that would reopen the decision.

**Corrected on 14 August 2026.** This paragraph said that 86Box's configuration file
lied, `grSstQueryHardware` reporting type `0` while the file announced `type = 2`.
That was false, and so was the method: there are **two Voodoo sections** in
`86box.cfg`, and the one 86Box reads carries the instance suffix. It said `type = 1`
— an Obsidian SB50, a two-TMU Voodoo 1 — and 86Box honoured it faithfully. The
hand-written section was never read.

The machine is now on the **floor** card: Voodoo 2, 2 MB of frame buffer, 2 MB per
TMU, confirmed in the settings dialog. E05-S02's texture budget will therefore be
tried against the real limit and not against twice it, which **reduces** E09-S04's
weight instead of increasing it.

What stays true, and matters for E05-S01: Glide 2.54 reports type `0` and FBI
revision `261` for the Voodoo 2 as for the Obsidian. **On this platform,
`grSstQueryHardware` does not distinguish the two generations** — the number of TMUs
and the memory per TMU are usable, the model is not.

**An explicit reservation:** the CPU floor is **provisional**. E00-S03's go/no-go has
not come — it awaits a real play session (E02-S06). The ADR says so and does not work
around the figure.

## Context

"3dfx Voodoo compatible" names five generations of cards with very different
capabilities, and two mutually incompatible APIs. The choice directly conditions the
rendering work — in particular the number of TMUs available, which decides whether
the two-texel combiner configurations go through in one pass or in two.

| Card | TMUs | Texture memory | API |
|---|---|---|---|
| Voodoo Graphics (Voodoo 1) | 1 | 2 MB | Glide 2.x |
| Voodoo 2, 8 MB | 2 | 2 × 2 MB | Glide 2.4 · Glide 3.x |
| Voodoo 2, 12 MB | 2 | 2 × 4 MB | Glide 2.4 · Glide 3.x |
| Voodoo Banshee | 1 | shared | Glide 3.x |
| Voodoo 3 | 2 | 16 MB | Glide 3.x |

A constraint common to the whole range: **no hardware transformation**. The card
receives vertices already projected into screen coordinates. The entire geometry
pipeline — matrices, transformation, lighting, clipping — stays on the CPU, which
weighs on the budget E00-S03 measures. The textures are powers of two, 256 × 256 at
most on the Voodoo 1 and 2.

The project's scoping retained the class **Pentium II / III, Voodoo 2 or 3, 64 MB of
RAM, Windows 95 OSR2.5**. This ADR records it formally and draws the numbered
consequences.

## Objective

To write `docs/adr/0002-hardware-target.md`: the hardware floor, the recommended
configuration, the Glide version, the reference resolution — every value justified by
a measurement or a hardware constraint, not by a preference.

## Scope

**In:** the decision and its justification.

**Out:** the Glide implementation (E05).

## Work

1. Take E00-S03's and E00-S04's conclusions to fix the CPU floor in MHz. If
   E00-S03's no-go has come, this ADR records the project's exit or the raising of
   the floor — it does not work around the figure.
2. Decide between **Glide 2.4 and Glide 3.x**. Glide 3.x covers Voodoo 2, Banshee
   and Voodoo 3 through a single API and simplifies support; Glide 2.4 remains the
   only route to the Voodoo 1. Check in the open 3dfx sources which targets each tree
   really builds, rather than trusting the commercial documentation of the period.
3. Fix the number of TMUs **required** and the number **exploited**. Two TMUs allow a
   single pass for the two-texel combiners; the rendering must nonetheless stay
   correct on a single TMU, through the multipass fallback (E05-S04).
4. Fix the reference resolution. 640 × 480 in 16 bits is a Voodoo 2's balance point;
   check that the frame buffer and the depth buffer fit there in the target card's
   memory, including triple buffering if it is retained.
5. Establish the texture memory budget per level and set it against the memory per
   TMU. The neighbouring native port has already measured a peak of **1.20 MB** per
   level (`../../Diddy-Kong-Racing/docs/research/level-working-set.md`) — a figure to
   reuse, checking that it really bears on the same level set.
6. Name E09-S04's validation configuration: the real hardware on which the release
   will be declared good.

## Acceptance criteria

- [x] `docs/adr/0002-hardware-target.md` fixes: floor CPU, recommended CPU, RAM,
      floor 3dfx card, recommended card, Glide version, resolution.
- [x] Every value points at the measurement or the hardware constraint that
      justifies it.
- [x] The Glide 2.4 / 3.x choice is argued on the real hardware coverage of the 3dfx
      sources, not on the period's documentation.
- [x] The texture memory budget per TMU is quantified and compared against the peak
      measured per level — 1,225 KB padded against 2 MB per TMU, that is 60 %.
- [x] The real-hardware validation configuration is named.
- [x] The ADR states what would reopen it — an overrun of the texture budget in
      E05-S02, the cost of filling `GrVertex` in E08-S03, E00-S03's go/no-go, a
      request for Voodoo 4/5 support.

## Verified upstream by E09-S01

The test machine is set up and a Glide demonstration runs on it
([E09-S01](../E09-qa/E09-S01-emulated-test-environment.md)). Three facts come out of
it, to be taken up here:

- **Glide 2.54 and Glide 3.01 are both supplied** by 3dfx's reference driver for the
  Voodoo 2. Step 2's API choice therefore does not depend on the hardware:
  `glide2x.dll` and `glide3x.dll` coexist on the same machine.
- **The emulated card model must be verified in 86Box's settings dialog, not deduced
  from the configuration file.** The hand-written Voodoo settings were silently
  ignored there: the machine emulated a 2 MB Voodoo 1 while the file announced a 4 MB
  Voodoo 2. Any texture-budget or multitexture measurement made without that check
  would be wrong.
- **The PCI identifier does not suffice to identify the card** on this platform.
  E05-S01's run-time detection must go through `grSstQueryHardware` /
  `grSstQueryBoards`.

## Risks

Widening the target costs dearly and is paid for in E05: one TMU against two means a
second rendering pass over part of the game, hence a doubled fill budget on those
surfaces. Better a narrow floor that is held than a wide compatibility that is false.

## References

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — the working-set peak
  per level, already measured on the native port's side
- [3dfx Glide sources](https://sourceforge.net/projects/glide/) ·
  [sezero/glide](https://github.com/sezero/glide) ·
  [hatarch/glide3x](https://github.com/hatarch/glide3x)
