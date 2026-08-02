# Modern Presentation Milestone

## Objective

Add an opt-in Modern preset that can present smooth motion from 30 FPS through
500 FPS without changing Diddy Kong Racing's authored simulation rate, audio
timing, input semantics, saves, collision, animation state, or deterministic
gameplay. The frozen Accurate preset remains the default, release fallback, and
regression oracle.

This milestone is not an invitation to edit generated recompilation output or
third-party submodules. All game-facing changes must enter through the existing
patch pipeline and project-owned runtime integration.

## Architecture decision

The current port is a static recompilation port. N64Recomp-generated native CPU
functions are linked to N64ModernRuntime, project-owned platform and UI code,
recompiled Rare RSP microcode, and the F3DDKR-to-RT64 renderer. The user supplies
a legally owned ROM for validation and game data; the runtime does not interpret
the original MIPS CPU instructions one at a time.

The upstream DKR project is now a complete decompilation, so a conventional
source port is technically possible. That would replace the generated CPU layer
with directly compiled game source, but it would still require platform
replacements for libultra, scheduling, input, audio, saves, asset loading,
renderer/microcode behavior, endian assumptions, memory layout, and host pointer
width. It is a separate migration, not a low-risk change to the current runtime.

For this milestone, retain the proven recompilation architecture:

1. Keep `first-public-build-accurate` immutable and shippable.
2. Implement Modern presentation as an opt-in layer around the same authoritative
   game simulation.
3. Use names and lifecycle boundaries from the complete decompilation to place
   precise patch hooks and stable interpolation identities.
4. Evaluate a separate direct-source-port branch after the first public release;
   do not mix that migration with frame interpolation.

## Research conclusions

### Lighthouse

Lighthouse demonstrates the desired presentation model:

- The game produces authored simulation frames at its original cadence.
- A renderer-side frame history records two valid endpoints.
- Stable hierarchical identifiers pair transforms across endpoints.
- Translation, scale, rotation, camera position, and projection are interpolated
  separately.
- Angle interpolation uses the shortest path.
- Camera cuts, teleports, reordered objects, sprites, and other unsafe cases can
  explicitly opt out.
- Frame data uses owned ring-buffer lifetimes so the game cannot overwrite data
  still used by a render thread.
- Intermediate frames are presented at the selected display rate; the final
  endpoint remains the exact authored frame.

### Golden Balloon

Golden Balloon is valuable both as a DKR-native source-port reference and as a
warning. Its current release deliberately does not replay delayed display lists
for motion interpolation because display-list commands can retain pointers to
viewport, matrix, vertex, texture, and nested-list memory that the next game task
mutates. It requires immutable ownership of both endpoint snapshots before that
approach is safe.

Its fixed-step host scheduling, simulation/presentation separation, broad refresh
rate test matrix, ROM-free release guards, provenance records, and explicit
live-versus-restart configuration behavior should be adopted.

## Non-negotiable invariants

- The game simulation advances exactly 30 authored ticks per second in normal
  NTSC gameplay, irrespective of the presentation target.
- Audio is clocked by game/audio time, never by the number of presented frames.
- Input is sampled according to a documented fixed-step policy and is never
  applied multiple times because extra visual frames were rendered.
- Save data produced by Accurate and Modern is mutually compatible.
- At every authored endpoint, Modern renders the same state as Accurate.
- Modern failure is fail-closed: show the newest authored frame rather than
  extrapolating, stalling the game, or rendering invalid geometry.
- Accurate does not call experimental interpolation code.
- No busy-spin frame limiter or unbounded GPU queue is permitted.
- Sky/background fixes, water, transitions, HUD, trails, skidmarks, wheels,
  propellers, and screen-space effects must not regress.

## Phase 0 — Freeze the release oracle

Status: complete.

- Source commit: `6640221f25706b7c0916e7f5efebf53950c53dbf`
- Tag: `first-public-build-accurate`
- Complete Git bundle and ROM-free source archive created.
- ROM-free Windows x64 package created.
- Linux x86_64 AppImage created.
- Windows tests and Linux package self-tests passed.

No Modern work may modify or move this tag.

## Phase 1 — Formalise the preset contract

1. Keep Accurate as the clean-install and invalid-config fallback.
2. Accurate forces authored presentation cadence and disables interpolation,
   maximum-detail overrides, extended view distance, and other Modern-only
   experiments.
3. Preserve proven correctness fixes in both presets. Accurate is not synonymous
   with forced 4:3 output or reintroducing original rendering defects.
4. Modern exposes:
   - target presentation rate: 30 to 500 FPS;
   - Match Display mode;
   - VSync and frame-latency policy;
   - high-detail and extended-distance options;
   - Modern visual presentation options described below.
5. Store a separate remembered Modern rate so switching to Accurate never
   destroys the user's Modern preference.
6. Mark settings as Live, Scene Reload, or Restart Required in the launcher and
   in-game overlay.
7. Add one-button `Restore Accurate Defaults` recovery.

