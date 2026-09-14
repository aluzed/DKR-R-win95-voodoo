# What the Windows 95 compatibility layer changes

Delivered by
[E01-S03](stories/E01-build/E01-S03-crt-startup-api-compatibility.md).
Implementation: `platform/win95/`.

This document exists for a precise reason: **a workaround whose difference is not
written down is a bug waiting to happen.** Each function below replaces an API
absent from Windows 95, and each loses something. What is lost is written down,
including when the loss is nil.

## Why a layer is necessary

Windows 95 resolves **every** import at load time. A missing symbol stops the
process from starting, with a message that names the DLL and the symbol — and
nothing else. The project's code calls none of the functions below: it is
`libstdc++` and `winpthreads` that import them. Their mere presence in the import
table is enough to kill the program.

The inventory is [E00-S01](research/win95-blockers.md)'s, and the choice of the
`posix` threading model [ADR 0001](adr/0001-toolchain.md)'s.

## The six functions, and what they cost

### `IsDebuggerPresent` — nothing lost

Always returns `FALSE`. There is no debugger attached on the target machine: the
answer is constant *and true*. `libstdc++` uses it to decide on a `DebugBreak` at
an assertion; it will take the other branch, which is the right one here.

### `SetProcessAffinityMask` — nothing lost

Accepts and does nothing. The target is single-processor (ADR 0002): there exists
only one possible placement, and accepting it is the correct behaviour, not an
approximation.

### `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler` — an accepted degradation

Return `NULL` and `0` — that is, "I could not register".

Windows 95 has only `SetUnhandledExceptionFilter`, which is a **single point** and
not a chain of handlers. `libgcc` uses it optionally and tests the return value:
returning `NULL` is an answer it knows how to handle.

**What is lost**: nothing for `libgcc`, but any future use of vectored handlers by
the project would fail silently. The opposite lie — returning a non-null token —
would be worse: the subsequent deregistration would bear on nothing.

The project installs its own filter through `SetUnhandledExceptionFilter`, in
`platform/win95/startup.c`.

### `GetTickCount64` — a real limit, at 49.7 days

`GetTickCount` returns to zero after 49.7 days. The layer accumulates the
wraparounds to return a counter that does not.

**What is lost**: the function must be called **at least once per 49.7-day
period**. Otherwise the wraparound goes unnoticed, and time steps back by 49 days.
That is not an implementation defect but an impossibility: two readings 49 days
and 1 millisecond apart are indistinguishable from each other.

A game loop meets the condition amply. A program that slept longer between two
readings would not.

The wraparound is covered by a test that **simulates** it — waiting seven weeks is
not a protocol. The logic is isolated as a pure function in
`platform/win95/tick64.c`, precisely so that it can be driven:

```sh
platform/win95/tests/run-tests.sh tick64
ctest --test-dir build/win95 -R DKRWin95Tick64
```

The test also fixes the limit above, so that it stays a documented choice and not
a surprise.

### The five critical-section functions — reimplemented, not worked around

`TryEnterCriticalSection` is absent from Windows 95. The layer therefore supplies
**all five** functions — `Initialize`, `Enter`, `TryEnter`, `Leave`, `Delete` —
which gives it ownership of `CRITICAL_SECTION`'s 24 bytes: since the whole binary
goes through it, their meaning belongs to it alone.

The atomic exchange goes through `lock cmpxchg`, a 486 instruction, where Windows
95 does not export `InterlockedCompareExchange`. The processor can do what the
system does not offer.

**What is lost:**

- **No spinning before blocking.** Microsoft's implementation spins for a while
  before sleeping; this one waits on the semaphore with a 1 ms timeout. On a very
  contended and very short section, that costs context switches the original
  avoids. On a single processor, spinning hardly makes sense anyway.
- **A timed wait rather than an infinite one.** If a wake-up is lost between the
  test and the wait, the loop catches it on the next turn instead of sleeping
  forever. It is a choice of robustness over precision: the wake-up may be delayed
  by 1 ms.
- **No diagnostics.** `DebugInfo` stays null; tools inspecting the structure would
  see nothing.

**What is not lost**: reentrancy, per-thread ownership, and mutual exclusion.
Verified on the machine — two threads, 4,000 increments in contention, final
counter exactly 4,000.

