/* E02-S01 - threads and synchronisation for Windows 95.
 *
 * A minimal interface built only on APIs that Windows 95 exports *and*
 * implements, on which `ultramodern` will be rested in E02-S02.
 *
 * The scope is not deduced from a generic model: it is measured from
 * `ultramodern`'s code, because every surplus is paid for in porting work. What
 * is actually used there, and nothing else:
 *
 *   std::thread                  x12   creation, joining, detaching
 *   std::mutex + lock_guard      x5    mutual exclusion, non-recursive
 *   LightweightSemaphore         xN    *the* scheduler's blocking primitive
 *   thread_local                 x3    threads.cpp: two flags, one pointer
 *   this_thread::sleep_for/until       delays
 *   set_native_thread_priority         5 levels, purely advisory
 *
 * Two absences are worth noting, because they shrink the ticket:
 *
 *   - **No condition variable at all.** `ultramodern` declares not one. Its
 *     conditional wait is a *counting semaphore*, and nothing else. The delicate
 *     reimplementation the ticket feared - per-waiter events plus a guarded
 *     counter, where wake-ups get lost - is therefore moot. We supply the
 *     semaphore, which is what is asked for.
 *
 *   - **No N64 -> Win32 priority mapping.** The N64's priority order is held by
 *     `ultramodern`'s software queue (`thread_queue_insert` inserts in `OSPri`
 *     order), and only one game thread runs at a time. The host system never
 *     arbitrates between two game threads. See `docs/WIN95-THREADING.md`.
 *
 * The semantics lost by each primitive is written down in
 * `docs/WIN95-THREADING.md`. A workaround whose difference is not written down
 * is a bug waiting to happen.
 */
#ifndef DKR_WIN95_THREADING_H
#define DKR_WIN95_THREADING_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- Bringing the layer up ------------------------------------------------ *
 *
 * To be called once, from the main thread, before any other call into this layer
 * - in practice right after `dkr_win95_startup`. It reserves the single TLS slot
 * (see below) and takes note of the calling thread.
 */
int  dkr_threading_init(void);
void dkr_threading_shutdown(void);

/* --- Threads -------------------------------------------------------------- */

typedef struct dkr_thread dkr_thread;
typedef void (*dkr_thread_fn)(void *arg);

/* Starts a thread. `stack_bytes` at 0 leaves the system's default size. Returns
   NULL on failure. The returned thread must be either joined or released. */
dkr_thread *dkr_thread_start(dkr_thread_fn fn, void *arg, unsigned long stack_bytes);

/* Waits for the thread to end, then frees the handle. Returns 1 on success. */
int  dkr_thread_join(dkr_thread *t);

/* Frees the handle without waiting. The thread carries on. The equivalent of
   `std::thread::detach`.

   May be called immediately after `dkr_thread_start`, including before the
   thread has run its first instruction - which, on a single processor, is the
   ordinary case and not the rare one. The created thread shares no allocation
   with this handle, precisely for that reason. */
void dkr_thread_release(dkr_thread *t);

/* The current thread's identifier. Never 0 for a live thread. */
unsigned long dkr_thread_id(void);

/* --- Priorities ----------------------------------------------------------- *
 *
 * These five levels reproduce `ultramodern::ThreadPriority` exactly, order
 * included, so that E02-S02 only has a trivial conversion to write.
 *
 * They do **not** carry the N64 priorities: see the top of this file.
 */
typedef enum {
    DKR_THREAD_PRIORITY_LOW = 0,
    DKR_THREAD_PRIORITY_NORMAL,
    DKR_THREAD_PRIORITY_HIGH,
    DKR_THREAD_PRIORITY_VERY_HIGH,
    DKR_THREAD_PRIORITY_CRITICAL
} dkr_thread_priority;

/* The mapping, isolated as a pure function so that it can be tested on the host
   without Windows. Returns the matching `THREAD_PRIORITY_*` constant, or
   DKR_THREAD_PRIORITY_INVALID for an out-of-range input. */
#define DKR_THREAD_PRIORITY_INVALID (-32768)
int  dkr_thread_priority_to_win32(int priority);

/* Applies the priority to the current thread. No measurable effect on the order
   of game threads; useful for infrastructure threads. */
void dkr_thread_set_priority(dkr_thread_priority priority);

/* --- Delays --------------------------------------------------------------- */

void dkr_sleep_ms(unsigned long ms);
void dkr_yield(void);

