# E06-S05 — Configuration by file

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | IN_PROGRESS |
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

## What was built (1 October 2026)

`DKRR.INI` beside the executable, two sections. `[Paths] Rom=` belongs to the
ROM search (E06-S06). `[Settings]` holds `NAME=VALUE` lines, each setting the
option `DKR_NAME` exactly as an environment variable would: a constructor of
priority 101 reads the section with `GetPrivateProfileSectionA` and `_putenv`s
it, before any C++ static initialiser -- several options are read once, there.
A variable already in the environment wins. Rather than one reader per
setting, every existing switch is configurable at once.

Two traps of Windows 95's profile functions, found on the test machine: reading
a missing file left it existing for `GetFileAttributesA`, so the template was
never written; and the system's cache of the file was then flushed over the
template. Existence is now taken before any profile call, and the cache is
flushed around the template's write.

Also decided with this ticket, as a default suited to the target: the
renderer's statistics are off unless `GFX_STATS` is set -- they draw nothing
and cost 4.3 ms a display list in the adventure hub.

## Acceptance criteria

- [x] The `.ini` file covers all the settings kept: `[Settings]` sets any `DKR_*` option, copied into the C runtime's environment before the static initialisers that read them (`platform/win95/ini_settings.c`, 1 October 2026).
- [x] An absent file produces a valid default configuration -- and the game then writes the commented template. Checked on the test machine.
- [~] An aberrant value is rejected, replaced by the default, and logged. *Partly*: the file's values reach each option's own reader, and only some of them check what they read (`RDRAM_SNAPSHOT_SIZE`, `CAPTURE_KEY`, `MQ_HOLD_REFUSED`). **Two mistakes are caught at the file since 3 October 2026**: a switch read only for its presence set to `0`, `off`, `no` or `false` -- which turned it *on* -- is left off, and a name no code reads is logged; checked on the test machine (`NO_DEPTH=0: left off`, `OSD=off: left off`, `NODEPTH is not a setting this build reads`). The lists come from the sources, and `run-tests.sh settings` fails when they fall behind. A value-taking setting given nonsense still depends on its own reader.
- [x] The file written at first launch is commented.
- [x] `docs/CONFIGURATION.md` documents every setting, with its impact on performance
      where applicable -- since 3 October 2026 every one of the 63 variables the code
      reads, checked against a search of the sources: the settings for playing and
      for reporting with their cost, and every investigation switch with what it
      does, its cost where it has one (`GFX_STATS`, `RDRAM_SNAPSHOT=copy`,
      `AUDIO_MICROCODE`, `GLIDE_OPEN_EARLY`, `MQ_HOLD_REFUSED=0`) and the file that
      reads it.
- [x] The diagnostic settings the other tickets ask for are present: every one of them, by the same mechanism.
- [x] The file's location is consistent with the saves' (E02-S05): beside `DKRR.EXE`, whose folder also holds `dkr-runtime-data\`.

## Risks

The configuration file is also what will allow a user to diagnose a problem themselves
on a machine the developer does not have to hand. Its default values must therefore be
safe and its format tolerant: a configuration that prevents the game from starting and
cannot be corrected is a dead end.

## References

- `runtime-recomp/src/game/runtime_ui.cpp` — 194 KB of ImGui overlay, replaced
- `runtime-recomp/src/game/runtime_enhancements.cpp` — current settings
- E07-S01 — settings removed with the Accurate-only profile
