# Threads and synchronisation under Windows 95

Delivered by
[E02-S01](stories/E02-system/E02-S01-threading-and-synchronisation-layer.md).
Implementation: `platform/win95/threading.{h,cpp}`.

The same rule as for [the compatibility layer](WIN95-COMPAT.md): **a workaround
whose difference is not written down is a bug waiting to happen.** Each primitive
below replaces a standard primitive, and what it loses is written down — including
when it loses nothing.

## What measurement changed in the ticket

> ### ⚠ Correction of 2026-08-13 — the survey covered an unrepresentative tree
>
> This document first asserted that `ultramodern` used **no** condition variable.
> **That is false**, and the reason for the error is worth writing down because it
> is not a slip.
>
> The survey was made on the dependency's worktree as it stood: only patch 0014
> was applied to it. The other thirteen were not, and could not be —
> `scripts/apply-dependency-patches.sh` checked the tree's cleanliness **inside**
> its loop, so that at the second patch it took the first one's effect for a local
> edit and refused. The full stack had never been able to apply in one go.
>
> But **the project's own patch 0013** introduces into `mesgqueue.cpp` a message
> queue resting on **two `std::condition_variable`s**. On the patched tree — the
> only one that counts, since it is the one we compile — the inventory is the one
> in the table below.
>
> The lesson is the one the repository already applies elsewhere: a measurement is
> only worth something once one has verified that it bears on the real state. The
> script is fixed, and the stack now applies in full from a pristine tree.

The ticket was written on three hypotheses. The survey disproved two, confirmed
one, and revealed two blockers nobody had seen.

### The condition variables are necessary — but they come from the repository

The ticket anticipated the delicate part: rebuilding Vista's condition variables
on Windows 95 events, "an exercise in which wake-ups get lost".

**Upstream** `ultramodern` uses none: its conditional wait is a counting
semaphore, `moodycamel::LightweightSemaphore`. It is the repository's patch 0013,
`use-reliable-external-message-fifo`, that adds two in `mesgqueue.cpp` — with
`notify_one`, `notify_all`, `wait(lock, predicate)` and `wait_for`, that is the
complete surface.

The delicate part is therefore indeed on the programme. It simply was not where
the ticket looked for it.

The real need, recorded rather than deduced, on the **patched** tree, that is the
one we compile:

| Primitive | Where | Status |
|---|---|---|
| `std::thread` | `events.cpp`, `threads.cpp`, `timer.cpp`, `ultramodern.hpp` | C++ bridge |
| `std::mutex` + `lock_guard` | `events.cpp`, `renderer_context.cpp`, `extensions.cpp`, **`mesgqueue.cpp`** | C++ bridge |
| `std::condition_variable` | **`mesgqueue.cpp` ×2** — added by patch 0013 | to be implemented |
| `std::unique_lock` | `mesgqueue.cpp` ×3, required by `wait` | to be implemented |
| `LightweightSemaphore` | `UltraThreadContext::running`, `initialized`, and every `BlockingConcurrentQueue` | `CreateSemaphoreW` bridge |
| `thread_local` | `threads.cpp` ×3 | **nothing to do** — measured working on the target |
| `this_thread::sleep_for` / `sleep_until` | `timer.cpp` | **nothing to do** — the `_WIN32` branch calls `Sleep` |

Two rows of this table are worth reading twice. `thread_local` **works under
Windows 95** — the PE's TLS directory is duly handled there, contrary to what is
often said; two threads write and read back their own value without treading on
each other, verified on the machine by
`tools/win95/witnesses/tls_probe.cpp`. And `sleep_for` is never reached, because
`ultramodern` already has a Windows branch calling `Sleep` directly.

The layer therefore delivers a **semaphore**, not a condition variable. That is
what is asked for, and the risk the ticket announced does not materialise.

### The N64 → Win32 priority mapping does not exist, and that is correct

The ticket asked for a mapping table, warning that it would be "lossy", the N64
having more useful levels than Win32 exposes.

**The N64 priorities never go through the host system.** `ultramodern` keeps the
order itself, in a software queue: `thread_queue_insert` (`threadqueue.cpp`)
inserts each `OSThread` in decreasing `OSPri` order, and **a single game thread
runs at a time** — each sleeps on its own `running` semaphore until the scheduler
wakes it. Windows never arbitrates between two game threads, because there are
never two ready at the same time.

What does exist is another mapping, unrelated to `OSPri`: the five levels of
`ultramodern::ThreadPriority`, which serve the infrastructure threads. It fits
inside Win32's seven classes without collapsing any of them:

| `ThreadPriority` | Win32 constant | Value |
|---|---|---:|
| `Low` | `THREAD_PRIORITY_BELOW_NORMAL` | −1 |
| `Normal` | `THREAD_PRIORITY_NORMAL` | 0 |
| `High` | `THREAD_PRIORITY_ABOVE_NORMAL` | 1 |
| `VeryHigh` | `THREAD_PRIORITY_HIGHEST` | 2 |
| `Critical` | `THREAD_PRIORITY_TIME_CRITICAL` | 15 |

Five levels inside seven classes: the mapping is **injective**, and strictly
increasing — two properties the test verifies, on the host as on the target,
because `dkr_thread_priority_to_win32` is a pure function.

**What is really lost** is elsewhere, and deserves to be named: `OSPri`'s 256
levels are flattened by `ultramodern` **on every platform**, modern Windows
included. It is not a loss of the Windows 95 port; it is a choice of the upstream
runtime, and the Windows 95 target adds nothing to it.

A detail worth knowing: in `ultramodern`, `set_native_thread_priority` computes
the constant and then **does not apply it** — the call to `SetThreadPriority` is
commented out upstream. The layer does apply it.

### `CreateSemaphoreW` is a stub — the real blocker

This is this ticket's discovery, and it was invisible to the existing guard rails.
It is treated in detail in
[`docs/research/win95-blockers.md`](research/win95-blockers.md); in summary:
`moodycamel::LightweightSemaphore` calls `CreateSemaphoreW`, which Windows 95
exports **without implementing it**. Both sides of the semaphore break, and
differently — the wait no longer blocks, the signal loops forever.

`compat.c`'s bridge now supplies `CreateSemaphoreW`, routed onto
`CreateSemaphoreA`. This ticket's layer calls nothing but `...A`.

## The primitives, and what they cost

### Threads — `_beginthreadex`, and not `CreateThread`

The ticket said `CreateThread`. The layer uses `_beginthreadex`, which calls it
internally, for a precise reason: `CreateThread` **does not prepare the CRT's
per-thread state** — `errno`, `strtok`'s buffer, `rand`'s state. A thread created
that way which touches the CRT reads and writes another thread's state, and leaks
it on exit. `ultramodern`'s threads do touch it, if only through `debug_printf`
and `std::string`.

The expected objection would be the dependency on `MSVCRT.DLL`. It is beside the
point: E01-S03's witness's import table already requires it for `__getmainargs`,
`_initterm` and a score of others. `_beginthreadex` **is exported** by the test
machine's `MSVCRT.DLL` — checked against its export table.

**What is lost**: nothing. The thread's name is not set —
`SetThreadDescription` is from Windows 10 and serves only a debugger, which the
target does not have.

### Detaching — two allocations, because there are two owners

`dkr_thread_start` allocates **two** blocks, and it is the only way
`dkr_thread_release` can be safe:

| Block | Owner | Freed by |
|---|---|---|
| `dkr_thread` — the descriptor | the creator | `dkr_thread_join` or `dkr_thread_release` |
| the start packet — `fn`, `arg` | the created thread | the thread itself, as soon as it has copied it |

Merging them into a single structure — the obvious version, and the first one
written here — is a **use after free**. The created thread reads `fn` and `arg` as
its very first act; `dkr_thread_release` frees the descriptor without waiting. On
a single processor, the creator keeps its quantum after `_beginthreadex`: at the
moment of the `release`, the created thread has generally **not yet executed a
single instruction**. It then jumps into an `fn` the CRT's heap has already
recycled.

This is not a textbook case: `ultramodern/src/timer.cpp` detaches its timer thread
immediately after creating it.

The defect was covered by no trial — the suite never detached anything. Trial no. 3
now does, in its most unfavourable form: eight threads started and released at
once. On the POSIX vehicle under AddressSanitizer, the merged version fails
immediately with `heap-use-after-free` in the trampoline; the two-block version
passes.

### Joining — `WaitForSingleObject`, and why that is not a detail

`std::thread::join()` **does not work under Windows 95** with the `posix`
threading model. `winpthreads`'s `pthread_join` validates its handle through
`GetHandleInformation`, which is **a stub** under Windows 95: it fails,
`pthread_join` takes its error path, and libstdc++ turns that failure into a
`std::system_error`.

Observed in the disassembly of `libwinpthread_la-thread.o`, then fixed in place by
the guard rail: the import check now refuses `GetHandleInformation` everywhere
except in `WITNESS.EXE`, the witness whose role is precisely to exercise the
standard model.

`dkr_thread_join` goes through `WaitForSingleObject`, which works. **That is one
of this layer's reasons for being**, and it was not in the ticket.

