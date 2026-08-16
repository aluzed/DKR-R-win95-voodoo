# E01-S03 — CRT, startup and the API compatibility layer

| | |
|---|---|
| **Epic** | E01 — 32-bit Windows 95 build chain |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | E00-S01, E01-S01 |
| **Blocks** | E01-S04, E02-S01, E06-S01 |

## State as of 2026-08-12 — delivered and run on the machine

`platform/win95/`: `compat.{h,c}`, `tick64.c`, `startup.{h,c}`, `witness.c`. The
lost semantics are documented in
[`docs/WIN95-COMPAT.md`](../../WIN95-COMPAT.md).

The witness exercising **the whole layer** runs under Windows 95:

```
IsDebuggerPresent      : false
SetProcessAffinityMask : accepted
GetTickCount64         : 123 ms elapsed        (for a 120 ms Sleep)
TryEnterCriticalSection: free lock taken
two threads, 4000 turns: counter = 4000 / 4000
```

and its startup log identifies the system:

```
system: platform 1, version 4.0 build 1111
 C
```

that is `VER_PLATFORM_WIN32_WINDOWS`, 4.0 build 1111 with the "C" marker — Windows
95 OSR2's signature.

**`GetTickCount`'s wraparound is simulated, not waited for.** The logic is isolated
as a pure function in `tick64.c`; nine checks drive it with chosen values, among
them two successive wraparounds and a monotonicity property. Run by CTest on the
host (`DKRWin95Tick64`), without an emulator.

**Two workarounds the ticket anticipated did not have to be written**, and that is
measured rather than assumed: neither `SignalObjectAndWait` nor
`InitializeCriticalSectionAndSpinCount` appears in the imports of `libwinpthread`,
`libstdc++` or `libgcc`. **The risk the ticket announced — losing
`SignalObjectAndWait`'s atomicity — does not materialise.**

**Unicode.** `librecomp` works in `std::u8string`, hence in UTF-8 bytes: 236 uses of
narrow strings against 25 of wide ones, all of them `u8string` and not `wchar_t`.
There is no wide conversion to remove. **One exception remains** —
`mod_manifest.cpp:52` calls `_wfopen_s`, which the machine's `MSVCRT.DLL` does not
export. It is in the mod system, already named as the first fork candidate.

**CRT: static linking**, ADR 0001's decision. A consequence written down for
E09-S05: the package has no redistributable to embed for the CRT.

**A design discovery:** the `_WIN32_WINNT=0x0400` guard E01-S01 set also hides the
APIs *this layer supplies* — it does not tell an API used by inadvertence from an
API replaced. `compat.h` must therefore redeclare what it implements, conditionally
on the version. Observed while compiling the witness, which failed on
`GetTickCount64`.

## Context

A binary can compile, link, and refuse to start under Windows 95 for two reasons
that appear nowhere in the build logs:

1. It imports a symbol absent from Windows 95's `kernel32.dll`. The loader then
   refuses the process **before** any execution, with a message that at best names
   the DLL. The CRT's startup code is the first suspect: it runs before `main` and
   depends on APIs that recent runtimes take for granted.
2. It depends on an absent DLL. `msvcrt.dll` is not supplied by first-generation
   Windows 95 — it arrives with OSR2, with Internet Explorer 4, or with an
   application that installs it.

E00-S01 inventoried the gaps and E00-S02 validated a witness executable. This ticket
turns that witness into a foundation usable by the whole project.

## Objective

To deliver `platform/win95/`: the startup, the CRT strategy, and a compatibility
layer that supplies the missing APIs — so that no other ticket has to worry about
them.

## Scope

**In:** startup, CRT, replacement of the missing APIs, redistributables.

**Out:** threads and synchronisation (E02-S01), the window (E06-S01).

## Work

1. Decide the CRT strategy according to E00-S02's T2 witness result: static linking
   — a larger binary, no dependency — or a redistributed `msvcrt.dll`. Write the
   decision and its consequence for the distribution package (E09-S05).
2. Write `platform/win95/compat.h` and `compat.c`: one implementation for every
   missing API recorded in E00-S01. The expected cases, each to be confirmed by
   measurement:
   - `TryEnterCriticalSection` — fall back on a classic critical section, or on a
     named mutex object if the non-blocking semantics really are required;
   - `InitializeCriticalSectionAndSpinCount` — `InitializeCriticalSection`, the spin
     parameter being moot on a single processor;
   - `GetTickCount64` — 32-bit `GetTickCount`, with overflow detection; the counter
     returns to zero after 49.7 days, which is not met in testing and is met at a
     player's;
   - `SignalObjectAndWait` — a non-atomic decomposition, with an explicit note on the
     race window thereby opened;
   - condition variables — built on events and a critical section.
3. Deal with the Unicode case. Under Windows 9x, the `...W` APIs are stubs that
   fail. Impose the `...A` APIs and code-page strings, including for file paths.
   Check what `librecomp` does with paths.
4. Write the entry point: `WinMain` or `main`, CRT initialisation, structured
   exception capture, and a startup log written to a file — there is no usable
   console for diagnosing on the target machine.
5. Add a version check at launch: cleanly refuse a system earlier than the retained
   floor, with a comprehensible message rather than a crash.
6. Document every workaround in `docs/WIN95-COMPAT.md`, with the exact semantics
   lost relative to the original API. A workaround whose difference is not written
   down is a bug waiting to happen.

## Acceptance criteria

- [ ] `platform/win95/compat.{h,c}` covers every missing API from E00-S01.
- [ ] Every workaround documents the lost semantics in `docs/WIN95-COMPAT.md`.
- [ ] `GetTickCount`'s overflow is handled and covered by a test that simulates the
      return to zero.
- [ ] The CRT strategy is settled, and its consequence for distribution written.
- [ ] The entry point writes a startup log to a file.
- [ ] Too old a system is refused with a clear message.
- [ ] A witness executable using the whole layer starts under emulated Windows 95.

## Risks

A workaround that weakens a synchronisation primitive's semantics produces rare,
non-reproducible races, discovered very late. That is notably the case of
`SignalObjectAndWait`, whose atomicity is precisely its reason for being. If
`ultramodern` depends on it, decomposition is not an acceptable solution — the
caller must be revisited, not the callee imitated.

## References

- `docs/ARCHITECTURE.md`
- E00-S01 — inventory of the missing APIs
- E00-S02 — witnesses T1 to T3
