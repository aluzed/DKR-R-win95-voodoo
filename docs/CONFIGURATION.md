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
| `KEYS` | comma-separated `control:key` or `control:key/key`; controls `A B Z START L R CU CD CL CR DU DD DL DR` and the stick's `SU SD SL SR`; keys named by their place on a US keyboard (`A`..`Z`, `0`..`9`, `SPACE`, `ENTER`, `TAB`, `LSHIFT`, `RSHIFT`, `LCTRL`, `RCTRL`, the arrows `UP DOWN LEFT RIGHT`, `INSERT DELETE HOME END PGUP PGDN`, `COMMA PERIOD SLASH SEMICOLON QUOTE MINUS EQUALS LBRACKET RBRACKET BACKSLASH`, `BACKSPACE`) | stick `W A S D`, A `SPACE`, B `LSHIFT/RSHIFT`, Z `Z`, Start `ENTER`, D-pad the arrows, L `Q`, R `E`, C `I K J L` | Player one's keys, **by position**: on a French keyboard the default stick is the Z Q S D block. Controls not named keep their default; a key may serve two controls; an entry not understood is skipped, and the log counts both (`[input] KEYS:`). | none |
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

Every `DKR_*` variable the code reads, what it does and where. Most are
investigation switches, each answering one question in one run; the comment
where it is read says more. Where one costs time, the cost is given. A setting
not listed in the tables above is not meant for playing; the joystick settings
are there under their file names, `JOY`, `JOY_BUTTONS` and `JOY_DEADZONE`.