### Mutual exclusion — non-recursive, and verified

Built on `CRITICAL_SECTION`, that is on the five functions
[E01-S03](WIN95-COMPAT.md) implements itself.

Win32's critical sections are **recursive**; `std::mutex` is not. The difference
is not theoretical: code that counted on a reentrant `std::mutex` deadlocking to
reveal a defect would no longer reveal it, and the defect would go into production
on the one platform where it does not show.

`dkr_mutex` restores the property: the owner is recorded, and reentrancy is
**detected and reported** — log then stop — instead of passing. The diagnostic is
better than `std::mutex`'s, which merely freezes: on an emulated machine without
modern tooling, a message naming the fault is worth far more than a hang.

Reading the owner outside the lock is safe, and for a precise reason: the only
value that triggers the alarm is our own identifier, which only we can have
written there; the write of an aligned 32-bit word cannot be torn on x86. The test
can therefore neither miss a reentrancy nor invent one.

`dkr_mutex_try_lock` on its own lock returns `0` **without reporting anything**:
the caller of a `try` has already allowed for failure, like the caller of
`std::mutex::try_lock`.

An edge case, for completeness: if a thread terminates while holding a
`dkr_mutex`, the owner stays recorded, and a later thread to which Windows
reassigned the same identifier would be accused of reentrancy. The lock was
definitively abandoned in that case anyway — the program is already broken, and a
wrongful accusation stays more legible than an eternal block.

**What is lost**: the defects inherited from E01-S03's `CRITICAL_SECTION` — no
spinning before blocking, a 1 ms wait rather than an infinite one, no diagnostics
in `DebugInfo`. They are described in [WIN95-COMPAT.md](WIN95-COMPAT.md).

### Counting semaphore — the wake-up semantics, written down

This is *the* scheduler's blocking primitive. Its semantics, set against what
`ultramodern` assumes:

| Property | Guaranteed | What `ultramodern` expects of it |
|---|---|---|
| `signal(n)` wakes exactly *n* waiters | **yes** | yes — one token per resumption |
| A `signal` earlier than the `wait` is kept | **yes** | **indispensable** — see below |
| FIFO wake-up order | **not guaranteed** | not applicable — see below |
| Spurious wake-up | **never** | yes |

**The signal that precedes the wait is not lost.** It is the property the startup
of the game threads depends on: `osCreateThread` creates the thread then waits on
`initialized`, and the created thread signals `initialized` before waiting on
`running` — but nothing orders the creator's `signal` of `running` and the created
thread's `wait`. A primitive with no memory would lose that wake-up and the thread
would sleep forever. The semaphore counts, so it loses nothing.

**The wake-up order is not guaranteed, and that is not a problem.** Windows 95
does not promise FIFO on a semaphore. `ultramodern` does not need it: each of its
`running` semaphores has **only one possible waiter**, the thread owning the
context. The question therefore never arises.

### Condition variable — the part the ticket dreaded

Windows 95 has none: its own date from Vista. This one is built on the semaphore
and a waiter count protected by a lock.

**Why no wake-up is lost.** The dangerous window is this one: the waiter releases
the caller's lock, then waits; a signal emitted *between the two* must still reach
it. It does reach it, for two complementary reasons:

1. The waiting primitive is a **counting semaphore**. `notify` deposits a token,
   and the token waits for the waiter.
2. The waiter count is incremented **before** the caller's lock is released. A
   signaller can only signal after modifying the state the waiter tests, and it
   can only modify it while holding that same lock — which it can only take after
   our release, hence after our registration. There exists no interleaving in
   which it misses us.

**This property holds by the argument, not by the test**, and that is the most
important thing to know about this part. The reverse order was tried: the suite
passes all the same, the 20,000 relays included. The reason is instructive — the
signaller's path to `notify` (take the lock, modify the state, release it) is
longer than the waiter's path to its registration, so that it almost always loses
the race. Almost. That is exactly the shape of defect the ticket describes: rare,
non-deterministic, and manifesting as a random freeze at the player's end.

**What is not guaranteed**, and is no more guaranteed elsewhere: a wake-up may be
**stolen**. If two threads are waiting and a third signals, nothing says which one
resumes. `std::condition_variable` does not say either, and that is why every
correct caller wraps its wait in a loop over a predicate. Both of the repository's
call sites do.

`wait_for` with a predicate keeps a **global deadline**, and not a per-round one:
restarting it on every wake-up is that function's classic defect — under repeated
wake-ups the wait would never end.

### Manual-reset event

