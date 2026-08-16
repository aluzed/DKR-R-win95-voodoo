# E07-S03 — Decoupling from SDL2

| | |
|---|---|
| **Epic** | E07 — Scope reduction |
| **Status** | IN_PROGRESS |
| **Priority** | P1 |
| **Estimate** | M |
| **Depends on** | E06-S01, E06-S02, E06-S03, E07-S02 |
| **Blocks** | E09-S03 |

## Context

SDL2 is present in ten of the project's files, and across all four layers at once:
window, inputs, audio, and integration with RT64. E06 wrote the Win32 replacements for
each. What remains is to cut the link cleanly.

The wrong reflex would be to duplicate each file in two versions, one SDL2 for the
modern target, the other Win32 for the Win95 target. The two would drift, and the
oracle would lose its value: comparing two implementations that have diverged no longer
proves anything (E00-S07).

The right structure separates **what depends on the platform** from **what does not**,
and duplicates only the first. The input mapping logic, the audio mixing policy, the
presentation policy are portable code that must stay single.

## Objective

To get SDL2 out of the Win95 target, without duplicating the portable logic or breaking
the modern target.

## Scope

**In:** the platform / logic separation, and selection at compile time.

**Out:** writing the Win32 implementations (E06).

## Work

1. Survey each use of SDL2 in the ten files concerned and classify it: **platform** (to
   be abstracted) or **logic** (to be kept as it is).
2. Define a minimal platform interface covering window, inputs, audio and time — the
   smallest that covers both implementations. It will be narrow, because the portable
   logic was set aside in step 1.
3. Extract from `runtime_input.cpp` (25 KB) the mapping logic, which must stay single
   and shared.
4. Do the same for the audio: the mixing policy (`audio_mix_policy.hpp`) and the
   equaliser are portable; only the output is not.
5. Implement the interface twice: SDL2 for the modern target, Win32 for the Win95
   target. Selection at compile time, with no branch at run time.
6. Check that the modern target behaves exactly as before. It is the condition for the
   oracle to keep its value.
7. Check that no SDL2 symbol is imported by the Win95 binary — E01-S04's guard rail
   does it automatically.
8. Run the test suites that are kept on both targets.

## Acceptance criteria

- [x] Every use of SDL2 is classified as platform or logic.
- [~] The platform interface is minimal — one function — and covers both
      implementations **for the window**. Inputs, audio and time await E06.
- [x] The logic stays single: nothing has been duplicated, and two portable pieces
      shut behind a platform guard have been taken out of it.
- [x] Selection happens at compile time, with no branch at run time.
- [~] The modern target passes its **18 suites**. The behaviour with RT64 turned on is
      not verified here, for want of SDL2 on this machine.
- [~] No file compiled for Win95 includes SDL2. The check on the binary will come when
      the game links.
- [ ] The test suites that are kept pass on both targets.

## State as of 2026-08-13 — the link is cut, one function sufficed

Step 1's survey gives a more favourable result than the ticket suggested we should
fear. **The RT64 switch had already done nearly all the work**:

| File | Occurrences | Outside the RT64 guard | Verdict |
|---|---:|---:|---|
| `runtime_platform.cpp` | 189 | 0 | platform, replaced by E06 |
| `runtime_ui.cpp`, `rt64_renderer.cpp` | 87 | — | **excluded from the build** when RT64 is off |
| `runtime_input.cpp` | 82 | 0 | platform, under a guard |
| `runtime_input.hpp`, `runtime_ui.hpp` | 8 | 8 | **forward declaration only** — no inclusion of SDL |
| `game_main.cpp`, `runtime_stubs.cpp` | 4 | 0 | under a guard |
| **`runtime_enhancements.cpp`** | **3** | **3** | **the only real link** |

A single file really depended on SDL2 outside a guard, and it wanted only **one thing**
from it: the window's size, for an aspect ratio. Everything that followed from it — the
view frustum's scale, the presentation policy — is portable logic.

Hence the interface, which holds in one function:

```cpp
bool dkr::runtime::platform::window_size(int& width, int& height);
```

Declared **outside** the guard, implemented once on each side. `runtime_stubs.cpp` was
doing exactly the same `sdl_window()` + `SDL_GetWindowSize` dance and goes through the
same accessor: the duplication the ticket warns against is removed rather than added.

Two inherited divisions, found while compiling, were rectified along the way — both of
them **portable code shut behind a platform guard**, which is the exact defect this
ticket seeks to undo:

- `RdramAddress` and `g_title_intro_tail_gate` lived under the RT64 guard in
  `runtime_stubs.cpp` whereas `dkr_title_intro_audio_tail`, which uses them, does not
  depend on it.
- `game_main.cpp`'s crash handler read the x86-64 registers by name. An i386 branch was
  added to it, and `StackWalk64` now receives the right machine type — giving it the
  wrong one would report a stack of fanciful values, which is worse than no stack at
  all.

### Result

| | |
|---|---|
| Game sources compiling for Windows 95 | **17 out of 17** |
| Modern target's suites | **18 out of 18** |
| Win95 target's suites | 4 out of 4 |
| Instruction set | none outside the Pentium II's |

**What is not verified here**: the modern target's behaviour with RT64 *turned on*, for
want of SDL2 on this machine. The 18 suites cover the portable logic, which is precisely
what this ticket was not to touch; `window_size`'s RT64 branch reproduces the removed
code identically — same null check, same call, same threshold.

The criteria that stay open belong to E06: the interface today covers only the window,
because that is all that was missing in order to compile. Inputs, audio and time will
come to it when their Win32 implementations exist.

## Risks

The risk is creeping duplication: at every difficulty, it will be tempting to copy a
file rather than extract the abstraction. The criterion of not duplicating the logic is
not a requirement of style — it is what keeps the oracle usable to the end of the
project.

## References

- `runtime-recomp/src/game/runtime_platform.cpp` (33 KB), `runtime_input.cpp` (25 KB),
  `game_main.cpp` (22 KB)
- E00-S07 — oracle strategy
- E06 — Win32 implementations