/* --- Mutual exclusion ----------------------------------------------------- *
 *
 * Built on CRITICAL_SECTION - the one from `platform/win95/compat.c`, since
 * E01-S03 supplies all five functions and therefore owns the structure.
 *
 * **Non-recursive, and checked.** Win32's critical sections are recursive,
 * `std::mutex` is not. Code that relied on a reentrant `std::mutex` deadlocking
 * to reveal a defect would no longer reveal it, and the defect would ship. This
 * layer restores the property: reentrancy is detected and reported immediately,
 * instead of deadlocking silently. The diagnosis is better than `std::mutex`'s,
 * at a cost of two instructions.
 *
 * The reserved size is checked against `sizeof(CRITICAL_SECTION)` by a
 * `static_assert` in `threading.cpp`: the header does not have to include
 * windows.h.
 */
typedef struct {
    void          *reserved[16];  /* CRITICAL_SECTION, or its host equivalent */
    unsigned long  owner;         /* the owner's identifier, 0 if free */
    int            initialised;
} dkr_mutex;

int  dkr_mutex_init(dkr_mutex *m);
void dkr_mutex_destroy(dkr_mutex *m);
void dkr_mutex_lock(dkr_mutex *m);
void dkr_mutex_unlock(dkr_mutex *m);

/* Returns 1 if the lock was taken, 0 otherwise. Does not report reentrancy: a
   caller of `try` has already allowed for failure, so we simply return 0. */
int  dkr_mutex_try_lock(dkr_mutex *m);

/* --- Counting semaphore --------------------------------------------------- *
 *
 * This is *the* blocking primitive of `ultramodern`'s scheduler: every game
 * thread sleeps on `running.wait()` and is woken by `running.signal()`.
 *
 * Wake-up semantics, to be compared with what `ultramodern` assumes:
 *
 *   - `signal(n)` wakes **exactly n** waiters, never more.
 *   - A `signal` that precedes the `wait` is **not lost**: it is counted. That
 *     is the property game-thread startup depends on, where the creating
 *     thread's `signal` can get ahead of the created thread's `wait`.
 *   - **Wake-up order is not guaranteed.** Windows 95 does not promise FIFO on a
 *     semaphore. `ultramodern` does not need it: each of its semaphores has
 *     **only one possible waiter** - the thread that owns the context - so the
 *     question does not arise.
 *   - No spurious wake-ups: `wait` only returns 1 on a consumed token.
 */
typedef struct {
    void *handle;
} dkr_sem;

int  dkr_sem_init(dkr_sem *s, long initial_count);
void dkr_sem_destroy(dkr_sem *s);

/* Blocks until a token is obtained. Returns 1 on success, 0 if the semaphore is
   invalid - never a silent return without a token. */
int  dkr_sem_wait(dkr_sem *s);

/* Returns 1 if a token was taken before the deadline, 0 otherwise. */
int  dkr_sem_wait_timeout(dkr_sem *s, unsigned long ms);

/* Returns 1 if a token was available, 0 otherwise. Never blocks. */
int  dkr_sem_try_wait(dkr_sem *s);

/* Deposits `count` tokens. Returns 1 on success. */
int  dkr_sem_signal(dkr_sem *s, long count);

/* --- Manual-reset event --------------------------------------------------- *
 *
 * What the semaphore cannot express: waking **every** waiter at once, and
 * staying open for those who arrive later. It is the right shape for a
 * "once and for all" signal - end of initialisation, shutdown request - where a
 * semaphore would force us to know the number of waiters.
 */
typedef struct {
    void *handle;
} dkr_event;

int  dkr_event_init(dkr_event *e, int initially_set);
void dkr_event_destroy(dkr_event *e);
void dkr_event_set(dkr_event *e);
void dkr_event_reset(dkr_event *e);
int  dkr_event_wait(dkr_event *e);
int  dkr_event_wait_timeout(dkr_event *e, unsigned long ms);

