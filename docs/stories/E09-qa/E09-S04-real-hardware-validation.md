# E09-S04 — Validation on real hardware

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | TODO |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E05-S07, E06-S03, E08-S04, E09-S02 |
| **Blocks** | E09-S05 |

## Context

E09-S01's Voodoo emulation is faithful functionally and deceptive as to performance: the
modern host runs the rendering far faster than period hardware, and nothing in the
emulator reproduces the PCI bus's bandwidth, the disk's latency, or a real machine's
thermal behaviour.

This whole project is built on a budget constraint. It can only be validated on the
machine aimed at.

This ticket must take place **several times**, not once at the end. A first pass as soon
as the game displays an image on real hardware is worth more than an exhaustive
validation three months later: it reveals the surprises while there is still time to
respond to them.

## Objective

To validate the port on authentic hardware, and to confirm that the announced hardware
floor is the right one.

## Scope

**In:** running on real hardware, measuring, and adjusting the floor.

**Out:** correcting the defects found, which goes back to the tickets concerned.

## Work

1. Assemble the validation configuration named by E00-S05's ADR: processor, memory, 3dfx
   card, sound card, Windows 95 OSR2.5.
2. Assemble if possible a second, more modest configuration — close to the floor — and a
   faster one, in order to situate the operating range.
3. Install and launch. Record everything that differs from the emulator, in particular at
   startup and at Glide's initialisation.
4. Measure with E08-S01's instrumentation, over a real play session: time per frame as a
   distribution, breakdown by item, memory occupancy, page faults, audio underruns.
5. Compare item by item against the measurements obtained under emulation, and document
   the deviations. That table of deviations has lasting value: it indicates how far the
   emulator can be trusted from then on.
6. Check what the emulator cannot show: compatibility of the real 3dfx drivers, the sound
   card's behaviour, off-the-shelf gamepads, output to a cathode-ray monitor, load times
   from a period disk.
7. Actually play. A complete playthrough, several levels, in multiplayer. Playability
   defects — input latency, irregular pacing, imprecise driving — do not reveal themselves
   otherwise.
8. Confirm or correct E00-S05's ADR hardware floor on the strength of those
   measurements.

## Acceptance criteria

- [ ] The game runs on at least one authentic hardware configuration.
- [ ] E08-S01's measurements are taken on real hardware.
- [ ] The table of emulator / real-hardware deviations is documented, item by item.
- [ ] The real 3dfx drivers, the sound card and off-the-shelf gamepads are checked.
- [ ] A complete playthrough is played, multiplayer included.
- [ ] The ADR's hardware floor is confirmed or corrected.
- [ ] The defects found are recorded and attributed to a ticket.

## Risks

This is where the project's hypotheses are really put to the test. A large deviation
between the emulation and real hardware may call the hardware floor into question, or
even the feasibility on the class of machine aimed at. Hence the insistence on **early
and repeated** passes: discovering that deviation early leaves time to respond to it,
discovering it at the end leaves only the choice of revising the announcement.

## References

- E00-S05 — hardware floor to confirm
- E08-S01 — instrumentation
- E09-S01 — emulated environment and its documented limits
