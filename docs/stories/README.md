# Porting DKR-R to Windows 95 + 3dfx Voodoo

The development backlog. Every ticket is a self-contained file under
`docs/stories/<epic>/`.

## The project's aim

To run DKR-R — the static recompilation of Diddy Kong Racing — under Windows 95,
rendered by a 3dfx card through Glide. The player supplies their own ROM; no asset
is redistributed (`docs/ASSET_POLICY.md`).

**The chosen target**: Pentium II / III, Voodoo 2 or 3, 64 MB of RAM, Windows 95
OSR2.5 — settled by
[E00-S05](E00-scoping/E00-S05-adr-hardware-target-glide.md) and
[ADR 0002](../adr/0002-hardware-target.md). The **hardware** floor is fixed and the
test machine is now aligned with it: Voodoo 2, 2 MB of frame buffer, 2 MB per TMU.
Only the **CPU** floor stays provisional, E00-S03's go/no-go awaiting a real play
session.

## What is kept, what falls

This repository's value lies in its generated code and in its F3DDKR decoder. The
rest of the stack is specific to modern systems.

| Layer | Fate |
|---|---|
| N64Recomp output (`RecompiledFuncs`) | **kept** — portable C manipulating integers |
| Recompiled audio microcode (`aspMain`) | **kept**, vector emulation rewritten without SSE (E03) |
| F3DDKR decoder (`f3ddkr_rt64.cpp`) | **extracted** from RT64, rested on a clean interface (E04) |
| Save codec, Controller Pak, audio policy | **kept** — the project's portable code |
| `ultramodern` / `librecomp` | **patched** — system primitives substituted (E02) |
| RT64 | **replaced** by a Glide backend (E05) |
| SDL2 | **replaced** by bare Win32 (E06) |
| Dear ImGui, texture packs, CRT overlay | **removed** (E07) |
| Modern mode, interpolation, presentation identities | **removed** (E07-S01) |

## Overall status

| Epic | Title | Tickets | TODO | In progress |
|---|---|---|---|---|
| [E00](E00-scoping/) | Scoping, measurements and decisions | 7 | 5 | **2** |
| [E01](E01-build/) | 32-bit Win95 build chain | 6 | 5 | **1** |
| [E02](E02-system/) | Win95 system substrate | 6 | 2 | **4** |
| [E03](E03-rsp/) | RSP on x86 without SSE | 3 | 3 | 0 |
| [E04](E04-hle-f3ddkr/) | RT64-independent F3DDKR HLE | 8 | 8 | 0 |
| [E05](E05-glide/) | Glide backend | 8 | 8 | 0 |
| [E06](E06-platform/) | Win95 platform | 6 | 6 | 0 |
| [E07](E07-scope/) | Scope reduction | 3 | 2 | **1** |
| [E08](E08-perf/) | Performance | 4 | 4 | 0 |
| [E09](E09-qa/) | Integration, QA and distribution | 5 | 4 | **1** |
| | **Total** | **56** | **48** | **8** |

### In progress

