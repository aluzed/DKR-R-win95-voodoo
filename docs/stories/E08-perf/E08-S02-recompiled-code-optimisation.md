# E08-S02 — Optimising the recompiled code

| | |
|---|---|
| **Epic** | E08 — Performance |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | L |
| **Depends on** | E08-S01, E01-S05 |
| **Blocks** | — |

## Context

The recompiled code is the budget's largest item, and it is also the hardest to
optimise: it is generated, one does not edit it, and it translates the VR4300's
instructions faithfully including when that fidelity is expensive.

The levers available, from the least risky to the most:

1. **The compiler's options.** The generated code is very repetitive; the choice of
   optimisation level, of inlining strategy and of scheduling for the Pentium II can
   give a notable gain for no risk.
2. **Code layout.** A binary of several tens of megabytes on a machine whose
   instruction cache is counted in kilobytes: grouping the hot functions improves
   locality, and that is often where the largest gain is found on this class of
   machine.
3. **The recompilation policy.** `dkr.us.v77.recomp-policy.json` drives the generation.
   Some of N64Recomp's options change the code produced; examine them.
4. **Replacing functions.** The hook pipeline allows a game function to be replaced by
   a native implementation. For the rare very hot and purely computational functions,
   it is the most powerful lever — and the one that most endangers fidelity, since it
   substitutes hand-written code for translated code.

The order matters: the first three levers do not change the game's behaviour, the
fourth does.

5. **The guest register's width.** Added 21 September 2026, after the fact, because
   none of the four above covers it. N64Recomp declares `typedef uint64_t gpr`
   unconditionally, so every emitted instruction carries a 64-bit register on a
   32-bit machine. DKR uses that width in thirteen operations across three
   functions. Narrowing it removes 30.5% of the emitted x86 instructions and 31.5%
   of the instructions that reference memory — larger than any of the four levers
   above, and it is not a compiler setting but a change to the recompiler's type
   model. The measurement, the census that found the three functions, and their
   hand-written wide paths are in `docs/research/cpu-budget.md` and reproduced by
   `scripts/Measure-Narrow-Gpr.sh`.

   **Built, run, and measured at nothing — 21 September 2026.** The plumbing is
   done: `DKR_WIN95_NARROW_GUEST_REGISTER`, the three functions served by native
   paths through the policy's hooks, and a game that boots and plays its attract
   loop with every general register half as wide. On the test machine, at the same
   wall offset, guest execution time moved by **−0.6%** and the context-switch rate
   by **+0.2%**.

   The inference from the instruction count was wrong, and the measurements that
   explain why were already in this repository: the core retires one instruction
   every 4.1 cycles on a three-wide machine, so 92% of its issue capacity was
   already idle; `recomp_context` is L1-resident at either width, so the vanished
   memory accesses were hits; and the guest's own eight megabytes are touched
   identically whatever the host register is made of.

   **This retires the lever and raises a question about the others.** Lever 1 —
   compiler options — is also argued from instruction count, and now has to answer
   the same objection before it is worth measuring. The levers that survive are the
   ones that change *what the guest touches and when*: code layout for locality
   (lever 2), and anything that shrinks the working set (E08-S04).

## Objective

To reduce the recompiled code's cost, strictly preserving the game's behaviour.

## Scope

**In:** compilation options, layout, recompilation policy, targeted function
replacement.

**Out:** the graphics path (E08-S03) and memory (E08-S04).

## Work

1. Identify the hot functions from E08-S01's export, on a real play session and not on
   a synthetic loop.
2. Explore the compiler's options, measuring each variant. On this architecture,
   optimising for size may beat optimising for speed, because the cache is the limiting
   factor — that is counter-intuitive and it is measured.
3. Work on code layout: group the hot functions. If the retained toolchain allows it,
   profile-guided optimisation is the most direct way of getting there.
4. Examine the recompilation policy's options and measure their effect.
5. For the hottest functions, evaluate native replacement. The decomp
   (`extern/dkr-decomp`) supplies the original C, which makes the exercise far less
   risky than a rewrite: one compiles the original source rather than imitating its
   behaviour. Beware all the same — the neighbouring native port discovered that this
   C, under `#ifdef NON_MATCHING`, **had never been compiled by any target** and
   carried five outright defects. It is to be checked, not trusted.
6. For each replacement, prove the equivalence by comparing output over a large sample
   of inputs, against the recompiled version.
7. Measure the cumulative gain and report it to E08-S01's budget.
8. Check the game for regressions after each change: a complete play session, not just
   the startup.

## The game thread's hot functions, sampled (29 September 2026)

Work item 1, from the sampler (`DKR_TRACE_SAMPLER=90`, `DKR_TRACE_SAMPLER_DELAY=50`,
normal mode, both opt-in options, at `8e7d062`): 90 s of the **attract mode**,
7,629 samples of the game thread in which its instruction pointer moved. The
attract mode runs the game's own races and menus; it is not a player's
session, which is why the first criterion stays open.

| Share | Function |
|---:|---|
| 11.2% | `func_8002E904` |
| 5.0% | `func_800B92F4` |
| 4.4% | `waves_update` |
| 4.4% | `func_8002FF6C` |
| 3.9% | `obj_animate` |
| 3.8% | `sort_objects_by_dist` |
| 2.9% + 1.9% | `gzip_inflate_codes`, `gzip_huft_build` -- the demo reloads its levels |
| 2.5% + 1.4% | `calc_dynamic_lighting_for_object_2`, `_1` |
| 2.3% | `model_init_normals` |
| 2.2% | `calc_env_mapping_for_object` |
| 1.2% + 1.0% | `lrintf`, `do_cvt_w_s` -- float to integer for `cvt.w.s` |
| 19.9% | KERNEL32, the thread waiting |