> **A lesson that cost dearly.** The first version merely returned `FALSE` from
> `TryEnterCriticalSection` — a *lawful* answer under the contract, since every
> caller must allow for failure, and one checked as safe since `try_lock` appears
> nowhere in the runtime.
>
> It froze the whole machine. `winpthreads` loops on that function to take its
> locks, and the busy wait starves Windows 95's scheduler to the point of stopping
> the taskbar's clock.
>
> **A lawful stub is not a harmless stub.**

### `CreateSemaphoreW` — added by E02-S01, and of another nature

The six above were **missing** from the export table, and their absence is loud:
the program does not start, and Windows names the symbol. `CreateSemaphoreW` is
exported. It simply does nothing — three instructions that return zero and set
`ERROR_CALL_NOT_IMPLEMENTED`, at the same address as `CreateEventW`.

It is a trap of another class: the link succeeds, the load succeeds, the import
check was satisfied, and only the execution differs.

It matters because `moodycamel::LightweightSemaphore` calls it, and that semaphore
is the blocking primitive of the whole of `ultramodern`'s scheduler. With a null
handle, the wait no longer blocks and the signal loops forever.

The layer supplies it, routed onto `CreateSemaphoreA`, converting the name if
there is one. **What is lost**: nothing — `CreateSemaphoreA` is real code, and the
name conversion can only fail on a name the system's code page does not represent.

Full detail and consequences: [WIN95-THREADING.md](WIN95-THREADING.md) and
[research/win95-blockers.md](research/win95-blockers.md).

## What did not need to be written

The ticket anticipated two delicate workarounds. Measurement made them
unnecessary, and that is a result worth recording:

| API | Status | Verification |
|---|---|---|
| `SignalObjectAndWait` | **not required** | absent from the imports of `libwinpthread`, `libstdc++` and `libgcc` |
| `InitializeCriticalSectionAndSpinCount` | **not required** | the same |
| `GetThreadId` | **not required** in the `posix` model | required by the `win32` model, ruled out by ADR 0001 |

The risk the ticket announced — decomposing `SignalObjectAndWait` and losing its
atomicity, hence opening a race window — **does not materialise**. Nothing asks
for it.

> **A follow-up, from E02-S01.** The conclusion "Vista's condition variables are
> not to be reproduced" was confirmed for a stronger reason than the choice of the
> `posix` model: `ultramodern` uses **no** condition variable at all. Its
> conditional wait is a counting semaphore.
>
> The table above stays true, but a column it did not have must be added to it:
> "exported" does not mean "implemented". See
> [WIN95-THREADING.md](WIN95-THREADING.md).

Vista's condition variables are not reimplemented either: that is precisely what
the choice of the `posix` model allowed us to avoid, their reproduction on Windows
95 events being an exercise in which wake-ups get lost.

## Unicode: the `...A` APIs, without exception

Under Windows 9x, the `...W` family **is exported but does nothing**. The
disassembly of `KERNEL32.DLL` shows it: every `...W` entry fits in three
instructions — `xor eax,eax`, an index, a jump to a common tail that sets
`ERROR_CALL_NOT_IMPLEMENTED`. `LoadLibraryExW` and `MoveFileExW` share the same
address, because neither has any code.

Consequence: **the whole layer uses the `...A` APIs**, including for file paths,
and the startup log opens its file with `CreateFileA`.

Good news on the runtime's side: `librecomp` works in **`std::u8string`**, that is
in UTF-8 over bytes — 236 uses of narrow strings against 25 of wide ones, and the
latter are `u8string`, not `wchar_t`. There is therefore no wide conversion to
remove.

**One exception remains**, to be dealt with by E01-S02: `mod_manifest.cpp:52`
calls `_wfopen_s`, which is **not exported** by the machine's `MSVCRT.DLL` (only
`_wfopen` is, and it rests on `CreateFileW`, hence on a stub). It is in the mod
system, which E00-S01 already names as the first candidate for a fork.

There remains the question of code pages: UTF-8 is not the system's code page 850.
A path containing accented characters will not be transmitted correctly. That is
not addressed here, and it is not urgent — the game opens only paths it builds
itself.

## CRT: static linking, without exception

`-static -static-libgcc -static-libstdc++`. A decision of
[ADR 0001](adr/0001-toolchain.md), for three reasons, two of them measured:

1. **`libgcc_s_dw2-1.dll` does not exist under Windows 95.** A binary linked
   dynamically against libgcc does not load — observed on a witness compiled
   without `-static` by inadvertence.