What the semaphore cannot express: waking **all** the waiters at once, and staying
open for those who arrive later — without the signaller having to know their
number. It is the right shape for a "once and for all" signal: end of
initialisation, shutdown request.

`ultramodern` does not use one today; it is supplied because the semaphore cannot
replace it without counting the waiters, and because E02-S02 will have to handle
shutdown.

### Thread-local variables — a single system slot

Windows 95 offers only **64 TLS slots** for the whole process, and `libstdc++` as
well as `winpthreads` already consume some.

The layer therefore takes **only one**, which points at an array of pointers: the
number of per-thread variables becomes a matter of a program constant
(`DKR_TLS_SLOTS`, 8) and not of a system resource. `ultramodern` uses three.

The index dispenser is protected by `lock cmpxchg` and not by a lock:
`dkr_tls_reserve` may be called before `dkr_threading_init`, hence before any
critical section is ready.

**What is lost**: `dkr_threading_shutdown` frees only the calling thread's block.
There exists no way under Windows 95 to go and free another thread's; calling that
function while other threads are running therefore leaks their block — 32 bytes
each. It is written down rather than fixed: the only possible fix would be a global
registry of the blocks, whose lock would be taken on every TLS access. The blocks
of threads created by the layer are freed by those threads at the end of their
lives.

## Checking

The suite is **a single source**, `platform/win95/tests/test_threading.cpp`,
compiled twice. Two separate files would end up diverging, and it is on the target
that the differences matter.

```sh
platform/win95/tests/run-tests.sh threading           # on the host
ctest --test-dir build/win95 -R DKRWin95Threading     # the same, through CTest
DKR_STRESS_SECONDS=600 platform/win95/tests/run-tests.sh threading

./Build-Win95.sh
scripts/Push-To-Win95-VM.sh build/win95/bin/THREADS.EXE
# in the guest:  d:\threads.exe          then  d:\threads.exe --stress 600
# the full report lands in D:\THREADS.LOG
```

On the host, the vehicle is POSIX — `pthread`, `sem_t`. **It is not a supported
platform**, only enough to bring the "edit, run, observe" cycle down from a round
trip to the emulated machine to a second. Passing there proves nothing about the
target; that is why the same binary also runs on the machine.

**The timeout is not a precaution, it is the detection mechanism.** A lost wake-up
does not produce a wrong result: it produces a wait that never ends. Verified by
injecting the loss of one wake-up in a thousand, which makes the timeout expire
instead of failing cleanly. Without `timeout`, the suite would hang.

The suite was put to the test by mutation — a test that cannot fail proves
nothing:

| Mutation injected | What the suite does |
|---|---|
| the lock no longer locks | `FAIL` — 39,807 increments out of 40,000 |
| the TLS block is shared by every thread | `FAIL` — 11 crossed reads, the main thread's value lost |
| one wake-up in a thousand is lost | **timeout expired** — the expected deadlock |
| descriptor and start packet merged | `heap-use-after-free` under ASan, in the trampoline |

The last is not an invented mutation: it is the version that was written first, and
which the detachment trial did not yet exist to catch.

## Results

On the test machine — Windows 95 OSR2, emulated Pentium II 400 MHz:

```text
48 checks, 0 failure(s)
result: OK
```

Including the check that `CreateSemaphoreW` returns a usable handle, which would
fail on a Windows 95 without `compat.c`'s bridge — that is, exactly what it is
asked to watch for.

### The C++ bridge and the condition variable

```text
C++ bridge: 22 checks, 0 failure(s)
```

The twenty-two reproduce real lines of `ultramodern`: the four-argument variadic
construction of `threads.cpp:273`, the immediate detach of `timer.cpp:145`, both
forms of `lock_guard`, and the four uses of the condition variable added by patch
0013 — among them a strict relay of **20,000 rounds**, where the consumer must
necessarily fall asleep and where nothing else will come to wake it.

### Endurance: ten minutes on the target

```text
Endurance: 600 seconds
  ...
  598 s, 8420 rounds
  ok    8437 rounds with no lost wake-up and no deadlock
result: OK
```

Each round chains the two patterns that can lose a wake-up — the scheduler's round
trip and lock contention. Over 8,437 rounds, that comes to:

| | On the target, in ten minutes |
|---|---:|
| Semaphore wake-ups (wait + signal) | **8,437,000** |
| Locks taken under contention | **337,480,000** |

No lost wake-up, no deadlock, and the machine stayed usable throughout — that last
point is not decorative: it is exactly what the first version of
`TryEnterCriticalSection` failed to hold.

The loop stops **at the first anomaly** and not at the end of the allotted time; it
therefore went the distance because it found nothing.
