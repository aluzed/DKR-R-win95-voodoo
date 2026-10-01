# Configuration -- the Windows 95 target

The game is configured by `DKRR.INI`, beside `DKRR.EXE`. The game writes a
commented copy the first time it starts without one; an absent file is a valid
configuration, every setting having a default. Settings are read once, when the
game starts.

## The file

```ini
[Paths]
Rom=C:\GAMES\DKR\DKR.Z64

[Settings]
RDRAM_SNAPSHOT=copy
```

- **`[Paths]`** -- `Rom=`, the ROM. Written by the game when it finds one
  beside `DKRR.EXE`; see `ROM_SETUP.md` for the order in which it looks.
- **`[Settings]`** -- every line `NAME=VALUE` sets the option `DKR_NAME` for
  this run, exactly as if `SET DKR_NAME=VALUE` had been typed before starting
  the game. A variable already set in the environment wins over the file, so a
  batch file can override one setting for one run.

A setting that is a **switch** is on when its line is present, **whatever its
value**: put `;` in front of the line to turn it off. (`GFX_STATS=0` turns the
statistics *on*.) The log's first lines say how many settings the file applied
and how many the environment overrode:

    [boot][ini] C:\GAMES\DKR\DKRR.INI: 2 setting(s) applied, 0 overridden by the environment