| Ticket | State |
|---|---|
| [E09-S01](E09-qa/E09-S01-emulated-test-environment.md) | `REVIEW` — **a complete environment**: Windows 95 OSR2.5 on a Pentium II / Voodoo 2, 3dfx driver installed, **a Glide demonstration rendering a Gouraud triangle**, a reference snapshot frozen, and a machine drivable without a screen. |
| [E00-S04](E00-scoping/E00-S04-spike-rsp-cost-without-sse.md) | `REVIEW` — **the recompiled audio microcode cannot hold real time**: the target reaches only **3.9 %** of the RSP's vector throughput. The scalar fallback already existed; MMX would not save this path. **[E03-S03](E03-rsp/E03-S03-high-level-mixer-fallback.md) moves from contingency to critical path.** |
| [E07-S03](E07-scope/E07-S03-sdl2-decoupling.md) | `IN_PROGRESS` — the link with SDL2 is cut: **a single file depended on it outside the RT64 guard**, and all it wanted was the window's size. One function, `platform::window_size`, suffices; `runtime_stubs.cpp` goes through the same accessor rather than duplicating it. **The game's 17 sources compile for Windows 95**, and the modern target's 18 suites pass. |
| [E01-S05](E01-build/E01-S05-compiling-the-recompiled-code.md) | `IN_PROGRESS` — **the recompiled code compiles and links for Windows 95**: 37 files out of 37, a 4.23 MB PE that passes both guard rails, `aspMain.cpp` included and without a stub. No absent extension — the 64-bit arithmetic goes through libgcc. The comparison against the oracle **agrees bit for bit on the functions it reaches**, faults included; it stops on a fault Windows 95 does not deliver as a signal. **`librecomp` compiles as well** — 26 units out of 26 — E01-S02's "six errors" coming down to a single `static_assert` plus include paths, and `allocation_size`, which was **zero** in 32-bit, now follows the target. |
| [E02-S05](E02-system/E02-S05-eeprom-and-controller-pak-saves.md) | `DONE` — the durable-write layer is delivered, **40 checks without a failure on the target**, the power cuts being simulated rather than waited for. The ticket was right about `MoveFileEx`, but the form of its unavailability reveals a **third category of absent API**: exported, with real code, and refusing at run time — which neither the import check nor the stub survey can see. **The rule banning `<filesystem>` was wrong**: the include and the `path` type cost nothing and `path` works on the machine; only the ~140 operations count, not the 250 uses of the type. `ultramodern` no longer has a single forbidden include. Patch 0018 routes **the 10 operations at `librecomp`'s core** without touching the type; what remains is the mod system, which this port does not need. |
| [E02-S03](E02-system/E02-S03-clock-timers-and-pacing.md) | `IN_PROGRESS` — the time base is delivered and measured: `QueryPerformanceCounter` at **1,193,180 Hz, that is the 8254 PIT**, 4.19 µs, 200,000 reads without a single step backwards, and a **drift of −0.0000 % over 300 s** on the target. Two of the ticket's assumptions are disproved (`GetTickCount` is at 9 ms and a hundred times cheaper; `timeBeginPeriod(1)` changes nothing here). A defect found along the way: `ultramodern` derives `osGetCount` from `high_resolution_clock`, which is **the wall clock** on this toolchain — wiring it onto this base is therefore justified by measurement. |
| [E02-S02](E02-system/E02-S02-ultramodern-scheduler.md) | `IN_PROGRESS` — patch 0015: `ultramodern`'s five primitives go through a seam that the target fills with E02-S01's layer. **`ultramodern` compiles for Windows 95, 15 files out of 15**, the modern targets unchanged, forbidden includes down from **9 to 1**. `thread_local` works on the target, measured. Points 5 to 7 stay blocked by E01-S05, E02-S05 and E07-S03. |
| [E02-S01](E02-system/E02-S01-threading-and-synchronisation-layer.md) | `REVIEW` — the threading layer is complete: threads, locks, semaphore, **condition variable**, events, TLS. **48 checks without a failure under emulated Windows 95**, a 600 s endurance run (8,437 rounds). Two invisible blockers found: `CreateSemaphoreW` and `GetHandleInformation` are exported by Windows 95 but **empty**, which breaks moodycamel's semaphore and `std::thread::join()`; the import guard rail now checks the empty exports too. The initial survey, made on a worktree that a broken `apply-dependency-patches.sh` left without its first thirteen patches, had wrongly concluded that no condition variable was needed. |
| [E00-S03](E00-scoping/E00-S03-spike-recompilation-cpu-budget.md) | Both factors are measured: **2.16×** for the 64 → 32 bit move without SSE, **17.7×** for the normalisation towards the 400 MHz Pentium II — that is **≈ 38×** between the development machine and the target. Go/no-go not pronounced: what is now missing is the CPU cost of a game frame, which requires [E02-S06](E02-system/E02-S06-game-bring-up.md). |

### Gained along the way

