# E01-S02 — An enforced C++ subset and the standard library's dependencies

| | |
|---|---|
| **Epic** | E01 — 32-bit Windows 95 build chain |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | ~~L~~ **M** |
| **Depends on** | E00-S01, E00-S02, E01-S01 |
| **Blocks** | E02-S01, E02-S02, E04-S01 |

## State as of 2026-08-12 — delivered

[`docs/CPP-SUBSET.md`](../../CPP-SUBSET.md), with the
`tools/win95/check-cpp-subset.py` checker wired in as a **pre-build** step.

### The count, the only honest measure of progress

| Dependency | Files | Errors | Remaining |
|---|---:|---:|---|
| **`ultramodern`** | 15 | **0** | — |
| **`librecomp`** | 26 | **6** | accounted for below |

**`ultramodern` compiles for Windows 95** with a single two-hunk patch,
`patches/n64-modern-runtime/0014-build-for-windows-95-targets.patch`:

- `std::quick_exit` does not exist — mingw only declares it under `_UCRT`, and the
  target links the legacy msvcrt. Replaced by `std::_Exit`, which the `__APPLE__`
  branch just above already chooses;
- `SetThreadDescription` is from Windows 10 and does nothing but name a thread for a
  debugger that does not exist here. The call is removed, not emulated.

Verified on both targets: **0 errors on Windows 95, 0 errors on the 64-bit Linux
host.** A patch that breaks upstream breaks the oracle.

E00-S01's prediction — "`ultramodern` gets patched, it does not get rewritten" — is
confirmed: two hunks.

**`librecomp`: 6 errors, all accounted for.**

| Error | × | Nature | What follows |
|---|---:|---|---|
| `static_assert(sizeof(std::size_t) == 8)`, `mods.hpp:53` | 4 | a real 64-bit assumption | the mod system, a fork candidate |
| `rabbitizer.hpp` not found | 2 | dependency not fetched | E01-S05 |

### Compiling is not loading

`ultramodern` compiles without an error **and stays unloadable**: it still includes
`<thread>`, `<mutex>` and `<filesystem>`, that is **9 forbidden includes** which the
checker reports. A forbidden include compiles perfectly and only shows itself when
loading on the target machine.

That is exactly **E02-S01**'s work, and it is why the check runs as a pre-build step
on the target's sources rather than as a post-link one.

### There is no standard to restrict

C++20 is permitted **in full**. `<format>`, `<ranges>`, `<span>`, `<bit>` and
`<atomic>` are measured at **0 absent symbols**. What is forbidden comes down to the
import table, not to the dialect.

**Exceptions and RTTI kept**: the T3b witness exercises them under Windows 95. A
detail that explains a great deal: they are what makes `libstdc++` import
`GetThreadId` — one therefore cannot escape the compatibility layer by avoiding
`std::thread`.

### One correction

`docs/research/win95-blockers.md` asserted that no 64-bit assumption remained. That
was false: my search used `sizeof(size_t)` without accepting the `std::` prefix.
Actually compiling `librecomp` revealed it. A corrected sweep finds only one, and
the document is amended.

## Earlier state — the initial question was badly put

[E00-S01](../E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md)
measured, and this ticket's premise does not hold: **there is no C++ subset to
impose.** GCC 13 targets i686 PE32 and implements all of C++20; concepts,
`<ranges>`, `<span>`, `consteval` and `operator<=>` cost nothing at run time and
block nothing.

What blocks are **library facilities**, measured by import table:

| Facility | APIs absent from Win95 |
|---|---:|
| `printf`, `std::atomic` | **0** |
| `std::chrono` | 2 |
| `std::mutex`, `condition_variable` | 6 |
| `std::thread` | 7 |
| `std::filesystem` | **13** |

The ticket must therefore be reworded: not "which dialect to forbid ourselves", but
**"which library facilities to replace"** — in practice `std::filesystem` (400 call
sites, of which 139 disappear with RT64 off) and the threading layer (E02-S01).

