# E07-S02 — Dropping ImGui, the texture packs and the telemetry

| | |
|---|---|
| **Epic** | E07 — Scope reduction |
| **Status** | TODO |
| **Priority** | P1 |
| **Estimate** | ~~M~~ **S** |
| **Depends on** | E06-S05 |
| **Blocks** | E07-S03 |

## State as of 2026-08-12 — the essential is already done by an existing switch

[E00-S01](../E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md) found that
`runtime-recomp/CMakeLists.txt` already places these components behind
`DKR_RUNTIME_BUILD_RT64`, whose default value is **`OFF`**: `runtime_ui.cpp` (ImGui),
`runtime_texture_packs.cpp`, `runtime_rice_texture_import.cpp`,
`runtime_crt_overlay.cpp`, `f3ddkr_rt64.cpp`, `rt64_renderer.cpp` and the ImGui/SDL
bridge.

Quantified consequences:

- **1,054 of the 1,089 ImGui references** (97 %) are in `runtime_ui.cpp`, already
  excluded;
- **139 of the 299 uses of `std::filesystem`** (46 %) disappear with them.

There is therefore **nothing to delete** — which preserves the modern target, which
goes on turning the switch on.

Going through the preprocessor directives line by line shows that **the decoupling from
ImGui is already complete**: the 26 remaining ImGui lines in `runtime_platform.cpp` are
all under `#if DKR_RUNTIME_HAS_RT64`, the inclusion of `imgui.h` included. Zero
references outside a guard.

Only **four SDL2 references** remain outside a guard, in two files — the public
signature of `input::poll` (`runtime_input.cpp:571-572`) and
`runtime_enhancements.cpp` (lines 10, 461, 468). They belong to E07-S03.

The telemetry (`runtime_telemetry.cpp`), for its part, is not under a guard: it is this
ticket's only real remainder.

## Context

Several of DKR-R's subsystems have no place on this target, and they represent a
considerable share of the project's code:

| Component | Size | Reason |
|---|---|---|
| `runtime_ui.cpp` | 194 KB | ImGui overlay, replaced by the `.ini` file (E06-S05) |
| `runtime_texture_packs.cpp` | 28 KB | RT64 / Rice packs — texture memory does not allow it |
| `runtime_rice_texture_import.cpp` | 9 KB | idem |
| `runtime_crt_overlay.cpp` | 10 KB | CRT filters in ImGui — moot on a real cathode-ray monitor |
| `runtime_telemetry.cpp` | 4 KB | to be reassessed, a counter display stays useful (E08-S01) |
| `save_manager.cpp` | 25 KB | ImGui graphical interface; the underlying codec is kept (E02-S05) |
| `runtime_magic_codes.cpp` | 7 KB | to be kept if it does not depend on ImGui |

The case of the texture packs deserves to be explicit: it is not an aesthetic
renunciation but a hardware constraint. A Voodoo 2 has 2 to 4 MB of texture memory per
TMU, and the original game already occupies 1.20 MB of it at the peak (E05-S02).
High-resolution textures do not fit there, however much one may want them.

The CRT filter has an irony of its own: the target machine is very probably connected
to a genuine cathode-ray monitor.

## Objective

To remove the subsystems that are moot on this target, preserving what keeps a use.

## Scope

**In:** removing these components and their dependencies.

**Out:** the Modern mode (E07-S01) and decoupling from SDL2 (E07-S03).

## Work

1. Remove `runtime_ui.cpp` and every dependency on ImGui. It is the project's largest
   removal, and it eliminates an entire external dependency.
2. Remove the texture packs and the Rice import, as well as the associated RT64 patch
   (`0011-enable-runtime-rice-texture-aliases`,
   `0012-cache-rice-replacement-decisions`) if the oracle does not depend on it.
3. Remove the CRT overlay.
4. Remove the graphical save-management interface, **keeping** `dkr_save_codec.cpp` and
   `virtual_pak.cpp`'s logic (E02-S05). The separation between the two is this ticket's
   point of vigilance.
5. Reassess the telemetry: an on-screen counter display stays precious for E08-S01.
   Keep the collection, replace the ImGui display by a minimal rendering through
   E05-S07's 2D rectangles.
6. Check the magic codes: keep them if they depend only on the configuration, remove
   them if they require an interface.
7. Sort out the corresponding test suites: `rice_texture_pack_policy_tests` goes,
   `save_manager_tests` is to be reviewed according to what is kept,
   `magic_code_policy_tests` follows step 6's decision.
8. Measure the gain in binary size and in memory.

## Acceptance criteria

- [ ] No dependency on ImGui remains in the Win95 target.
- [ ] Texture packs, Rice import and CRT overlay are removed, associated patches
      included.
- [ ] The save codec and the Controller Pak logic are kept and working.
- [ ] Telemetry collection is kept, its display replaced.
- [ ] The decision on the magic codes is taken and applied.
- [ ] The test suites are sorted out accordingly.
- [ ] The gain in binary and in memory is measured.
- [ ] No game feature is lost along the way — only port features.

## Risks

`save_manager.cpp` mixes interface and logic. Removing the interface without carrying
off the logic requires precision: an error here breaks the saves, which is the
project's least forgivable defect (E02-S05). Test it before and after.

## References

- `runtime-recomp/src/game/runtime_ui.cpp`, `runtime_texture_packs.cpp`,
  `runtime_crt_overlay.cpp`, `runtime_telemetry.cpp`, `save_manager.cpp`
- `docs/TEXTURE_PACKS.md`
- E05-S02 — texture memory budget that excludes the packs