| Deliverable | Scope |
|---|---|
| [`docs/research/cpu-budget.md`](../research/cpu-budget.md) | E00-S03's measurements, method and limits |
| [`docs/research/rsp-audio-budget.md`](../research/rsp-audio-budget.md) | E00-S04's measurements — the figure that triggers E03-S03 |
| [`docs/TEST-ENVIRONMENT.md`](../TEST-ENVIRONMENT.md) | The recipe for the emulated environment and its known limits |
| `scripts/Setup-Win95-Toolchain.sh` | MIPS toolchain, cmake, ninja, uv — **without root privileges** |
| `scripts/generate_recomp_toml.py` | N64Recomp configuration from the policy, a Linux port of the PowerShell script — advances [E01-S06](E01-build/E01-S06-generating-sources-off-windows.md) |
| `scripts/Setup-Win95-TestVM.sh`, `prepare_win95_install.py`, `Run-Win95-VM.sh`, `Drive-Win95-VM.sh`, `Push-To-Win95-VM.sh` | The test machine: setting up, preparing the installation from the player's ISO, launching, driving without a screen, transferring files |
| `tools/win95/azerty_keys.py` | Translates text into physical keys for an AZERTY guest — without which no file path can be typed |
| `scripts/patch_voodoo2_inf.py` | 86Box exposes its Voodoo 2 with the Voodoo 1's PCI identifier; the original driver does not recognise it without this fix |
| `tools/win95/glidetest.c` + `build-glidetest.sh` | The Glide demonstration — and the **first 32-bit PE without a CRT or SSE to run under Windows 95**, which advances [E00-S02](E00-scoping/E00-S02-spike-pe-win95-toolchain.md) |
| `tools/cpu-budget/` | A replayable bench for the recompiled code, **on the host and on the target machine** |
| `patches/n64recomp/0002-…` | Portable 64×64→128 multiplication: **unblocks the whole 32-bit target** |

**The project's oracle is buildable under Linux.** The reference decomp's ELF now
builds without Windows, and the ROM produced is identical to the bit to the
player's — the neighbouring decomp nonetheless gave its MIPS toolchain up as
absent.

## Order of attack

Two tickets come **before everything else**, and for opposite reasons.

[**E09-S01**](E09-qa/E09-S01-emulated-test-environment.md) — the emulated test
environment. It is classed under QA by theme, but it is a practical prerequisite:
without a Windows 95 machine restorable in a few seconds, everything else is
developed blind. It even conditions E00-S02.

[**E00-S03**](E00-scoping/E00-S03-spike-recompilation-cpu-budget.md) — the CPU
budget. It decides whether the project is feasible on this class of machine, and it
carries an explicit **go / no-go**. Discovering it now costs a week; discovering it
after E04 and E05 costs three months.

```
E09-S01 (test machine)
   │
E00 (scoping) ── E00-S03: GO / NO-GO ───┐
   │                                     │ if no-go: floor raised,
   │                                     │ or a switch to the native port
   ├──> E01 (32-bit build) ──> E02 (system substrate)
   │                                │
   │                          E02-S06: the game runs under Win95
   │                                │   (without an image - diagnostic renderer)
   │                                │
   ├──> E07 (scope reduction, in parallel)
   │                                │
   │            E04 (F3DDKR HLE) ───┤
   │                  │             │
   │            E04-S08 (software rasteriser)
   │                  │
   │            [FIRST IMAGE]
   │                  │
   │            E05 (Glide backend) <── E09-S02 (visual comparison)
   │                  │
   ├──> E03 (RSP) ──> E06 (Win95 platform)
   │                  │
   │            [PLAYABLE GAME]
   │                  │
   └──> E08 (performance) ──> E09 (validation, packaging)
```

E03 (RSP / audio) is largely independent and can be carried out in parallel.

### The three verifiable milestones

| Milestone | Ticket | What it proves |
|---|---|---|
| The game runs | [E02-S06](E02-system/E02-S06-game-bring-up.md) | The build, the system substrate and the scheduler hold. No image, but graphics tasks submitted at a measurable rate. |
| The first image | [E04-S08](E04-hle-f3ddkr/E04-S08-reference-software-rasteriser.md) | The F3DDKR decoder is right, independently of Glide. Becomes E05's oracle. |
| The game is playable | E05 + E06 | Accelerated rendering, input, audio, pacing. |

## The hard point

[**E05-S03**](E05-glide/E05-S03-color-combiner-translation.md) — the colour
combiner's translation. The RDP's combiner is programmable; Glide's is fixed. It is
the only ticket estimated XL in the graphics part.