2. **`MSVCRT.DLL` is not original.** The test machine's is dated 3 November 1997,
   while the rest of the system carries 24 August 1996. It arrives with an update,
   and **a first-generation Windows 95 does not have it at all**. Depending on it
   would amount to making the game depend on a version of Internet Explorer.
3. Static linking removes any question of redistribution.

**Consequence for distribution
([E09-S05](stories/E09-qa/E09-S05-packaging-distribution.md))**: the package has
**no redistributable to embed** for the CRT. What remains is to check for the
presence of `glide2x.dll`, supplied by the card's driver and not by the package.

The cost is the binary's size: 501 KB for a witness that would be 51 with Open
Watcom. Immaterial against [ADR 0003](adr/0003-memory-budget.md)'s 14 MiB of
headroom.

## Startup

`dkr_win95_startup()` must be called on the first line of `main`. It does three
things nobody else will:

**A log in a file.** There is no usable console on the target machine: a
full-screen game that dies before its first frame leaves nothing to read. The log
is written **next to the executable** — launched from the Start menu, a program
inherits a current directory that has nothing to do with where the user will go
looking for the file — and it is **flushed after every line**, so that the last
line survives the crash that interrupted it. That is precisely the one that
counts.

**A structured exception filter.** Without it, an invalid instruction produces a
dialog box that names nothing usable. With it, the code and the address go into
the log. The `EXCEPTION_ILLEGAL_INSTRUCTION` case carries an explicit note: on
this target, it is the symptom of an instruction later than the Pentium II having
escaped E01-S01's check.

**A version check.** Win32s on Windows 3.1 and any version earlier than 4.0 are
refused with a comprehensible message, rather than by a crash on a missing API.
Windows NT is accepted: the binary runs there too, which makes development less
painful.

Recorded on the test machine:

```
=== startup log ===
Platform witness
D:\DKR-BOOT.LOG
exception filter installed
system: platform 1, version 4.0 build 1111
 C
startup complete
```

Platform 1 is `VER_PLATFORM_WIN32_WINDOWS`, and version 4.0 build 1111 with the
"C" marker is Windows 95 OSR2's signature.

## Checking

```sh
./Build-Win95.sh
ctest --test-dir build/win95                        # tick64 + threads (E02-S01)
scripts/Push-To-Win95-VM.sh build/win95/bin/PLATFORM.EXE
```

The `PLATFORM.EXE` witness exercises the whole layer. On the test machine:

```
IsDebuggerPresent      : false
SetProcessAffinityMask : accepted
GetTickCount64         : 123 ms elapsed        (for a 120 ms Sleep)
TryEnterCriticalSection: free lock taken
two threads, 4000 turns: counter = 4000 / 4000
```

## What this build does not carry, and how it says so

Three subsystems the mainstream targets build are absent here. Each is a build
flag in `runtime-recomp/src/game/netplay_presence.hpp`, and all three **default
to 1**: a target that says nothing keeps the feature, so one added upstream
cannot lose it by omission. Windows 95 sets all three to 0 in
`cmake/win95-target.cmake`.

### `DKR_RUNTIME_HAS_NETPLAY` — a loader decision, not a scope one

Online play is not switched off here because a Pentium II would be slow at it.
It is off because **the binary would not start.** The netplay sources call
`getaddrinfo`, `freeaddrinfo`, `inet_pton` and `inet_ntop`, and not one of the
four is in the export table of this machine's `WSOCK32.DLL` — read off the
machine, see [`tools/win95/exports/PROVENANCE.md`](../tools/win95/exports/PROVENANCE.md).
By the rule at the top of this document, a missing import stops the process
loading whether or not the function is ever called.

Three further walls stand behind that one: `WSAStartup(MAKEWORD(2, 2))` asks for
Winsock 2.2 where the machine carries 1.1 and has no `WS2_32` at all; two of the
transports are built on libdatachannel, which is WebRTC with DTLS and ICE under
it; and the netplay sources hold 189 `std::scoped_lock` and 3 `std::mutex`,
which is precisely what E02-S02's seam exists to remove.

Forty-three call sites are guarded across six files.

### `DKR_RUNTIME_HAS_LEGACY_MODS` — a scope decision

Thirty-five sources and five thousand lines of character and asset modding, for
a launcher path this target never takes, against the eight mebibytes
[ADR 0003](adr/0003-memory-budget.md) has to fit the whole game into. The mod
system has been out of scope since E00-S01; the build now says so rather than
carrying it unused.

