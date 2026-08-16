# E06-S05 — Configuration by file

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | S |
| **Depends on** | E06-S01 |
| **Blocks** | E06-S06, E09-S05 |

## Context

DKR-R exposes its settings through an ImGui overlay: `runtime_ui.cpp` is 194 KB. That
overlay disappears with ImGui (E07-S02), and it has to be replaced.

On this target, a text configuration file is the right choice, for three reasons: it is
the usage of the period, it costs nothing in memory or CPU, and it stays diagnosable —
a user can read and correct their file, which counts when support is done remotely on
hardware one does not have.

The `.ini` format is Windows 95's, and the system supplies the functions to read it —
`GetPrivateProfileString` and its family, present since Windows 3.1. We may as well use
them rather than write a parser.

## Objective

To deliver a configuration by `.ini` file covering all the port's settings, with
default values suited to the target.

## Scope

**In:** reading, writing, default values, validation, documentation.

**Out:** any graphical configuration interface.

## Work

1. Inventory the settings to keep, starting again from `runtime_ui.cpp` and removing
   those that disappear with the "Accurate" profile alone (E07-S01).
2. Design the file in sections: display, rendering, audio, inputs, paths, diagnostics.
   A legible file gets maintained; a flat file does not get reread.
3. Implement the reading through the system's profile functions, with a default value
   for each entry. An absent file must produce a valid configuration, not an error.
4. Validate each value read and fall back on the default in the event of an aberrant
   value, logging it. A hand-edited file will contain errors.
5. Write the file at first launch, commented, with the default values. It is the most
   effective documentation: it is where the user looks.
6. Choose which settings must be modifiable without a restart, and which need not be.
   On this target, requiring a restart is acceptable and avoids a great deal of
   complexity.
7. Document every setting in `docs/CONFIGURATION.md`: effect, admissible values,
   default, and impact on performance where there is one.
8. Provide the diagnostic settings the other tickets need: the decoder's trace mode
   (E04-S02), the choice of render backend (E04-S08), forcing the multipass path
   (E05-S04), displaying the counters (E08-S01).

## Acceptance criteria

- [ ] The `.ini` file covers all the settings kept.
- [ ] An absent file produces a valid default configuration.
- [ ] An aberrant value is rejected, replaced by the default, and logged.
- [ ] The file written at first launch is commented.
- [ ] `docs/CONFIGURATION.md` documents every setting, with its impact on performance
      where applicable.
- [ ] The diagnostic settings the other tickets ask for are present.
- [ ] The file's location is consistent with the saves' (E02-S05).

## Risks

The configuration file is also what will allow a user to diagnose a problem themselves
on a machine the developer does not have to hand. Its default values must therefore be
safe and its format tolerant: a configuration that prevents the game from starting and
cannot be corrected is a dead end.

## References

- `runtime-recomp/src/game/runtime_ui.cpp` — 194 KB of ImGui overlay, replaced
- `runtime-recomp/src/game/runtime_enhancements.cpp` — current settings
- E07-S01 — settings removed with the Accurate-only profile
