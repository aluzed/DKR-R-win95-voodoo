# E02-S01 — Win95 threading and synchronisation layer

| | |
|---|---|
| **Epic** | E02 — Windows 95 system substrate |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | ~~L~~ **M** |
| **Depends on** | E01-S02, E01-S03 |
| **Blocks** | E02-S02, E02-S03, E06-S03 |

## State as of 2026-08-12 — scope reduced by measurement

[E00-S01](../E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md) put a
number on this ticket, and it is smaller than expected:

- **`ultramodern` has only 6 files affected**, 12 `std::thread` and 5 `std::mutex`
  in all. It gets patched; it does not get rewritten.
- The gap comes down to **six functions**: `TryEnterCriticalSection`, `GetThreadId`
  and Vista's four condition variables.
- **Choose the `posix` (winpthreads) threading model** rather than `win32`: the same
  number of blockers, but superficial ones — `IsDebuggerPresent` returns false,
  `SetProcessAffinityMask` does nothing, `GetTickCount64` wraps around
  `GetTickCount`, the vectored handlers fall back on
  `SetUnhandledExceptionFilter`, which exists. Reproducing Vista's condition
  variables is markedly more delicate.
- The intended delivery is a **small compatibility library** placed before
  `libkernel32.a` in the linker's resolution order, not an abstraction layer inside
  `ultramodern`.
- `std::atomic` is a given: verified at run time on the emulated Pentium II.

Detail and figures:
[`docs/research/win95-blockers.md`](../../research/win95-blockers.md).

## Context

`ultramodern` reproduces the N64's scheduler: several game threads at strict
priorities, plus the runtime's infrastructure threads. It relies on the modern C++
standard library — `std::thread`, `std::mutex`, `std::condition_variable`, and
presumably some C++20 primitives.

Under Windows 95, two levels pose a problem:

- the standard library's implementation of those primitives may call APIs absent
  from the system (E00-S01 listed them);
- Windows 95 knows neither condition variables, which date from Vista, nor
  `TryEnterCriticalSection`, nor `SignalObjectAndWait`.

A point often forgotten works in our favour: the target machine is
**single-processor**. There is no real parallelism, only interleaving. Races remain
possible — preemption is real — but a whole swathe of complexity tied to weak memory
models disappears.

## Objective

To deliver `platform/win95/threading.{h,cpp}`: a minimal thread and synchronisation
interface, built solely on APIs present in Windows 95, on which `ultramodern` will
be rested in E02-S02.

## Scope

**In:** threads, mutual exclusion, conditional waiting, events, thread-local
variables, and their validation.

**Out:** the N64 scheduler itself (E02-S02) and the `ultramodern` patches.

## Work

1. Define the interface from what `ultramodern` really needs — recorded in its code,
   not deduced from a generic model. Over-sizing here costs directly in porting work.
2. Implement threads on `CreateThread`: creation, termination, joining, priority.
   Map the N64 priorities to Win32's thread priority classes, and write the mapping
   table: the N64 has more useful levels than Win32 exposes, so the mapping is lossy
   and must be chosen explicitly.
3. Implement mutual exclusion on `CRITICAL_SECTION`. Check reentrancy's behaviour
   under Windows 95: Win32's critical sections are recursive, which `std::mutex` is
   not — code that relied on a non-recursive `std::mutex` deadlocking to reveal a
   defect will no longer reveal it.
4. Implement conditional waiting. Without a native condition variable, the scheme is
   a manual-reset event per waiter, plus a protected counter. Write explicitly which
   wake-up guarantee is offered — one waiter, all of them, order respected or not —
   and match it to what `ultramodern` assumes.
5. Implement thread-local variables on `TlsAlloc`. Windows 95 severely limits the
   number of slots: count those really used and allocate only one, indexing a
   structure, if the count is tight.
6. Write the tests: creation and joining, exclusion under contention, conditional
   wake-up with no lost wake-up, respect for the priority order. Those tests must run
   on the modern host **and** on the target; a synchronisation test that runs only on
   the host proves nothing about the target.
7. Run the tests under stress: a long loop under load, in the test machine, to flush
   out lost wake-ups. A ten-second run does not find that kind of defect.

## Acceptance criteria

- [x] `platform/win95/threading.{h,cpp}` imports no API absent from Windows 95 —
      verified by E01-S04's guard rail, now doubled by a check on **empty** exports
      (see below).
- [x] The interface covers the needs recorded in `ultramodern`, without surplus.
      *Reopened then closed on 2026-08-13*: the survey redone on the patched tree
      revealed that the condition variable and `unique_lock` were missing; both are
      delivered.