### `DKR_RUNTIME_HAS_PAYLOAD_V80` — one ROM revision

This target recompiles US v1.0 only, so `game_payload.cpp`'s v1.1 arm has no
entry point to link against.

### The hooks all three still owe

`dkr.us.v77.recomp-policy.json` is a property of the **ROM revision, not of the
target**: the generated code calls every hook the policy names, on every build.
A target without netplay is therefore still called at
`dkr_netplay_gameplay_level_begin`, and one without RT64 at
`dkr_custom_tracks_table_load_begin`.

`runtime_absent_hooks.cpp` supplies all twenty-one, compiled only when the
matching guard is off so it can never collide with a real definition. It is not
a stub of either subsystem: each hook is what that hook does when the thing it
hooks into is absent, and the two that return a value return the caller's own
contract for "nothing happened" — `drive_authored_tick` returns 0 so the game
runs its own loop, `presentation_random_range` returns 0 so the game keeps its
own random.

## A guard that reads includes cannot see a use

Worth its own heading, because it has now cost two debugging sessions.

`tools/win95/check-cpp-subset.py` scans `platform/win95` and
`extern/n64-modern-runtime/ultramodern`. **It does not scan `librecomp`**, and it
matches text: an `#include` line, or a use written out as `std::thread`. Neither
catches a translation unit that reaches `std::thread` through a header it did not
name — and `librecomp` never includes `<thread>` directly.

The symptom is not a link error. It is a binary that loads, runs to
`support-configure`, and dies:

    terminate called after throwing an instance of 'std::system_error'
      what():  Resource temporarily unavailable

That is `pthread_create` failing behind libstdc++'s `std::thread`. The check that
does catch it is a sweep for the primitives themselves, over both components:

```sh
grep -rnE '\bstd::(thread|mutex|lock_guard|scoped_lock|unique_lock|condition_variable|this_thread)\b' \
  extern/n64-modern-runtime/librecomp/{src,include} \
  extern/n64-modern-runtime/ultramodern/{src,include} \
  | grep -v 'threading.hpp\|sync.hpp'
```

It must report nothing. And on the binary itself, which is the check no source
sweep can be talked out of:

```sh
i686-w64-mingw32-nm -C build/win95/bin/DKRR.EXE | grep _M_start_thread
```

Run both after any dependency-patch rebase. Three separate hunks of the
threading seam were lost to one three-way merge on 14 September 2026, and only
these two checks found them.

## Auditing an import: `ld --cref`, not the justification

14 September 2026. `exceptions.json` tolerated `MoveFileExW` on this reasoning:

> "It is asked for by libstdc++'s fs_ops.o, that is, by `std::filesystem::rename`
> — which this code does not call on this target ... The import is therefore
> present and unreachable."

That was true when it was written. It was not true afterwards, and **nothing in
the build could tell the difference**, because a justification is prose and the
checker only reads the symbol.

The audit that settles provenance is one linker flag:

```sh
i686-w64-mingw32-g++ ... -Wl,--cref -o probe.exe
```

Its cross-reference table names every file that references a symbol. For this one
it answered:

    _imp__MoveFileExW@12    ...  save_manager.cpp.obj
                                 runtime_magic_codes.cpp.obj

Not libstdc++. **The project's own code**, under `#if defined(_WIN32)`, restored
by the 1.0.5 beta 8 merge — reinstating the exact defect the justification
recorded as fixed by E02-S06.

What it cost while it stood: `MoveFileExW` is one of the exported-but-empty wide
stubs, so it returned 0 and set `ERROR_CALL_NOT_IMPLEMENTED`, and neither call
site has a fallback. **Every atomic save write and every one-shot magic-code
queue replacement failed on the target**, reporting an error the player never
sees.

Both are excluded from the Win32 branch now, the symbol is gone from all
thirty-eight binaries, and the exception has been **removed** rather than
reworded — a tolerance whose reason has disappeared goes on passing an import
nobody re-examines, which is precisely how this got in.

Verified by putting the direct call back for one build: `check_imports.py` fails
it and names the object,

    STUB  KERNEL32.DLL:MoveFileExW  <- save_manager.cpp.obj

so the attribution was there the whole time. The prose was what silenced it.

**Read the other entries against this.** Each one asserts a provenance that no
check verifies, and each was true when written.