/* --- Condition variable --------------------------------------------------- *
 *
 * Windows 95 has none: its own date from Vista. This one is built on the
 * semaphore above and a waiter counter guarded by a lock.
 *
 * It is the piece ticket E02-S01 feared - "an exercise where wake-ups get lost".
 * It turned out to be necessary not because of upstream `ultramodern`, which
 * uses none, but because of the repository's patch 0013, which introduces two of
 * them in `mesgqueue.cpp`.
 *
 * ## Why no wake-up is lost
 *
 * A condition variable's dangerous window is this one: the waiter releases the
 * caller's lock, then goes to wait. A signal emitted *between the two* must
 * still reach it.
 *
 * Here it does reach it, because the waiting primitive is a **counting
 * semaphore**: `notify` deposits a token, and the token waits for the waiter.
 *
 * And above all: the waiter counter is incremented **before** the caller's lock
 * is released. That order is not a precaution, it is the proof. A signaller can
 * only signal after modifying the state the waiter tests, and it can only modify
 * that state while holding the same lock. So it cannot take the lock until we
 * have released it - and by that moment we are already registered. There is no
 * interleaving in which it misses us.
 *
 * **This property holds by the argument, not by the test.** The reverse order
 * was tried: the suite passes all the same, 20,000 relays included. The reason
 * is instructive - the signaller's path to `notify` (take the lock, modify the
 * state, release it) is longer than the waiter's path to registration, so it
 * almost always loses the race. Almost. That is exactly the shape of defect the
 * ticket describes: rare, non-deterministic, and showing up as a random freeze
 * at the player's machine. So we handle it by construction and not by testing.
 *
 * ## What is not guaranteed, and is not guaranteed elsewhere either
 *
 * A wake-up can be **stolen**: if two threads wait and a third signals, nothing
 * says which of the two leaves. `std::condition_variable` says no more, and that
 * is why every correct caller wraps its wait in a loop over a predicate. Both
 * call sites in the repository do - `wait(lock, predicate)` and
 * `while (!complete && !exited)`.
 *
 * A `notify` emitted while nobody waits is lost, as it should be.
 */
typedef struct {
    dkr_mutex  guard;        /* guards `waiters` */
    dkr_sem    sem;          /* the waiting primitive proper */
    long       waiters;
    int        initialised;
} dkr_condvar;

int  dkr_condvar_init(dkr_condvar *cv);
void dkr_condvar_destroy(dkr_condvar *cv);

/* Wakes at most one waiter, at most all of them. No effect if there are none. */
void dkr_condvar_notify_one(dkr_condvar *cv);
void dkr_condvar_notify_all(dkr_condvar *cv);

/* Releases `external`, waits, then retakes it before returning - including on a
   timeout, like `std::condition_variable`.
   `wait` returns 1; `wait_timeout` returns 1 if woken, 0 if it timed out. */
int  dkr_condvar_wait(dkr_condvar *cv, dkr_mutex *external);
int  dkr_condvar_wait_timeout(dkr_condvar *cv, dkr_mutex *external,
                              unsigned long ms);

/* --- Thread-local variables ----------------------------------------------- *
 *
 * Windows 95 offers only 64 TLS slots for the whole process, and libstdc++ and
 * winpthreads already consume some. This layer therefore takes **only one**,
 * which designates an array of pointers: the number of per-thread variables
 * becomes a matter of a constant and not of a system resource.
 *
 * `ultramodern` uses three (`is_entrypoint_thread`, `is_game_thread`,
 * `thread_self`). The margin is deliberately short: this is not a general store,
 * and every new slot has to justify itself.
 */
#define DKR_TLS_SLOTS 8

/* Reserves a slot. Returns its index, or -1 if none is left. To be called once
   per variable, typically at startup. */
int   dkr_tls_reserve(void);

void *dkr_tls_get(int slot);
void  dkr_tls_set(int slot, void *value);

/* Frees the current thread's block. Called automatically at the end of threads
   started by `dkr_thread_start`; to be called by hand for a thread this layer
   did not create and which is ending. */
void  dkr_tls_release_current(void);

/* --- Diagnostics ---------------------------------------------------------- *
 *
 * Called when the layer observes a fault it cannot recover from - today,
 * reentrancy on a `dkr_mutex`. By default the message goes into the startup log
 * and then the process stops: on the target machine, carrying on after a
 * synchronisation fault only produces a freeze further along, with no visible
 * connection to its cause.
 *
 * The tests intercept it to check that the detection works.
 */
typedef void (*dkr_threading_fatal_fn)(const char *message);
void dkr_threading_set_fatal_handler(dkr_threading_fatal_fn handler);

/* Reports a fault through the same channel. Public because E02-S02's C++ bridge
   (`threading.hpp`) needs it: a destroyed `thread` that is still joinable is the
   same class of fault as reentrancy, and must report and test the same way.

   Only returns if a handler intercepted it - the default behaviour is to stop
   the process. Callers must therefore stay correct in both cases. */
void dkr_threading_fatal(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_THREADING_H */