Exit gate: a config test proves that clean, missing, legacy, truncated, and
out-of-range settings all start safely in Accurate.

## Phase 2 — Instrument authoritative time

Before interpolating anything, add diagnostics that separately count:

- simulation ticks;
- audio tasks/samples and queue depth;
- input samples consumed by simulation;
- display-list/task submissions;
- interpolated presentations;
- dropped or repeated presentations;
- GPU queue depth and frame latency;
- interpolation invalidations and their reason.

Use monotonic host time and a rational/fixed-point phase accumulator. Avoid
integer `1000 / fps` deadlines, which drift at 59.94 Hz and other fractional
rates. On pause, focus loss, sleep, debugger stops, display changes, and overlay
suspension, rebase the presentation clock rather than trying to catch up.

Exit gate: over a 30-minute automated run, Accurate and Modern execute the same
number of simulation ticks and audio samples for identical scripted input.

## Phase 3 — Build immutable renderer snapshots

This is the safety-critical phase.

1. Define a project-owned `RenderSnapshot` containing every value needed after
   game memory is released for the next task:
   - viewport and scissor state;
   - projection/view and object transforms;
   - vertices used by dynamic geometry;
   - referenced texture identity and lifetime;
   - nested display-list or decoded draw-command ownership;
   - fog, lights, material/combiner state, and render flags;
   - camera and scene identity;
   - semantic transform IDs.
2. Prefer decoded/renderer-level immutable state over retaining raw RDRAM
   pointers. If a raw reference remains, copy or pin its complete dependency.
3. Use a minimum four-slot ring with explicit Free, Recording, Ready, and InUse
   states plus renderer claim/release fences.
4. Never overwrite a snapshot while RT64 or the presentation thread can read it.
5. Add generation counters and assertions for stale handles, pointer escape,
   duplicate ownership, and incomplete capture.
6. If either endpoint is invalid, present the current authored endpoint with no
   interpolation.

Exit gate: ThreadSanitizer/AddressSanitizer where supported, stress tests, and
snapshot poison tests show no access to mutable or reclaimed task memory.

## Phase 4 — Give transforms deterministic identities

Pairing solely by display-list order, matrix address, allocation address, or
vertex pointer is unsafe. Use decomp-informed patch-pipeline hooks to assign
stable hierarchical IDs at meaningful render boundaries:

- scene and camera;
- racer, vehicle body, wheels, propeller, and hovercraft attachments;
- track and hub objects;
- characters and animated model limbs;
- doors, balloons, balloon strings, weapons, pickups, and bosses;
- particles, sprites, billboards, shadows, trails, and skidmarks.

Each ID combines scene generation, object lifecycle generation, render kind, and
subcomponent index. Heap-address reuse must not revive a dead object's history.
Detect collisions in development builds and disable interpolation for both
colliding objects rather than guessing.

Exit gate: stable-ID capture/replay produces identical endpoint object sets for
representative intro, menu, hub, race, boss, and cutscene scenes.

## Phase 5 — Interpolate conservative object classes

Introduce interpolation in controlled tiers:

1. Camera position and orientation, with shortest-path angles.
2. Opaque static and rigid world objects.
3. Racer bodies and independently identified vehicle attachments.
4. Skeletal/animated model transforms.
5. Billboards and sprites using a dedicated path.
6. Carefully approved effects and dynamic geometry.

Use translation and scale lerp and quaternion or shortest-path rotation. Never
interpolate across scene changes, camera cuts, warps, teleports, respawns, menu
changes, object creation/destruction, or a large configurable discontinuity.

Initially force endpoint rendering for:

- HUD and all screen-space UI;
- transitions and full-screen fills;
- sky and lower-horizon/background passes;
- water/background coverage helpers;
- track-select and menu backgrounds;
- particles with unstable topology;
- trails, skidmarks, decals, shadows, and framebuffer effects.

Those classes are enabled only after their own lifecycle and midpoint tests.
This protects the fixes already accepted in Accurate.

Exit gate: the endpoint at interpolation factor 1.0 hashes or captures identically
to the non-interpolated authored frame, within documented renderer tolerances.

## Phase 6 — Presentation pacing from 30 to 500 FPS

1. Keep simulation at authored cadence.
2. Generate presentation timestamps with a rational accumulator for 30, 50,
   59.94, 60, 75, 90, 100, 120, 144, 165, 240, 360, 480, and 500 FPS.
3. Match Display resolves against the active monitor and updates safely when the
   window changes monitor or the display mode changes.
4. Cap the CPU/GPU submission rate; wait on timers, VSync, fences, or the window
   event loop instead of spinning.
5. Bound frames in flight to two by default, with a tested maximum of three.
6. Drop an intermediate presentation when late; never run extra simulation ticks
   or queue an unbounded backlog to catch up.
7. Support non-integer ratios such as 30-to-50 and 30-to-144 without periodic
   speed changes or phase drift.
8. Preserve intentional slow-cadence scenes and cutscenes by interpolating only
   between their valid authored endpoints.