The file is next to the executable rather than with the saves
(`dkr-runtime-data\` in the same folder): the two are in the same place, and
this is the one a player is told to open.

## Settings for playing

| Setting | Values | Default | Effect | Cost |
|---|---|---|---|---|
| `RDRAM_SNAPSHOT` | `none`, `copy` | `none` | `copy` copies the game's four megabytes of memory for every frame drawn, so that the game runs while the card draws. On one processor that overlap does not exist. | `copy`: about 51 ms a frame in a race against 35 ms |
| `GLIDE_SWAP` | `immediate` | synchronised | Presents each frame at once instead of at the monitor's retrace. | none; may tear |
| `JOY_DEADZONE` | 0..90 | 15 | The share of a joystick axis's half-range that reads as centred. Pads of the period drift at rest. An out-of-range value is refused and logged. | none |
| `JOY_BUTTONS` | up to eight of `A B Z START L R CU CD CL CR DU DD DL DR -`, comma-separated | `A,B,Z,START,L,R,CD,CL` | The N64 button for each of the pad's buttons, in order; `-` for none. A list naming no N64 button is refused and logged. | none |
| `JOY` | `off` | on | Ignore joysticks and pads. | none |
| `OSD` | switch | off | Draws frame rate, render and audio times, triangles, texture memory and audio underruns over the game. | about 0.5 ms a frame |

## Settings for reporting a problem

| Setting | Values | Effect | Cost |
|---|---|---|---|
| `GFX_STATS` | switch | The renderer's per-triangle statistics, in the log. Off by default since 1 October 2026. | up to 4.3 ms a frame |
| `TIMING_EXPORT` | a folder, ending in `\` | Writes `FRAMES.BIN` and `AUDIO.BIN`, one record per frame and per audio task, read by `tools/win95/timing_report.py`. | negligible |
| `TRACE_IDLE_METER` | switch | Logs the share of the processor left idle every five seconds. | none (an idle-priority thread) |
| `TRACE_PAGING` | switch | Logs Windows 95's memory and paging counters every five seconds. | negligible |
| `CAPTURE_KEY` | switch or a count | F9 writes the current display list and the card's picture of it to `D:\` -- the test machine's transfer disk. | seconds per capture |

## Every option

What reads each `DKR_*` variable, for anyone who needs more than the tables
above. Most are investigation switches, described where they are read; a
setting not listed in the tables is not meant for playing.

| Variable | Read in |
|---|---|
| `DKR_AUDIO_CAPTURE` | `game_main.cpp` |
| `DKR_AUDIO_MICROCODE` | `game_main.cpp` |
| `DKR_CANARY` | `glide_renderer.cpp` |
| `DKR_CAPTURE_KEY` | `glide_renderer.cpp` |
| `DKR_CAPTURE_LIST` | `glide_renderer.cpp` |
| `DKR_CAPTURE_MODE` | `glide_renderer.cpp` |
| `DKR_CLEAR_NEAREST` | `glide.c` |
| `DKR_DUMP_EVERY` | `glide_renderer.cpp` |
| `DKR_DUMP_FRAME` | `glide_renderer.cpp` |
| `DKR_DUMP_MODE` | `glide_renderer.cpp` |
| `DKR_FLATTEN_W` | `glide_renderer.cpp` |
| `DKR_FOG` | `glide_renderer.cpp` |
| `DKR_FORCE_COMBINE` | `glide_renderer.cpp` |
| `DKR_FORCE_STATE` | `glide_renderer.cpp` |
| `DKR_FORGET_AT_FRAME` | `glide_backend.c` |
| `DKR_GFX_NO_STATS` | `glide_renderer.cpp` |
| `DKR_GFX_STATS` | `glide_renderer.cpp` |
| `DKR_GLIDE_SWAP` | `glide.c` |
| `DKR_INPUT_BACKEND` | `runtime_platform.cpp` |
| `DKR_INTERPOLATION_TRACE` | `presentation_identity.cpp` |
| `DKR_LAUNCHER_PROFILE` | `launcher_performance.hpp` |
| `DKR_LEGACY_QUALIFICATION_RECIPE` | `game_main.cpp` |
| `DKR_LOG` | `game_main.cpp` |
| `DKR_MQ_HOLD_REFUSED` | `mesgqueue.cpp` |
| `DKR_NEUTRAL` | `glide_renderer.cpp` |
| `DKR_NO_ALPHA_TEST` | `glide_renderer.cpp` |
| `DKR_NO_AUDIO_OUT` | `runtime_platform.cpp` |
| `DKR_NO_DEPTH` | `glide_renderer.cpp` |
| `DKR_NO_MULTIPASS` | `glide_renderer.cpp` |
| `DKR_NO_STATE_SHADOW` | `glide_backend.c` |
| `DKR_NO_TEXCACHE` | `glide_renderer.cpp` |
| `DKR_OSD` | `glide_renderer.cpp` |
| `DKR_PAINT_WHITE` | `glide_renderer.cpp` |
| `DKR_PROBE` | `glide_renderer.cpp` |
| `DKR_PROBE_SWITCH` | `game_main.cpp` |
| `DKR_RDRAM_SNAPSHOT` | `events.cpp` |
| `DKR_RDRAM_SNAPSHOT_FROM` | `events.cpp` |
| `DKR_RDRAM_SNAPSHOT_POOL` | `events.cpp` |
| `DKR_RDRAM_SNAPSHOT_SIZE` | `events.cpp` |
| `DKR_RENDERER` | `game_main.cpp` |
| `DKR_SCISSOR` | `glide_renderer.cpp` |
| `DKR_SDL3_INPUT_HOST` | `runtime_platform.cpp` |
| `DKR_SHADOW_TRACE` | `f3ddkr_rt64.cpp` |
| `DKR_SPLIT_TRACE` | `f3ddkr_rt64.cpp` |
| `DKR_THREADS_LOG` | `test_threading.cpp` |
| `DKR_TIMING_EXPORT` | `timing_export.hpp` |
| `DKR_TRACE_AUDIO_ZONES` | `game_main.cpp` |
| `DKR_TRACE_CPU` | `events.cpp`, `threads.cpp` |
| `DKR_TRACE_EXCLUSIVE` | `threads.cpp` |
| `DKR_TRACE_IDLE_METER` | `game_main.cpp` |
| `DKR_TRACE_LIST` | `glide_renderer.cpp` |
| `DKR_TRACE_PAGING` | `game_main.cpp` |
| `DKR_TRACE_RENDER_ZONES` | `glide_renderer.cpp` |
| `DKR_TRACE_SAMPLER_DELAY` | `sampler.c` |
| `DKR_TRACE_SAMPLER` | `sampler.c` |
| `DKR_TRACE_SP` | `events.cpp`, `mesgqueue.cpp` |
| `DKR_TRACK_PROFILE` | `track_performance.hpp` |
| `DKR_VI_PRESENT` | `events.cpp` |
