# E05-S03 — Translating the N64 colour combiner to Glide

| | |
|---|---|
| **Epic** | E05 — Glide backend |
| **Status** | IN_PROGRESS |
| **Priority** | P0 |
| **Estimate** | XL |
| **Depends on** | E04-S06, E05-S01 |
| **Blocks** | E05-S04, E09-S02 |

## Context

This is the project's hardest ticket.

The RDP's combiner is a **programmable** unit: for each term — colour and alpha — it
chooses four inputs among sixteen possible sources, and computes `(a - b) × c + d`,
over one or two cycles. The configuration space runs into the thousands.

Glide's combiner is **fixed**. `grColorCombine` and `grAlphaCombine` offer a closed
list of functions and factors, and `grTexCombine` offers another for the texture
stage. What does not fit in that list has to be obtained otherwise: by several render
passes with blending, by the second TMU, or by an approximation.

What makes the problem tractable is that it is not a matter of translating the
combiner in general. DKR uses only **33 configurations**, inventoried by the
neighbouring native port, of which **only 3 read two texels**
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`) — a figure to be
rechecked by E04-S06 on this port. Thirty-three concrete, enumerated cases, whose
frequency and screen area are known: it is a finite problem.

## Objective

To realise each of the combiner configurations DKR uses, with a visual deviation
measured and judged acceptable for each.

## Scope

**In:** the RDP configuration → Glide setting mapping, multipass, and measuring the
deviation.

**Out:** multitexturing on two TMUs (E05-S04) and fog (E05-S06).

## Work

1. Start from E04-S06's inventory, sorted by screen area covered. Work in that order:
   the configuration that covers the most pixels is the one whose error will show the
   most.
2. Classify each configuration in three categories:
   - **exact** — a Glide setting produces the same result;
   - **multipass** — several passes with blending achieve it, at the price of the fill
     budget;
   - **approximate** — no combination achieves it, and the deviation must be measured
     then accepted or refused.
3. Write the mapping table as data, indexed by E04-S06's canonical form: a lookup, not
   a cascade of conditions. That makes the table legible, testable, and completable
   without touching the code.
4. For each configuration, measure the deviation from the reference software
   rasteriser (E04-S08), which implements the combiner faithfully. The measurement is
   made by image difference, not by eye.
5. Deal with two-cycle, which corresponds to two combination stages. Depending on the
   configuration, it is resolved by the second TMU (E05-S04), by a second pass, or by
   simplification when the second stage is neutral.
6. Measure the cost of multipass. Each additional pass doubles the fill of the surface
   concerned, and fill is precisely what limits a Voodoo 2 at 640 × 480. A multipass
   configuration covering a large number of pixels must be reconsidered as
   approximate.
7. Document the result in `docs/research/combiner-mapping.md`: per configuration, the
   category, the Glide setting, the measured deviation, and the cost.
8. Log at run time any configuration absent from the table, with a fallback rendering
   that is visible but not aberrant.

## Acceptance criteria

- [x] The inventory's 29 distinct configurations are handled and classified — 12
      exact, 10 multipass, 4 approximate, 3 referred to E05-S04. The ticket announced
      33: the neighbouring port's inventory counts 21 in the tables plus 12 outside
      them, several of which coincide.
- [x] The table is a data structure indexed by canonical form, and **generated** from
      the definitions in the game's source. Verified by properties: no duplicate key —
      two entries with the same key would mask each other, and one configuration would
      be rendered by another's setting with no message to say so.
- [x] Each configuration's deviation is measured on the card, by reading the frame
      buffer back. The oracle is **the formula** and not a second program: comparing
      two programs merely displaces the question of which one is right. The scene
      excludes all interpolation, so that a deviation can only come from the combiner.
      The ticket assumed E04-S08's rasteriser implemented the combiner faithfully;
      **that was not the case**, and `dkr_combiner_eval` had to be written for this
      criterion to have any meaning.
- [x] Two-cycle is handled and each case states its strategy. Two forms fall back —
      `PASS2` is the identity, `(COMBINED,0,X,0)` is a scaling that composes — which
      avoids declaring every second cycle multipass and doubling the fill on the most
      common surfaces.
- [ ] The multipass fill cost is measured — **blocked by the ROM**. The multipass
      share is, on the other hand, bounded and watched: a check fails if it exceeds
      half the table's entries, because it is fill that limits a Voodoo 2 at 640×480.
- [~] `docs/research/combiner-mapping.md` documents category, setting and
      justification per configuration, as well as the measured enumeration values.
      **The multipass fill cost does not appear there**: it requires a representative
      scene, hence the ROM.
- [x] An unknown configuration returns NULL from the lookup, and the fallback is
      defined: texture modulated by the vertex colour, the inventory's most frequent
      behaviour. The two opposite reflexes are set aside — drawing nothing would make
      a piece of scenery disappear without a trace, painting in an alert colour would
      make the game unplayable at the first forgotten combiner.
- [~] Each approximate configuration's deviation is measured and reported, but **no
      threshold is accepted yet**: the `ENV_ALPHA` family has just been reclassified on
      the strength of a measurement, and one untried way out remains — carrying the
      constant in the vertex alpha, where `LOCAL_ALPHA` would go and fetch it. Setting
      a threshold before having tried that way out would amount to accepting a
      deviation we know may be avoidable.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

This is the ticket that can go off the rails. The temptation will be to work through
the 33 configurations one by one until "it looks about right", with no measurement.
The visual deviation then accumulates silently, and the final rendering is diffusely
wrong without any error being attributable.

The discipline of measuring against E04-S08 is not a formality: it is what turns this
ticket from a work of appreciation into a verifiable one. It is also why E04-S08 is a
prerequisite and not a comfort.

## References

- `../../Diddy-Kong-Racing/docs/research/combiner-inventory.md` — 33 configurations
- E04-S06 — rechecked inventory and canonical form
- E04-S08 — comparison oracle
- [3dfx Glide sources](https://sourceforge.net/projects/glide/) —
  `grColorCombine`, `grAlphaCombine`, `grTexCombine`