9. Show a launcher warning above the current display rate: it can reduce input
   latency but consumes additional CPU/GPU power and may not improve visible
   motion.

Exit gate: no fan-spiking busy loop, no runaway queue, stable frame times, and
identical gameplay/audio clocks at every target rate.

## Phase 7 — DKR-specific regression closure

Run focused tests on historically fragile content:

- Nintendo/Rare and title sequences;
- character-select music ownership: exactly the selected character layer plays,
  with no carried intro layer or accumulating voices;
- track-select backgrounds;
- every sky and lower-horizon variant;
- every water map, shoreline, waterfall, and camera pitch/yaw case;
- transition overlays at 4:3, 16:9, 21:9, and 32:9;
- wheels, plane/hovercraft propellers, and other vehicle attachments;
- Golden Balloon string/ribbon geometry;
- sprites, billboards, particles, shadows, trails, and skidmarks;
- portals, boss arenas, cutscenes, respawns, warps, and scene changes;
- one through four local viewports where supported.

The Golden Balloon string is original presentation, not a Modern enhancement. If
it is still absent, fix it through the patch pipeline in both presets before the
Modern release candidate.

Exit gate: no open P0/P1 visual, audio, timing, save, crash, or controller bugs.

## Phase 8 — Release-safe Modern quality-of-life options

Add these behind Modern, independently toggleable where practical:

- maximum model detail / disable LOD substitution;
- wider but still correct ultrawide culling;
- view-distance multiplier, initially 1x through 6x;
- projection-aware horizontal FOV options with a conservative default;
- HUD safe-area and scale controls;
- internal render scale/resolution;
- MSAA, texture filtering, and anisotropic filtering where the RT64 backend
  supports them safely;
- borderless fullscreen and reliable monitor selection;
- controller deadzone, sensitivity, rumble strength, and full remapping;
- launcher and overlay navigation entirely by controller;
- clear apply/revert countdown for display changes.

Ultrawide culling must expand the horizontal frustum and relevant spatial/portal
selection. It must not simply disable every cull or force a nominal 200-degree
camera FOV, which would waste work and distort perspective. Geometry directly
behind the camera can remain culled; objects visible through any viewport edge
must remain resident and drawable.

Gameplay-changing cheats, free camera/photo mode, speedrun tools, randomizers,
and mod APIs belong in a later Enhancements or Tools section rather than the core
Modern preset.

## Phase 9 — Verification matrix

### Automated

- interpolation math, wraparound angles, camera cuts, and teleport thresholds;
- stable-ID uniqueness, lifecycle reuse, and collision fail-closed behavior;
- snapshot ownership, queue backpressure, and generation rollover;
- config migration, persistence, defaults, and recovery;
- 30/50/59.94/60/75/90/100/120/144/165/240/360/480/500 pacing;
- long-run tick/audio equality and suspension rebasing;
- endpoint image/state comparison and selected midpoint golden images;
- save/pak round trips and recovery;
- ROM/copyrighted-asset scan for every package.

### Manual, visible playtests

Every risky renderer phase is tested in a visible build with the player—not by a
hidden smoke test. Cover all three vehicle types, Adventure and race modes,
menus, character select, track select, representative bosses and cutscenes, and
the historically fragile scenes listed above.

Test 4:3, 16:9, 21:9, and 32:9; window resize; fullscreen transitions; monitor
move/hotplug; overlay open/close; focus loss; and controller-only navigation.

Run at least one 30-minute automated soak and one two-hour manual soak. Monitor
audio drift, simulation count, frame-time percentiles, memory growth, GPU queue
depth, and interpolation invalidations.

## Phase 10 — Release process

1. Ship Modern first as an opt-in beta while Accurate remains the default.
2. A failure in Modern must be recoverable from the launcher without starting
   the game.
3. Build from a clean tree and record commit, dependency commits, compiler,
   target, artifact hashes, tests, and manual GPU coverage.
4. Produce ROM-free Windows x64 and Linux x86_64 AppImage artifacts for every
   release candidate.
5. Run automated asset scans and package self-tests.
6. Complete physical Windows and Linux GPU/audio/controller testing.
7. Tag the release candidate without changing the Accurate baseline tag.
8. Keep the baseline bundle and packages adjacent to the Modern artifacts for an
   immediate rollback.

## Definition of done

The Modern milestone is complete only when:

- 60, 120, 144/165, 240, 360, and 500 FPS presentation is visibly smooth when
  hardware can sustain it;
- gameplay, audio, input, collision, animation state, and saves remain on the
  Accurate timeline;
- authored endpoints match Accurate;
- no known sky, lower-horizon, water, transition, culling, billboard, attachment,
  particle, trail, skidmark, or HUD regression remains;
- frame pacing does not busy-spin or build an unbounded queue;
- Accurate remains the clean default and can be selected as an immediate safe
  fallback;
- both Windows and Linux AppImage packages pass automated and physical release
  qualification.