One function carries a ninth of the thread, which makes it the first candidate
for item 5, once it is named from the decompilation (not checked out here) and
confirmed hot in a played race.

## The game thread's hot functions in a race, driven by hand (29 September 2026)

The same sampler, in a race a player started: from a cold boot, PLAYER SELECT,
TRACKS, Ancient Lake, car, time trial off, one confirmed press at a time with
`Drive-Win95-VM.sh pad-hold`, then the accelerator held with some steering.
The sampler now waits for the race itself: `DKR_TRACE_SAMPLER_DELAY=race+15`
starts the countdown when the renderer first reads `gGameMode` as INGAME, since a
route driven by hand cannot be given a fixed delay in advance. Normal mode,
`DKR_RDRAM_SNAPSHOT=none`, 90 s from 15 s after the start, 12,887 samples of the
game thread in which its instruction pointer moved:

| Share | Function |
|---:|---|
| 5.7% | `sort_objects_by_dist` -- a bubble sort of the object list by distance |
| 3.7% | `calc_env_mapping_for_object` |
| 2.8% | `calc_dynamic_lighting_for_object_2` |
| 2.0% | `func_8005B818` |
| 1.9% each | `mtxf_mul`, `obj_update`, `obj_animate` |
| 1.6-1.7% each | `render_level_segment`, `block_visible`, `func_8002DE30`, `render_level_geometry_and_objects` |
| 1.3-1.5% each | `material_set`, `shadow_update`, `func_8002FF6C`, `render_mesh`, `mtxs_transform_dir` |
| 0.8% | `func_8002E904` -- 11.2% in the attract mode |
| 20.2% | KERNEL32, the thread waiting |

**In a race the profile is flat.** The attract mode's first function, the
shadow projection `func_8002E904` (named from `tracks.c`), falls from 11.2% to
0.8%; the demo's level reloads (`gzip_*`) and `waves_update` are gone. Nothing
carries more than a twentieth of the thread, and the thread is 4.3 ms of a
frame (`frame-budget.md`): the hottest function is worth about a quarter of a
millisecond, and a native replacement of it would win less than that.

So item 5 has no candidate. `sort_objects_by_dist` is the only one that looks
replaceable -- a stable sort, which an insertion sort reproduces order for
order -- but its keys come from `get_distance_to_camera`, which would have to
stay recompiled for the floats to match, and what is left to win is the
swapping. Not worth the fidelity risk the ticket warns about.

Limits: one track, one player, eight karts, and a driver who spent part of
the race against a wall. It is still the game's race code running with a
player in it, which the attract mode was not.

## Acceptance criteria

- [x] The hot functions are identified on a real play session -- a race
      driven by hand, above. The profile is flat: 5.7% at most.
- [ ] Every compilation option is measured, not assumed.
- [ ] The effect of code layout is measured separately.
- [ ] Any native replacement is proved equivalent by comparing output over a large
      sample.
- [ ] The cumulative gain is measured and reported to the budget.
- [ ] The game behaves identically, verified by a complete session.
- [x] No native replacement is made without a prior measurement proving the function is
      hot. None is made: the race's profile has no function hot enough to justify one.
- [x] The three functions that need a 64-bit general register are identified by a
      census of the emitted opcodes, and each is proved equivalent at both widths —
      `tools/cpu-budget/wide_register_paths.h`, `tools/cpu-budget/narrow_gpr_test.c`.
- [x] The narrowed register is built and linked, and its gain measured as a running
      game rather than an instruction count — `scripts/Measure-Guest-Time-VM.sh`.
      The gain is −0.6%, which is nothing, and the reason is recorded.
- [~] The measurement is repeated away from the title screen. Driven into the
      adventure hub, the share of wall time on which guest threads are *scheduled*
      falls from 72% to 50% — which is the game thread waiting on a renderer that
      has more to do, not the recompiled code shrinking. The renderer's own clock
      gives the frame instead, and there the finding is that **`elsewhere` is flat
      at about 150 ms a frame in every scene**: the game's own cost is a floor, five
      times the whole budget, and it does not vary with what is on screen. Not a
      race, and the narrow build was not driven through the same route: see
      `docs/research/cpu-budget.md`.
- [ ] Lever 1 is re-argued before it is measured: "fewer instructions" is now known
      not to be the currency on this machine.

## Risks

Replacing functions is the most tempting and the most dangerous lever: it substitutes
hand-written code for faithfully translated code, and a difference in behaviour may
manifest only in a rare game situation. Use it only on functions the profile proves
matter, and never without proof of equivalence.

## Risks, added for the fifth lever

The narrowing is measured at nothing and is therefore off by default, which makes
the risks below latent rather than live. They are kept because the option exists and
someone may turn it on.

Narrowing the register is uniform and therefore cannot be applied to part of the
game: every function gets it. Three need a wide path and have one; the argument that
there is no fourth rests on a census of the opcodes the recompiler prints in its own
comments, which is only as good as that printing. `-Wshift-count-overflow` finds four
of the thirteen sites by itself and is worth keeping on for that reason, but it will
not find the other nine — `atan2s` in particular compiles clean and then returns the
wrong angle only when a coordinate difference exceeds 2^21, which is the failure that
survives a play-test.

## References

- `docs/research/cpu-budget.md` — the width measurement and the census
- `scripts/Measure-Narrow-Gpr.sh` — reproduces both builds and the comparison
- `runtime-recomp/dkr.us.v77.recomp-policy.json`
- `extern/dkr-decomp` — original C source
- `../../Diddy-Kong-Racing/docs/stories/E01-build/E01-S04-porter-hasm-en-c.md` —
  five defects found in the decomp's `NON_MATCHING` C
- E08-S01 — profile and budget