What makes it tractable: DKR uses only **33 configurations**, of which **only 3
read two texels** — an inventory already established by the neighbouring native port
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`), to be rechecked by
[E04-S06](E04-hle-f3ddkr/E04-S06-rdp-state.md). Thirty-three enumerated cases, whose
frequency and screen area are known: a finite problem.

That is also why E04-S08 is a prerequisite and not a comfort. Without an oracle
implementing the combiner faithfully, E05-S03 is done by judgement, and the error
accumulates without being attributable.

## Estimated distribution of effort

| Area | Share |
|---|---|
| F3DDKR HLE + Glide backend (E04, E05) | ~35 % |
| Build, system substrate, RSP (E01, E02, E03) | ~30 % |
| Platform and scope reduction (E06, E07) | ~15 % |
| Scoping, performance, QA (E00, E08, E09) | ~20 % |

## Non-negotiable constraints

1. **Never modify a dependency worktree directly.** `ultramodern`, `librecomp`,
   `N64Recomp`, RT64, `RecompiledFuncs` and `RecompiledPatches` are regenerated — a
   direct edit disappears without warning. Everything goes through
   `patches/manifest.json` (`docs/ARCHITECTURE.md`).
2. **No asset redistributed.** The ROM comes from the player; the package is
   scanned before distribution.
3. **32-bit, without SSE.** Two automatic guard rails impose it: the instruction-set
   check
   ([E01-S01](E01-build/E01-S01-cmake-i686-toolchain-without-sse.md)) and the PE
   import check
   ([E01-S04](E01-build/E01-S04-pe-import-guard-rail.md)).
4. **The oracle stays buildable.** The modern target is the only executable
   reference that tells us whether the port is *right*. Keeping it is settled by
   [E00-S07](E00-scoping/E00-S07-adr-oracle-branch-strategy.md); breaking it without
   need is losing the means of verification.

## What the neighbouring repository already brings

`/var/www/Diddy-Kong-Racing` carries a **native** port of the same game to the same
target, from the decomp rather than by static recompilation. Its "Voodoo95" backlog
has several deliverables directly reusable here:

| Deliverable | Used by |
|---|---|
| `docs/research/combiner-inventory.md` — 33 configurations, 3 with two texels | E04-S06, E05-S03, E05-S04 |
| `docs/research/level-working-set.md` — peak of 1.20 MB per level | E00-S05, E05-S02 |
| Defects in the decomp's `NON_MATCHING` C | E08-S02 |
| Architectural arbitrations (fork, oracle, reference rasteriser) | E00-S07, E04-S08 |

It is also the **fallback** if E00-S03 concludes no-go: its approach does not carry
the recompiled code's translation overhead, at the price of far heavier work on
everything else.

## Conventions

### Statuses

| Status | Meaning |
|---|---|
| `TODO` | Not started |
| `IN_PROGRESS` | Under way |
| `BLOCKED` | Blocked — the reason and the blocking ticket are noted in the ticket |
| `REVIEW` | Implemented, awaiting validation |
| `DONE` | Validated against its acceptance criteria |

The status is updated **in the ticket's own file** (the `Status` field) **and** in
the table above. A ticket moves to `DONE` only when all of its acceptance criteria
are ticked.

### Priorities

- **P0** — critical path, blocks other epics
- **P1** — necessary for a playable game
- **P2** — quality, comfort, optimisation
- **P3** — optional

### Estimates

`S` ≤ 1 day · `M` 2–4 days · `L` 1–2 weeks · `XL` > 2 weeks

## Cross-cutting references

- `docs/ARCHITECTURE.md` — execution path and protected boundaries
- `docs/F3DDKR.md` — microcode → renderer bridge
- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — RDRAM snapshot and identities
- `docs/BUILDING.md`, `docs/ASSET_POLICY.md`
- [3dfx Glide sources](https://sourceforge.net/projects/glide/) ·
  [sezero/glide](https://github.com/sezero/glide) ·
  [hatarch/glide3x](https://github.com/hatarch/glide3x)
- PCem · 86Box — emulation of period machines with 3dfx cards