| Variable | What it does | Read in |
|---|---|---|
| `DKR_AUDIO_CAPTURE` | captures audio tasks to D: for the host's microcode oracle (`docs/AUDIO-HLE.md`) | `game_main.cpp` |
| `DKR_AUDIO_MICROCODE` | `1` runs the recompiled audio microcode instead of the high-level mixer: 4.6 times its cost | `game_main.cpp` |
| `DKR_CANARY` | draws two test squares each list, to tell a broken render state from broken vertices | `glide_renderer.cpp` |
| `DKR_CAPTURE_KEY` | `<n>`: F9 writes a capture and the card's frame, up to n times | `glide_renderer.cpp` |
| `DKR_CAPTURE_LIST` | display-list numbers to capture, without a key | `glide_renderer.cpp` |
| `DKR_CAPTURE_MODE` | counts `DKR_CAPTURE_LIST` from the moment a game mode is reached | `glide_renderer.cpp` |
| `DKR_CLEAR_NEAREST` | a clear diagnostic; answers one question in one run | `glide.c` |
| `DKR_DUMP_EVERY` | with `DKR_DUMP_FRAME`, dumps a frame every n lists | `glide_renderer.cpp` |
| `DKR_DUMP_FRAME` | dumps the card's frame at a list number, as a BMP | `glide_renderer.cpp` |
| `DKR_DUMP_MODE` | counts `DKR_DUMP_FRAME` from a game mode (-1 intro, 0 race, 1 menu) | `glide_renderer.cpp` |
| `DKR_FLATTEN_W` | gives every triangle a rectangle's depth values; a rasterisation diagnostic | `glide_renderer.cpp` |
| `DKR_FOG` | `1` turns fog on; off by default, see the file | `glide_renderer.cpp` |
| `DKR_FORCE_COMBINE` | `shade`, `texel`, `texel_shade`, `texel_shade_a`: one combine mode for every draw | `glide_renderer.cpp` |
| `DKR_FORCE_STATE` | `1`: every triangle under the canary's render state | `glide_renderer.cpp` |
| `DKR_FORGET_AT_FRAME` | `1` forgets every remembered card state at each frame | `glide_backend.c` |
| `DKR_GFX_NO_STATS` | turns the per-list statistics off anywhere | `glide_renderer.cpp` |
| `DKR_GFX_STATS` | `1` turns the per-list statistics on: 4.3 ms a list in the hub | `glide_renderer.cpp` |
| `DKR_GLIDE_OPEN_EARLY` | `1` opens the card before the game starts, as before 2 October 2026: the first image 1.25 s later (`docs/research/win95-startup.md`) | `glide_renderer.cpp` |
| `DKR_GLIDE_SWAP` | `immediate` swaps without waiting for the scan; measured no different here | `glide.c` |
| `DKR_INPUT_BACKEND` | the modern build's input backend choice | `runtime_platform.cpp` |
| `DKR_INTERPOLATION_TRACE` | the modern build's frame-interpolation trace | `presentation_identity.cpp` |
| `DKR_LAUNCHER_PROFILE` | the modern launcher's profiling | `launcher_performance.hpp` |
| `DKR_LEGACY_QUALIFICATION_RECIPE` | the modern build's scene qualification | `game_main.cpp` |
| `DKR_LOG` | where the runtime log is written | `game_main.cpp` |
| `DKR_MQ_HOLD_REFUSED` | `0` puts a refused message back on the queue instead of holding it: the idle thread spins (patch 0056) | `mesgqueue.cpp` |
| `DKR_NEUTRAL` | `<mask>` neutralises render-state fields: 1 texture, 2 filter, 4 blend, 8 fog, 16 constant colour | `glide_renderer.cpp` |
| `DKR_NO_ALPHA_TEST` | `1` draws every texel whatever its alpha | `glide_renderer.cpp` |
| `DKR_NO_AUDIO_OUT` | `1` mixes the sound and plays nothing | `runtime_platform.cpp` |
| `DKR_NO_DEPTH` | `1` turns the depth test off | `glide_renderer.cpp` |
| `DKR_NO_MULTIPASS` | `1` draws one pass where the combiner needs two | `glide_renderer.cpp` |
| `DKR_NO_STATE_SHADOW` | `1` writes every card state, remembering nothing | `glide_backend.c` |
| `DKR_NO_TEXCACHE` | `1` converts every texture even when the card holds it | `glide_renderer.cpp` |
| `DKR_OSD` | an on-screen display of the frame's timings and the audio underruns | `glide_renderer.cpp` |
| `DKR_PAINT_WHITE` | `1` paints every vertex opaque white | `glide_renderer.cpp` |
| `DKR_PROBE` | `x,y`: the pixel the paint-stack probe watches | `glide_renderer.cpp` |
| `DKR_PROBE_SWITCH` | a thread-switch cost probe at start-up | `game_main.cpp` |
| `DKR_RDRAM_SNAPSHOT` | `copy` copies the game's memory for each frame instead of drawing from it live: a race at 50 ms a frame against 34 (patch 0058) | `events.cpp` |
| `DKR_RDRAM_SNAPSHOT_FROM` | the address the snapshot copy starts from | `events.cpp` |
| `DKR_RDRAM_SNAPSHOT_POOL` | how the snapshot buffers are allocated | `events.cpp` |
| `DKR_RDRAM_SNAPSHOT_SIZE` | the snapshot's size; four megabytes by default | `events.cpp` |
| `DKR_RENDERER` | `null` draws nothing and counts the lists; runs without a 3dfx card | `game_main.cpp` |
| `DKR_ROM_CACHE_BLOCKS` | `1` to `64`: fewer 16 KiB blocks in the ROM cache, for measuring its size (patch 0061) | `pi.cpp` |
| `DKR_SCISSOR` | `0` keeps `G_SETSCISSOR` from the card; on by default since 2 October 2026 -- without it the two-player views overlap | `glide_renderer.cpp` |
| `DKR_SDL3_INPUT_HOST` | the modern build's SDL3 input host | `runtime_platform.cpp` |
| `DKR_SHADOW_TRACE` | the modern RT64 decoder's shadow trace | `f3ddkr_rt64.cpp` |
| `DKR_SPLIT_TRACE` | the modern RT64 decoder's split-screen trace | `f3ddkr_rt64.cpp` |
| `DKR_THREADS_LOG` | `THREADS.EXE`'s log file | `test_threading.cpp` |
| `DKR_TIMING_EXPORT` | `<prefix>` writes FRAMES.BIN and AUDIO.BIN: every frame's period and render time | `timing_export.hpp` |
| `DKR_TRACE_AUDIO_ZONES` | the mixer's cost per command | `game_main.cpp` |
| `DKR_TRACE_CPU` | stamps every context switch; guest time against wall time every 5 s | `events.cpp`, `threads.cpp` |
| `DKR_TRACE_EXCLUSIVE` | runs the display list at time-critical priority, so that `render=` is processor time | `threads.cpp` |
| `DKR_TRACE_IDLE_METER` | reports the time each thread spends parked | `game_main.cpp` |
| `DKR_TRACE_LIST` | a full trace of one display list | `glide_renderer.cpp` |
| `DKR_TRACE_PAGING` | the system's paging counters every 5 s | `game_main.cpp` |
| `DKR_TRACE_RENDER_ZONES` | the renderer's cost by zone | `glide_renderer.cpp` |
| `DKR_TRACE_SAMPLER` | samples every thread's EIP for n seconds into D:\SAMPLES.BIN | `sampler.c` |
| `DKR_TRACE_SAMPLER_DELAY` | starts the sampler late; `race+<n>` waits for a race | `sampler.c` |
| `DKR_TRACE_SP` | traces the RSP task messages | `events.cpp`, `mesgqueue.cpp` |
| `DKR_TRACK_PROFILE` | the modern build's per-track profiling | `track_performance.hpp` |
| `DKR_VI_PRESENT` | how video interrupts present a frame | `events.cpp` |
