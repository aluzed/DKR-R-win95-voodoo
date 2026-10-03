# Testing

How this port is tested, on the development machine and on the test machine
(86Box, Windows 95; `docs/TEST-ENVIRONMENT.md`). E09-S03.

## Three families

| Family | What it tests | Where it runs | How |
|---|---|---|---|
| **Platform** | the Windows 95 layer: clock, threads, files, saves, the render chain | host, and the same sources on the target | `platform/win95/tests/run-tests.sh` |
| **Portable logic** | the runtime's policies and codecs that the game compiles | host, and the same sources on the target | `tools/tests/run-portable-tests.sh`, `tools/tests/Run-Target-Tests-VM.sh` |
| **Cross-target** | the same input giving the same output on both | both, compared | the render oracle (`tools/render/check-corpus.sh`), the audio oracle (`tools/audio/abi_difftest`, `replay_aspmain`), and the portable suites' compile-time assertions (below) |

`scripts/Package-Win95.sh` runs the platform tests and the portable-logic suites
on the host and refuses to write a package if either fails.

## Platform tests

    platform/win95/tests/run-tests.sh             every suite
    platform/win95/tests/run-tests.sh threading   one

Seventeen suites on the host: `tick64`, `joystick` (added to the runner on
3 October 2026 -- its sixteen checks had only been run by hand), `settings`
(the lists `ini_settings.c` keeps against the sources' `getenv` calls),
`streams` (no stream opened on a `std::filesystem::path` in the Windows 95
build, `check_path_streams.py`), `clock`, `threading`, `fileio`, `saves`,
and the render chain -- `clip`, `transform`, `f3ddkr`, `rdp`, `texture`,
`combiner`, `tmu`, `pipeline`, `render`. Several are also built for the target
by `Build-Win95.sh` and run there by hand: `THREADS.EXE`, `THRCPP.EXE`,
`CLOCKT.EXE`, `FILEIOT.EXE`, `FSSEAM.EXE`, `SAVEMGR.EXE`, `SAVECDC.EXE`,
`PWRCUT.EXE`, `DSKFULL.EXE` and the render witnesses; their tickets record the
results (E02-S01, E02-S03, E02-S05, E04).

## Portable-logic suites

`runtime-recomp/CMakeLists.txt` declares seventy suites, run by CTest in
`Build-Linux.sh`. That build needs GekkoNet, SDL3, RT64 and the generated
sources; a Windows 95 checkout has none of them. Sorted on 2 October 2026 by
following each suite's includes into `runtime-recomp/src/game` and comparing
with the sources `cmake/win95-target.cmake` compiles into `DKRR.EXE`:

- **30 test code the Windows 95 game contains and need nothing else.** They are
  listed in `tools/tests/portable-suites.txt` and run on the host by
  `tools/tests/run-portable-tests.sh` (about 25 seconds, every one passing on
  2 October). Policies -- presentation, HUD layout, input, controller mapping,
  magic codes, audio mix -- the save codec and manager, the audio equaliser,
  the renderer snapshot.
- **2 test shared code but need more:** `DKRRomRevision` takes three ROM
  revisions as arguments; `DKROnlineInputBroker` links the netplay library.
- **38 test code the Windows 95 build does not contain:** netplay and its
  transports, rollback and determinism hashing, friends, Quick Join, texture
  packs, the launcher, RT64's split screen and HUD groups, live LOD and mips,
  the SDL3 input host, the controller database. They stay with the modern
  build.

### On the target

`Build-Win95.sh` builds the same suites from the same list as
`build/win95/bin/portable/PT<short>.EXE`, with the game's include paths,
definitions and compiler flags. Then:

    tools/tests/Run-Target-Tests-VM.sh

copies them to `D:\PT`, runs them one after the other on the test machine
through a batch file, and reports each one's exit code.

Seven of the thirty are not built for the target, each for a stated reason
(`cmake/win95-target.cmake`, `DKR_PORTABLE_HOST_ONLY`):

- **`SAVMGR`**: `SAVEMGR.EXE` already runs that suite on the target.
- **`MAGRT`**: its harness creates and removes directories through
  `std::filesystem`, whose operations are Windows 95's empty wide functions
  (`docs/research/win95-filesystem.md`).
- **`AMIX`, `CAMERA`, `HUD`, `MOTION`, `WIDE`: they do not compile for the
  target, and that is the cross-target divergence this ticket asked to be able
  to catch** -- found, not planted.

### The divergence: float constants in `long double`

The target compiles with `-mfpmath=387`, and GCC 13 gives C++ the standard
excess-precision rules there: a `float` constant expression is evaluated in
`long double`. A comparison of a function's `float` result with one therefore
fails where the host's passes:

    static_assert(advance_mix_volume(1.0F, 0.0F) == 0.92F);
    // the comparison reduces to 9.20000016689300537109e-1l == 9.20000000000000000015e-1l

Thirteen such assertions in five suites. Most are only the test's comparison,
but one is in the code under test:

    constexpr float split_gutter_authored(float viewport_aspect) {
        return viewport_aspect <= (4.0F / 3.0F) ? 0.0F
            : 80.0F * (viewport_aspect / (4.0F / 3.0F) - 1.0F);
    }

Called with `4.0F / 3.0F`, the parameter holds the `float` 1.33333337 and the
comparison's right side the `long double` 1.33333333..., so the target takes
the second branch and returns 2.4e-6 where the host returns 0. A millionth of a
HUD unit, invisible -- but every threshold comparison of a `float` against a
constant expression in the game's C++ behaves this way on the target. Whether
the target's C++ should be built with `-fexcess-precision=fast` instead is a
decision for the whole game, to be measured, not taken in a test file.

**How far it reaches, surveyed on 3 October 2026.** The 122 files of
`src/game` that `DKRR.EXE` compiles or includes hold 29 comparisons of a
`float` against a constant that `float` cannot represent exactly. 27 are
thresholds with a margin built in -- `<= 1.0001F` for a cover scale, `< 0.001F`
for a neutral equaliser band, `>= 0.9999F` for a full volume -- or ImGui code the
target does not run; the excess precision moves where such a test falls by
less than a float's last bit, at a value the margin exists to avoid. The other
two are `split_gutter_authored` and `fullscreen_gutter_authored`, which test
their boundary exactly, the case above. Nothing found changes what the game
does.

## The render oracle

    tools/render/build-host-tools.sh
    tools/render/check-corpus.sh ~/.local/dkr-win95/corpus

replays every capture of a corpus through the software oracle and reports what
changed in the decoder's counts and in the image. `docs/VISUAL-TESTING.md`.
The corpus is kept outside the repository, eight megabytes a capture: on the
development machine, since 3 October 2026, 66 captures from the test machine
in `~/.local/dkr-win95/corpus`, with their counts and images as references.
Run it after any change to the decoder or the oracle; `--accept` adopts new
counts when a change is meant to move them.

## Not yet

- On 3 October 2026 the target run is green: the five platform tests and the 23
  portable suites. Its first run had stopped at `SAVEMGR.EXE` -- a page fault
  behind Windows' error dialog, from a test helper opening a stream on a `path`
  (`_wfopen`, empty on Windows 95). Opening streams with
  `dkr::fs::stream_name(path)` avoids that everywhere.

- The portable suites are not run on the target by the package script: that
  needs the test machine, about five minutes.
- `DKRRomRevision` needs the v80 ROM, which this checkout's machine does not
  have.
