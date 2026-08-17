# E04-S06 — Translating the RDP state

| | |
|---|---|
| **Epic** | E04 — RT64-independent F3DDKR HLE |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | L |
| **Depends on** | E04-S02 |
| **Blocks** | E05-S03, E05-S05, E05-S06 |

## Context

The N64's RDP is driven by a dense state: cycle mode, colour combiner, render mode,
blending, depth test, fog, texture modes. That state is encoded in a few 64-bit words
with interleaved fields, and it is what determines what appears on screen.

The colour combiner deserves a mention of its own. It is a programmable unit that
computes, for each pixel, a combination of texel, primitive colour, environment
colour, shading colour and constants — over one or two cycles. Glide's fixed combiner
is far less expressive, and the translation is the hard point of the whole of epic
E05.

The preparatory work already exists, and it comes from the neighbouring native port:
its E00-S01 ticket inventoried **33 combiner configurations** actually used by DKR, of
which **only 3 read two texels**
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`). That figure changes
the nature of the problem: it is not a matter of translating a programmable combiner
in general, but 33 concrete, enumerated cases.

## Objective

To translate the game's RDP state into the abstract render state defined in E04-S01,
exhaustively and verifiably.

## Scope

**In:** decoding the RDP state and translating it into the abstract state.

**Out:** realising that state through Glide (E05-S03 to E05-S06).

## Work

1. Take up the native port's inventory of 33 configurations and **recheck it** on
   this port: same game, same version, but a different decoder may see configurations
   that the other normalises. The inventory is a solid starting point, not an imported
   truth.
2. Decode the cycle mode. Single cycle and two-cycle do not translate the same way:
   two-cycle corresponds to two combination stages, hence potentially to two passes or
   to two TMUs on Glide's side.
3. Decode the combiner: the sixteen possible inputs of each term, over one or two
   cycles, and represent them in a canonical, comparable form. That canonical form is
   what will allow E05-S03 to match a configuration to a Glide setting by a simple
   lookup.
4. Decode the render mode: blending, alpha test, dithering, depth test and write, fog,
   antialiasing.
5. Decode the texture modes: filtering, wrapping, mirroring, levels of detail,
   conversion.
6. Instrument the decoder so that it logs any configuration encountered and not
   catalogued. That is the safety net: a static inventory cannot guarantee that it has
   seen all of the game's paths, and a missing case must announce itself rather than
   silently produce a wrong image.
7. Replay a complete playthrough — every level, every game mode, the menus, the
   cutscenes — with that instrumentation, and complete the inventory with what comes
   back.
8. Write `docs/research/rdp-state-inventory.md`: the exhaustive list of the states
   encountered, their frequency, and the screen area they cover. Frequency and area
   decide the order of work in E05-S03.

## Acceptance criteria

- [ ] The inventory of 33 configurations is rechecked on this port, with the
      differences recorded — **impossible by the same method**, and that is the
      conclusion. The neighbour derived it from the decompilation's C sources; this
      port does not have them, it works from recompiled MIPS. Its equivalent is
      instrumentation at run time, which requires the ROM.
- [x] Single cycle and two-cycle are both decoded and distinguished — and the cycle
      mode **is part of the canonical key**, the same word not producing the same
      image depending on the cycle.
- [x] The combiner is represented in a comparable canonical form. Verified against the
      **63 `G_CC_*` macros in the decompilation's headers**, resolved and encoded by a
      generator rather than transcribed: the 63 decode field for field, with no key
      collision.
- [x] The render and texture modes are decoded — cycle, filtering, LOD, detail,
      perspective, alpha comparison, Z source, depth test and write. The blender stays
      raw: it belongs to E05-S05.
- [x] Any uncatalogued configuration is logged at run time — and it took two
      faults to get there, both found on 17 August 2026 by running the game.
      The key did not normalise the RDP's several spellings of zero, and the
      counter was asking an eight-entry table hand-written in a shorthand where
      `0` meant zero, which could never match anything. That table is gone; the
      lookup goes to `CC_TABLE`, generated from the game's source.

      Measured on the machine, same ROM, same window:

      | | catalogued | unknown |
      |---|---:|---:|
      | before | **0** | 24,286 |
      | after | **20,986** | 424 |

      The two keys that remain are logged with their composition, which is what
      makes them addable: `09FF9108` is `G_CC_SHADE`, `0EF93108` is `TEXEL0`
      with `TEXEL0_A * PRIM_A` alpha. Neither appears in the game's static
      tables, hence neither is in the generated inventory.
      See [`docs/research/win95-game-render.md`](../../research/win95-game-render.md).
- [ ] A complete playthrough is replayed under instrumentation — **blocked** by
      E02-S06, which requires the ROM.
- [~] `docs/research/rdp-state-inventory.md` exists and records what is established.
      **Frequency and screen area are missing**: both are measured at run time, and
      the neighbour's count of table entries is a coarse substitute for them — it
      counts declarations, not pixels.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

A missed configuration does not show at decoding time: it shows on screen, as a
surface of an unexpected colour, possibly in a single level. Step 6's instrumentation
and step 7's complete playthrough are what distinguish a real inventory from a
plausible one.

## References

- `../../Diddy-Kong-Racing/docs/research/combiner-inventory.md` — 33 configurations,
  3 with two texels
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — `MoveWord`, `SetTextureImage`,
  `LoadBlock` handlers
- E05-S03 — the main consumer of this inventory