- [x] The N64 → Win32 priority mapping is written and justified. The result is that
      **it does not exist**: the N64 order is kept by `ultramodern`'s software
      queue, not by the host system. What is written, and tested, is the
      `ThreadPriority` → `THREAD_PRIORITY_*` table.
- [x] The conditional wait's wake-up semantics are documented and match what
      `ultramodern` assumes. *Reopened then closed on 2026-08-13*: `dkr_condvar` is
      delivered, and the absence of lost wake-ups is established **by construction**
      — the registration precedes the release of the caller's lock — because it is
      not reliably established by the test, which is written in black and white.
- [x] The reentrancy difference between `CRITICAL_SECTION` and `std::mutex` is
      documented, and its effect assessed: `dkr_mutex` restores non-reentrancy and
      **reports** it instead of deadlocking.
- [x] The tests pass on the modern host and under emulated Windows 95 — the same
      source, 48 checks on the target, 0 failures.
- [x] A stress run of at least ten minutes passes with no lost wake-up and no
      deadlock — **600 s on the target, 8,437 rounds**, that is 8.4 million semaphore
      wake-ups and 337 million locks taken under contention. The machine stayed usable
      throughout.

## Result

Delivered: `platform/win95/threading.{h,cpp}`,
`platform/win95/tests/test_threading.cpp` (host **and** `THREADS.EXE`),
`tools/win95/find_stubs.py`, and the empty-export check in
`tools/win95/check_imports.py`.

Documentation: [`docs/WIN95-THREADING.md`](../../WIN95-THREADING.md).

### What measurement changed in the ticket

Three of the ticket's hypotheses fell, and a blocker it had not seen appeared:

1. ~~**No condition variable in `ultramodern`.**~~ **Corrected on 2026-08-13: that
   was false.** The survey covered the dependency's worktree as it stood — only patch
   0014 applied — and the other thirteen could not be, since
   `scripts/apply-dependency-patches.sh` checked the tree's cleanliness inside its
   loop. **The repository's patch 0013** introduces into `mesgqueue.cpp` two
   `std::condition_variable`s, with `notify_one`, `notify_all`,
   `wait(lock, predicate)` and `wait_for` — the complete surface. Upstream
   `ultramodern` does indeed use none. **The delicate part therefore remains to be
   done**, and the corresponding acceptance criterion is reopened.

2. **The N64 → Win32 priority mapping has no reason to exist.**
   `thread_queue_insert` keeps the order in software and a single game thread runs at
   a time: the host system never arbitrates between two game threads.

3. **`CreateSemaphoreW` is a stub.** Exported by Windows 95, it returns 0 and sets
   `ERROR_CALL_NOT_IMPLEMENTED`. `moodycamel::LightweightSemaphore` calls it, and it
   is the blocking primitive of *all* of the scheduler. On the waiting side the
   blocking disappears — the game threads then all run together; on the signalling
   side `ReleaseSemaphore(NULL)` loops forever and **freezes the machine**.
   `compat.c`'s bridge now supplies it.

4. **`std::thread::join()` does not work under Windows 95.** `pthread_join`
   validates its handle through `GetHandleInformation`, another stub; the failure
   travels up as a `std::system_error`. `dkr_thread_join` goes through
   `WaitForSingleObject`.

A fifth point appeared on rereading, and it comes from this layer and not from
Windows 95: the first version merged the thread descriptor and its start packet into
a single allocation, which makes `dkr_thread_release` a **use after free** — the
created thread reads `fn` before having run, and on a single processor it has
generally not run at all. No trial detached a thread, so nothing caught it. Trial
no. 3 now does, and `ultramodern/src/timer.cpp` takes that path for real.

Points 3 and 4 were invisible to the import guard rail, which checked only the
symbol's *presence*. It now also checks that it is not empty: `find_stubs.py`
recognises the pattern in the disassembly and records **179 stubs in KERNEL32, 176 in
ADVAPI32, 162 in USER32, 62 in GDI32**.

### What is left for E02-S02

`ultramodern` is not yet rested on this layer — that is the next ticket, and its
scope is the one this ticket excluded. The 12 `std::thread`, 3 `std::mutex` and 3
`thread_local` recorded there await it, as does the question of
`BlockingConcurrentQueue`, which the `CreateSemaphoreW` bridge makes functional
without patching it.

## Risks

Synchronisation defects are rare, non-deterministic, and manifest as random freezes
mid-game. They are particularly expensive here, because the diagnosis cycle goes
through an emulated machine without modern tooling. Hence the insistence on stress
tests rather than on code review.

## References

- `docs/ARCHITECTURE.md` — `ultramodern` supplies the scheduling
- E00-S01 — missing synchronisation APIs
- E01-S03 — the compatibility layer