Detail: [`docs/research/win95-blockers.md`](../../research/win95-blockers.md).

## Context

E00-S02's ADR fixes the C++ standard available. Two very different outcomes:

- **C++17 or C++20 hold** — `ultramodern` and `librecomp` get patched at the
  margins, and the bulk of the work moves to the standard library: which parts of
  libstdc++ or libc++ really work under Windows 95, in particular threads and
  synchronisation.
- **C++98 only** — `ultramodern` and `librecomp` are to be rewritten, and the
  project doubles in size. The per-file count of C++20 constructs established in
  E00-S01 then says what to rewrite first.

In both cases a rule must be written and then **tooled**: without an automatic
check, a forbidden construct is reintroduced at the first contribution and is only
discovered at link time, or worse, at run time.

## Objective

To fix the C++ subset permitted for the Win95 target, to enforce it mechanically,
and to make `ultramodern` and `librecomp` compilable within that subset.

## Scope

**In:** the rule, its tooling, and the dependency patches needed.

**Out:** the system layer itself (E02-S01) and the rendering code (E04, E05).

## Work

1. Write `docs/CPP-SUBSET.md`: permitted standards, permitted standard headers,
   forbidden headers with the replacement to use for each. The candidates for
   prohibition, to be confirmed by E00-S02's measurement rather than by presumption:
   `<thread>`, `<mutex>`, `<condition_variable>`, `<filesystem>`, `<format>`,
   `<ranges>`, `<latch>`, `<barrier>`, `<semaphore>`.
2. Decide on the use of exceptions and RTTI. E00-S02's T3 witness already has the
   answer for the retained toolchain. Disabling them reduces the binary's size —
   which counts on this target — but requires checking that the code kept does not
   depend on them.
3. Write the subset checker: a script that walks the Win95 target's sources and
   fails on a forbidden header. Wire it in as a pre-build step.
4. Compile `ultramodern` with E01-S01's toolchain and deal with the errors in
   decreasing order of frequency. Every correction goes through
   `patches/n64-modern-runtime/`, never through a direct edit of the worktree —
   that is the repository's rule (`docs/ARCHITECTURE.md`).
5. The same work for `librecomp`, setting aside its RSP vector emulation: that is
   E03-S01's business and must not block this ticket.
6. Push everything that belongs to threads and synchronisation behind E02-S01's
   interface, rather than fixing it in place. This ticket prepares the ground;
   E02-S02 fills it.
7. Keep the count of remaining compilation errors, dependency by dependency, in the
   ticket. It is the only honest measure of progress here.

## Acceptance criteria

- [ ] `docs/CPP-SUBSET.md` exists: standards, permitted headers, forbidden headers
      with their replacement.
- [ ] The decision on exceptions and RTTI is taken and justified.
- [ ] The subset checker runs as a pre-build step and fails on a forbidden header
      introduced deliberately.
- [ ] `ultramodern` compiles for the Win95 target, or its remaining errors are
      counted and assigned to a named ticket.
- [ ] `librecomp` compiles for the Win95 target, excluding the RSP vector emulation
      explicitly referred to E03-S01.
- [ ] Every dependency change is a patch under `patches/`, referenced from
      `patches/manifest.json`.
- [ ] The modern targets still compile with the same patches applied — a patch that
      breaks upstream breaks the oracle.

## Risks

The temptation will be to modify the dependency worktrees directly in order to move
fast. `Prepare-DKR-Runtime` recreates them, and the work disappears without warning.
The patch pipeline is not an administrative formality: it is the only place where
changes survive.

## References

- `docs/ARCHITECTURE.md` — protected boundaries and patch pipeline
- `patches/manifest.json` — 13 `n64-modern-runtime` patches already in place, among
  them `0001-use-msvc-compatible-warning-options.patch`: the precedent exists
- E00-S01 — per-file count of C++20 constructs
