/* E02-S01 - threads and synchronisation for Windows 95.
 *
 * The contract, what is lost relative to the standard primitives, and the survey
 * of `ultramodern`'s real needs are in `threading.h` and in
 * `docs/WIN95-THREADING.md`. This file contains only the implementation.
 *
 * Two implementations live here:
 *
 *   - **Windows** - the target. Only APIs that Windows 95 exports *and*
 *     implements; the distinction is not rhetorical, see the note on
 *     `CreateSemaphoreW` below.
 *
 *   - **POSIX** - a test vehicle, and nothing else. It exists so that the
 *     `tests/test_threading.cpp` suite also runs on the modern host, where the
 *     "change, run, observe" cycle costs a second instead of a round trip to the
 *     emulated machine. It is not a supported platform, and a test that only
 *     passed there has proved nothing about the target: that is why the same
 *     suite is also built as THREADS.EXE and run under Windows 95.
 */
#include "threading.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================== *
 * State shared by both implementations
 * ========================================================================== */

static dkr_threading_fatal_fn dkr_fatal_handler = 0;

/* Defined below, once per implementation. Public: see threading.h. */
void dkr_threading_fatal(const char *message);

void dkr_threading_set_fatal_handler(dkr_threading_fatal_fn handler)
{
    dkr_fatal_handler = handler;
}

/* The priority mapping. A pure function with no Windows dependency: it can be
   tested on the host, and the constants are written out so that the table reads
   without consulting windows.h.

   Windows 95 exposes seven thread priority classes. `ultramodern` distinguishes
   five. The mapping is therefore *injective* - no level collapses onto another -
   and the question of loss does not arise in that direction.

   It arises in the other: the N64's priorities run from 0 to 255 and never come
   through here. See docs/WIN95-THREADING.md. */
#define DKR_W32_BELOW_NORMAL  (-1)
#define DKR_W32_NORMAL          0
#define DKR_W32_ABOVE_NORMAL    1
#define DKR_W32_HIGHEST         2
#define DKR_W32_TIME_CRITICAL  15

int dkr_thread_priority_to_win32(int priority)
{
    switch (priority) {
    case DKR_THREAD_PRIORITY_LOW:       return DKR_W32_BELOW_NORMAL;
    case DKR_THREAD_PRIORITY_NORMAL:    return DKR_W32_NORMAL;
    case DKR_THREAD_PRIORITY_HIGH:      return DKR_W32_ABOVE_NORMAL;
    case DKR_THREAD_PRIORITY_VERY_HIGH: return DKR_W32_HIGHEST;
    case DKR_THREAD_PRIORITY_CRITICAL:  return DKR_W32_TIME_CRITICAL;
    default:                            return DKR_THREAD_PRIORITY_INVALID;
    }
}

/* --- Thread-local slots, the portable part -------------------------------- *
 *
 * A single system slot is consumed; it designates this block. The count of
 * logical slots is a constant of the program, not a system resource - which
 * matters under Windows 95, where the process has only 64 for everyone,
 * libstdc++ and winpthreads included.
 */
typedef struct {
    void *slots[DKR_TLS_SLOTS];
} dkr_tls_block;

/* Index dispenser. Guarded by an atomic exchange rather than a lock:
   `dkr_tls_reserve` may be called before `dkr_threading_init`, hence before any
   critical section is ready. */
static volatile long dkr_tls_next = 0;

static dkr_tls_block *dkr_tls_block_current(int create);

int dkr_tls_reserve(void)
{
    long slot;
    do {
        slot = dkr_tls_next;
        if (slot >= DKR_TLS_SLOTS) {
            return -1;
        }
    } while (!__sync_bool_compare_and_swap(&dkr_tls_next, slot, slot + 1));
    return (int)slot;
}

void *dkr_tls_get(int slot)
{
    dkr_tls_block *b;
    if (slot < 0 || slot >= DKR_TLS_SLOTS) {
        return 0;
    }
    /* No creation on read: a thread that has never stored anything reads zero,
       which is the expected initial value of a thread-local variable. */
    b = dkr_tls_block_current(0);
    return b ? b->slots[slot] : 0;
}

void dkr_tls_set(int slot, void *value)
{
    dkr_tls_block *b;
    if (slot < 0 || slot >= DKR_TLS_SLOTS) {
        return;
    }
    b = dkr_tls_block_current(1);
    if (b) {
        b->slots[slot] = value;
    }
}


/* --- Condition variable, shared by both implementations ------------------- *
 *
 * It rests only on `dkr_mutex` and `dkr_sem`, which both backends supply. There
 * is therefore **one** implementation and not two to keep in step - which
 * matters for the piece of the ticket that was announced as the most delicate.
 * The correctness argument is in `threading.h`.
 */
int dkr_condvar_init(dkr_condvar *cv)
{
    if (!cv) {
        return 0;
    }
    cv->waiters     = 0;
    cv->initialised = 0;
    if (!dkr_mutex_init(&cv->guard)) {
        return 0;
    }
    if (!dkr_sem_init(&cv->sem, 0)) {
        dkr_mutex_destroy(&cv->guard);
        return 0;
    }
    cv->initialised = 1;
    return 1;
}

void dkr_condvar_destroy(dkr_condvar *cv)
{
    if (!cv || !cv->initialised) {
        return;
    }
    dkr_sem_destroy(&cv->sem);
    dkr_mutex_destroy(&cv->guard);
    cv->initialised = 0;
}

void dkr_condvar_notify_one(dkr_condvar *cv)
{
    if (!cv || !cv->initialised) {
        return;
    }
    dkr_mutex_lock(&cv->guard);
    if (cv->waiters > 0) {
        cv->waiters--;
        dkr_sem_signal(&cv->sem, 1);
    }
    dkr_mutex_unlock(&cv->guard);
}

void dkr_condvar_notify_all(dkr_condvar *cv)
{
    long n;
    if (!cv || !cv->initialised) {
        return;
    }
    dkr_mutex_lock(&cv->guard);
    n = cv->waiters;
    if (n > 0) {
        cv->waiters = 0;
        dkr_sem_signal(&cv->sem, n);
    }
    dkr_mutex_unlock(&cv->guard);
}

/* Shared core of both waits. A negative `ms` - represented by `timed == 0` -
   means "no deadline". */
static int dkr_condvar_wait_impl(dkr_condvar *cv, dkr_mutex *external,
                                 int timed, unsigned long ms)
{
    int woken;

    if (!cv || !cv->initialised || !external) {
        return 0;
    }

    /* Registration happens **before** the caller's lock is released. That is
       what guarantees that a signaller, which can only act after obtaining that
       same lock or ours, always sees the waiter. */
    dkr_mutex_lock(&cv->guard);
    cv->waiters++;
    dkr_mutex_unlock(&cv->guard);

    dkr_mutex_unlock(external);

    woken = timed ? dkr_sem_wait_timeout(&cv->sem, ms)
                  : dkr_sem_wait(&cv->sem);

    if (!woken) {
        /* The deadline has passed. A signal may have been emitted between the
           timeout and this moment: the token would then be deposited and our
           counter already decremented. Leaving it would strand a wake-up for
           nobody, and the next waiter would leave for no reason. So we take it
           back. */
        dkr_mutex_lock(&cv->guard);
        if (dkr_sem_try_wait(&cv->sem)) {
            woken = 1;
        } else {
            cv->waiters--;
        }
        dkr_mutex_unlock(&cv->guard);
    }

    /* The caller's lock is retaken in every case, timeout included: that is
       `std::condition_variable`'s contract, and the caller writes its code
       assuming it. */
    dkr_mutex_lock(external);
    return woken;
}

int dkr_condvar_wait(dkr_condvar *cv, dkr_mutex *external)
{
    return dkr_condvar_wait_impl(cv, external, 0, 0);
}

int dkr_condvar_wait_timeout(dkr_condvar *cv, dkr_mutex *external,
                             unsigned long ms)
{
    return dkr_condvar_wait_impl(cv, external, 1, ms);
}


#if defined(_WIN32)

/* ========================================================================== *
 * Windows - the target
 * ========================================================================== */

#include <windows.h>
#include <process.h>

#include "startup.h"

/* `dkr_mutex`'s blob must hold a CRITICAL_SECTION. The check lives here and not
   in the header, so that the header stays free of windows.h. */
static_assert(sizeof(CRITICAL_SECTION) <= sizeof(((dkr_mutex *)0)->reserved),
              "dkr_mutex::reserved too small for a CRITICAL_SECTION");

void dkr_threading_fatal(const char *message)
{
    if (dkr_fatal_handler) {
        dkr_fatal_handler(message);
        return;
    }
    /* The startup log is flushed after every line: this one will survive the
       stop that follows. It is the only usable channel - the target machine has
       no console, and a full-screen game would have no use for one. */
    dkr_win95_log(message);
    /* Same reason as for the exception filter: this exit path is abrupt, and the
       settings that outlive the process must be undone before we leave. */
    dkr_win95_run_cleanups();
    ExitProcess(3);
}

/* --- Thread-local slots --------------------------------------------------- */

static DWORD dkr_tls_index     = 0xFFFFFFFFu;
static DWORD dkr_main_thread   = 0;

static dkr_tls_block *dkr_tls_block_current(int create)
{
    dkr_tls_block *b;

    if (dkr_tls_index == 0xFFFFFFFFu) {
        return 0;                       /* layer not initialised */
    }
    b = (dkr_tls_block *)TlsGetValue(dkr_tls_index);
    if (!b && create) {
        b = (dkr_tls_block *)calloc(1, sizeof(*b));
        if (b && !TlsSetValue(dkr_tls_index, b)) {
            /* Without this the block would be lost: nobody would keep its
               address, and the next call would allocate another. */
            free(b);
            b = 0;
        }
    }
    return b;
}

void dkr_tls_release_current(void)
{
    dkr_tls_block *b;
    if (dkr_tls_index == 0xFFFFFFFFu) {
        return;
    }
    b = (dkr_tls_block *)TlsGetValue(dkr_tls_index);
    if (b) {
        free(b);
        TlsSetValue(dkr_tls_index, 0);
    }
}

/* --- Bringing the layer up ------------------------------------------------ */

int dkr_threading_init(void)
{
    if (dkr_tls_index != 0xFFFFFFFFu) {
        return 1;                       /* already in service */
    }
    dkr_tls_index = TlsAlloc();
    if (dkr_tls_index == 0xFFFFFFFFu) {
        return 0;
    }
    dkr_main_thread = GetCurrentThreadId();
    return 1;
}

void dkr_threading_shutdown(void)
{
    if (dkr_tls_index == 0xFFFFFFFFu) {
        return;
    }
    /* Only the calling thread's block is freed: other threads' blocks are freed
       by themselves at the end of their lives, and there is no way under
       Windows 95 to go and free a third-party thread's. Calling this function
       while other threads are still running therefore leaks their blocks - 32
       bytes each. This is written down rather than fixed: the only possible fix
       would be to keep a global registry of blocks, whose lock would be taken on
       every TLS access. */
    dkr_tls_release_current();
    TlsFree(dkr_tls_index);
    dkr_tls_index = 0xFFFFFFFFu;
    /* The dispenser restarts from zero: without this, an init / shutdown / init
       cycle would never recover its slots, and the second cycle would exhaust
       the stock for no visible reason. */
    dkr_tls_next = 0;
}

/* --- Threads -------------------------------------------------------------- */

/* Two allocations, not one, because they do not have the same owner.
 *
 * `dkr_thread` belongs to the creator, which may release it whenever it likes -
 * that is the whole point of `dkr_thread_release`. The start packet belongs to
 * the created thread, which copies it and frees it itself.
 *
 * Merging them into a single structure would be a **use after free**, and not a
 * theoretical one: on a single processor, `_beginthreadex` returns to the
 * creator, which keeps its quantum, so the created thread has generally not run
 * a single instruction by the time `dkr_thread_release` frees. The thread then
 * jumps into an `fn` recycled by the CRT's heap.
 *
 * The case is not hypothetical: `ultramodern/src/timer.cpp` detaches its timer
 * thread immediately after creating it. */
typedef struct {
    dkr_thread_fn fn;
    void         *arg;
} dkr_thread_start_packet;

struct dkr_thread {
    HANDLE        handle;
    unsigned      id;
};

/* `_beginthreadex` rather than `CreateThread` - a deliberate departure from the
   letter of the ticket.
 *
 * `CreateThread` does not prepare the CRT's per-thread state: `errno`, `strtok`'s
 * buffer, `rand`'s state. A thread created that way which touches the CRT reads
 * and writes another thread's state, and leaks it on exit. `ultramodern`'s
 * threads do touch it - if only through `debug_printf` and `std::string`.
 *
 * The usual objection would be the dependency on MSVCRT.DLL. It is moot: the
 * E01-S03 witness's import table already asks for it for `__getmainargs`,
 * `_initterm` and a score of others. `_beginthreadex` is exported by the test
 * machine's MSVCRT.DLL - checked against its export table, not against
 * documentation.
 *
 * `_beginthreadex` calls `CreateThread`. The spirit of the ticket is kept; its
 * letter is corrected. */
static unsigned __stdcall dkr_thread_trampoline(void *param)
{
    /* Copy then free immediately: from here on the thread touches nothing the
       creator could free out from under it. */
    dkr_thread_start_packet packet = *(dkr_thread_start_packet *)param;
    free(param);

    packet.fn(packet.arg);

    /* The TLS block belongs to the thread: it dies with it. Without this, every
       game thread created and destroyed would leave one behind, and
       `ultramodern` creates one per `osCreateThread`. */
    dkr_tls_release_current();
    return 0;
}

dkr_thread *dkr_thread_start(dkr_thread_fn fn, void *arg, unsigned long stack_bytes)
{
    dkr_thread              *t;
    dkr_thread_start_packet *packet;
    uintptr_t                h;

    if (!fn) {
        return 0;
    }
    t = (dkr_thread *)calloc(1, sizeof(*t));
    if (!t) {
        return 0;
    }
    packet = (dkr_thread_start_packet *)calloc(1, sizeof(*packet));
    if (!packet) {
        free(t);
        return 0;
    }
    packet->fn  = fn;
    packet->arg = arg;

    h = _beginthreadex(NULL, (unsigned)stack_bytes, dkr_thread_trampoline,
                       packet, 0, &t->id);
    if (h == 0) {
        free(packet);           /* the thread does not exist: nobody frees it */
        free(t);
        return 0;
    }
    t->handle = (HANDLE)h;
    return t;
}

int dkr_thread_join(dkr_thread *t)
{
    DWORD r;
    if (!t) {
        return 0;
    }
    r = WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    free(t);
    return r == WAIT_OBJECT_0;
}

void dkr_thread_release(dkr_thread *t)
{
    if (!t) {
        return;
    }
    CloseHandle(t->handle);
    free(t);
}

unsigned long dkr_thread_id(void)
{
    return (unsigned long)GetCurrentThreadId();
}

void dkr_thread_set_priority(dkr_thread_priority priority)
{
    int mapped = dkr_thread_priority_to_win32((int)priority);
    if (mapped == DKR_THREAD_PRIORITY_INVALID) {
        return;
    }
    SetThreadPriority(GetCurrentThread(), mapped);
}

void dkr_sleep_ms(unsigned long ms)
{
    Sleep(ms);
}

void dkr_yield(void)
{
    /* `Sleep(0)` yields to threads of at least equal priority. Windows 95 does
       not have `SwitchToThread`, which would also yield to lower-priority
       threads. */
    Sleep(0);
}

/* --- Mutual exclusion ----------------------------------------------------- */

int dkr_mutex_init(dkr_mutex *m)
{
    if (!m) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    InitializeCriticalSection((LPCRITICAL_SECTION)m->reserved);
    m->initialised = 1;
    return 1;
}

void dkr_mutex_destroy(dkr_mutex *m)
{
    if (!m || !m->initialised) {
        return;
    }
    DeleteCriticalSection((LPCRITICAL_SECTION)m->reserved);
    m->initialised = 0;
}

/* Reading `owner` outside the lock is safe, and for a precise reason: the only
   value that trips the alarm is our own identifier, which only we can have
   written there. Another thread only writes its own or zero, and the write of an
   aligned 32-bit word cannot tear on x86. The test can therefore neither miss a
   reentrancy nor invent one. */
void dkr_mutex_lock(dkr_mutex *m)
{
    unsigned long me = (unsigned long)GetCurrentThreadId();

    if (m->owner == me) {
        dkr_threading_fatal("dkr_mutex: reentrancy - a std::mutex would have deadlocked here");
        return;
    }
    EnterCriticalSection((LPCRITICAL_SECTION)m->reserved);
    m->owner = me;
}

int dkr_mutex_try_lock(dkr_mutex *m)
{
    unsigned long me = (unsigned long)GetCurrentThreadId();

    if (m->owner == me) {
        return 0;       /* already held by us: a std::mutex `try_lock` fails */
    }
    if (!TryEnterCriticalSection((LPCRITICAL_SECTION)m->reserved)) {
        return 0;
    }
    m->owner = me;
    return 1;
}

void dkr_mutex_unlock(dkr_mutex *m)
{
    /* The order matters: clear the owner before releasing the section. The other
       way round, a thread could take the section and set its identifier, which
       we would then clear. */
    m->owner = 0;
    LeaveCriticalSection((LPCRITICAL_SECTION)m->reserved);
}

/* --- Semaphore ------------------------------------------------------------ *
 *
 * `CreateSemaphoreA`, and never `CreateSemaphoreW`. Under Windows 95 the latter
 * is **exported but empty**: three instructions that return zero and set
 * `ERROR_CALL_NOT_IMPLEMENTED`. It shares its address with `CreateEventW`, a
 * sign that neither has any code. Verified by disassembling the test machine's
 * KERNEL32.DLL - see docs/research/win95-blockers.md.
 *
 * The trap is serious because it is silent: the link succeeds, the load
 * succeeds, E01-S04's import check is satisfied since the symbol *is* exported.
 * Only execution differs.
 */
int dkr_sem_init(dkr_sem *s, long initial_count)
{
    if (!s) {
        return 0;
    }
    s->handle = (void *)CreateSemaphoreA(NULL, initial_count, 0x7FFFFFFF, NULL);
    return s->handle != 0;
}

void dkr_sem_destroy(dkr_sem *s)
{
    if (s && s->handle) {
        CloseHandle((HANDLE)s->handle);
        s->handle = 0;
    }
}

int dkr_sem_wait(dkr_sem *s)
{
    if (!s || !s->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)s->handle, INFINITE) == WAIT_OBJECT_0;
}

int dkr_sem_wait_timeout(dkr_sem *s, unsigned long ms)
{
    if (!s || !s->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)s->handle, ms) == WAIT_OBJECT_0;
}

int dkr_sem_try_wait(dkr_sem *s)
{
    if (!s || !s->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)s->handle, 0) == WAIT_OBJECT_0;
}

int dkr_sem_signal(dkr_sem *s, long count)
{
    if (!s || !s->handle || count <= 0) {
        return 0;
    }
    return ReleaseSemaphore((HANDLE)s->handle, count, NULL) != 0;
}

/* --- Manual-reset event --------------------------------------------------- */

int dkr_event_init(dkr_event *e, int initially_set)
{
    if (!e) {
        return 0;
    }
    /* TRUE: manual reset. That is what distinguishes the event from the
       semaphore - it wakes every waiter and stays open. */
    e->handle = (void *)CreateEventA(NULL, TRUE, initially_set ? TRUE : FALSE, NULL);
    return e->handle != 0;
}

void dkr_event_destroy(dkr_event *e)
{
    if (e && e->handle) {
        CloseHandle((HANDLE)e->handle);
        e->handle = 0;
    }
}

void dkr_event_set(dkr_event *e)
{
    if (e && e->handle) {
        SetEvent((HANDLE)e->handle);
    }
}

void dkr_event_reset(dkr_event *e)
{
    if (e && e->handle) {
        ResetEvent((HANDLE)e->handle);
    }
}

int dkr_event_wait(dkr_event *e)
{
    if (!e || !e->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)e->handle, INFINITE) == WAIT_OBJECT_0;
}

int dkr_event_wait_timeout(dkr_event *e, unsigned long ms)
{
    if (!e || !e->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)e->handle, ms) == WAIT_OBJECT_0;
}

#else

/* ========================================================================== *
 * POSIX - a host test vehicle, not a supported platform
 * ========================================================================== */

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static_assert(sizeof(pthread_mutex_t) <= sizeof(((dkr_mutex *)0)->reserved),
              "dkr_mutex::reserved too small for a pthread_mutex_t");

void dkr_threading_fatal(const char *message)
{
    if (dkr_fatal_handler) {
        dkr_fatal_handler(message);
        return;
    }
    fprintf(stderr, "erreur fatale : %s\n", message);
    exit(3);
}

static pthread_key_t dkr_tls_key;
static int           dkr_tls_ready = 0;

static void dkr_tls_destructor(void *p)
{
    free(p);
}

static dkr_tls_block *dkr_tls_block_current(int create)
{
    dkr_tls_block *b;

    if (!dkr_tls_ready) {
        return 0;
    }
    b = (dkr_tls_block *)pthread_getspecific(dkr_tls_key);
    if (!b && create) {
        b = (dkr_tls_block *)calloc(1, sizeof(*b));
        if (b) {
            pthread_setspecific(dkr_tls_key, b);
        }
    }
    return b;
}

void dkr_tls_release_current(void)
{
    dkr_tls_block *b;
    if (!dkr_tls_ready) {
        return;
    }
    b = (dkr_tls_block *)pthread_getspecific(dkr_tls_key);
    if (b) {
        free(b);
        pthread_setspecific(dkr_tls_key, 0);
    }
}

int dkr_threading_init(void)
{
    if (dkr_tls_ready) {
        return 1;
    }
    if (pthread_key_create(&dkr_tls_key, dkr_tls_destructor) != 0) {
        return 0;
    }
    dkr_tls_ready = 1;
    return 1;
}

void dkr_threading_shutdown(void)
{
    if (!dkr_tls_ready) {
        return;
    }
    dkr_tls_release_current();
    pthread_key_delete(dkr_tls_key);
    dkr_tls_ready = 0;
    dkr_tls_next  = 0;          /* likewise: see the Windows branch */
}

/* The same split of ownership as on the target, and for the same reason: the
   creator may release its handle before the thread has started. */
typedef struct {
    dkr_thread_fn fn;
    void         *arg;
} dkr_thread_start_packet;

struct dkr_thread {
    pthread_t handle;
};

static void *dkr_thread_trampoline(void *param)
{
    dkr_thread_start_packet packet = *(dkr_thread_start_packet *)param;
    free(param);

    packet.fn(packet.arg);

    dkr_tls_release_current();
    return 0;
}

dkr_thread *dkr_thread_start(dkr_thread_fn fn, void *arg, unsigned long stack_bytes)
{
    dkr_thread              *t;
    dkr_thread_start_packet *packet;

    /* Not honoured here, honoured on the target: a stack-size regression will
       therefore only show up on the machine. That is written in threading.h. */
    (void)stack_bytes;

    if (!fn) {
        return 0;
    }
    t = (dkr_thread *)calloc(1, sizeof(*t));
    if (!t) {
        return 0;
    }
    packet = (dkr_thread_start_packet *)calloc(1, sizeof(*packet));
    if (!packet) {
        free(t);
        return 0;
    }
    packet->fn  = fn;
    packet->arg = arg;
    if (pthread_create(&t->handle, 0, dkr_thread_trampoline, packet) != 0) {
        free(packet);
        free(t);
        return 0;
    }
    return t;
}

int dkr_thread_join(dkr_thread *t)
{
    int r;
    if (!t) {
        return 0;
    }
    r = pthread_join(t->handle, 0);
    free(t);
    return r == 0;
}

void dkr_thread_release(dkr_thread *t)
{
    if (!t) {
        return;
    }
    pthread_detach(t->handle);
    free(t);
}

unsigned long dkr_thread_id(void)
{
    /* Sufficient for what the layer uses it for: comparing two threads. */
    return (unsigned long)(uintptr_t)pthread_self();
}

void dkr_thread_set_priority(dkr_thread_priority priority)
{
    /* The host is not the target: priority is not applied here, only the mapping
       table is tested - and it is tested as a pure function. */
    (void)priority;
}

void dkr_sleep_ms(unsigned long ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* resume */
    }
}

void dkr_yield(void)
{
    sched_yield();
}

int dkr_mutex_init(dkr_mutex *m)
{
    if (!m) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    if (pthread_mutex_init((pthread_mutex_t *)m->reserved, 0) != 0) {
        return 0;
    }
    m->initialised = 1;
    return 1;
}

void dkr_mutex_destroy(dkr_mutex *m)
{
    if (!m || !m->initialised) {
        return;
    }
    pthread_mutex_destroy((pthread_mutex_t *)m->reserved);
    m->initialised = 0;
}

void dkr_mutex_lock(dkr_mutex *m)
{
    unsigned long me = dkr_thread_id();

    if (m->owner == me) {
        dkr_threading_fatal("dkr_mutex: reentrancy - a std::mutex would have deadlocked here");
        return;
    }
    pthread_mutex_lock((pthread_mutex_t *)m->reserved);
    m->owner = me;
}

int dkr_mutex_try_lock(dkr_mutex *m)
{
    unsigned long me = dkr_thread_id();

    if (m->owner == me) {
        return 0;
    }
    if (pthread_mutex_trylock((pthread_mutex_t *)m->reserved) != 0) {
        return 0;
    }
    m->owner = me;
    return 1;
}

void dkr_mutex_unlock(dkr_mutex *m)
{
    m->owner = 0;
    pthread_mutex_unlock((pthread_mutex_t *)m->reserved);
}

int dkr_sem_init(dkr_sem *s, long initial_count)
{
    sem_t *sem;
    if (!s) {
        return 0;
    }
    sem = (sem_t *)calloc(1, sizeof(*sem));
    if (!sem || sem_init(sem, 0, (unsigned)initial_count) != 0) {
        free(sem);
        s->handle = 0;
        return 0;
    }
    s->handle = sem;
    return 1;
}

void dkr_sem_destroy(dkr_sem *s)
{
    if (s && s->handle) {
        sem_destroy((sem_t *)s->handle);
        free(s->handle);
        s->handle = 0;
    }
}

int dkr_sem_wait(dkr_sem *s)
{
    int r;
    if (!s || !s->handle) {
        return 0;
    }
    while ((r = sem_wait((sem_t *)s->handle)) == -1 && errno == EINTR) {
        /* reprise */
    }
    return r == 0;
}

int dkr_sem_wait_timeout(dkr_sem *s, unsigned long ms)
{
    struct timespec ts;
    int r;

    if (!s || !s->handle) {
        return 0;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec  += 1;
    }
    while ((r = sem_timedwait((sem_t *)s->handle, &ts)) == -1 && errno == EINTR) {
        /* reprise */
    }
    return r == 0;
}

int dkr_sem_try_wait(dkr_sem *s)
{
    if (!s || !s->handle) {
        return 0;
    }
    return sem_trywait((sem_t *)s->handle) == 0;
}

int dkr_sem_signal(dkr_sem *s, long count)
{
    long i;
    if (!s || !s->handle || count <= 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (sem_post((sem_t *)s->handle) != 0) {
            return 0;
        }
    }
    return 1;
}

/* The manual-reset event does not exist in POSIX: it is rebuilt from a lock, a
   condition variable and a flag. That is exactly the construction the target does
   *not* have to make, since Windows 95 offers the object outright. */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             set;
} dkr_posix_event;

int dkr_event_init(dkr_event *e, int initially_set)
{
    dkr_posix_event *ev;
    if (!e) {
        return 0;
    }
    ev = (dkr_posix_event *)calloc(1, sizeof(*ev));
    if (!ev) {
        e->handle = 0;
        return 0;
    }
    pthread_mutex_init(&ev->mutex, 0);
    pthread_cond_init(&ev->cond, 0);
    ev->set   = initially_set ? 1 : 0;
    e->handle = ev;
    return 1;
}

void dkr_event_destroy(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_cond_destroy(&ev->cond);
    pthread_mutex_destroy(&ev->mutex);
    free(ev);
    e->handle = 0;
}

void dkr_event_set(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_mutex_lock(&ev->mutex);
    ev->set = 1;
    pthread_cond_broadcast(&ev->cond);
    pthread_mutex_unlock(&ev->mutex);
}

void dkr_event_reset(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_mutex_lock(&ev->mutex);
    ev->set = 0;
    pthread_mutex_unlock(&ev->mutex);
}

int dkr_event_wait(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return 0;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_mutex_lock(&ev->mutex);
    while (!ev->set) {
        pthread_cond_wait(&ev->cond, &ev->mutex);
    }
    pthread_mutex_unlock(&ev->mutex);
    return 1;
}

int dkr_event_wait_timeout(dkr_event *e, unsigned long ms)
{
    dkr_posix_event *ev;
    struct timespec ts;
    int ok;

    if (!e || !e->handle) {
        return 0;
    }
    ev = (dkr_posix_event *)e->handle;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec  += 1;
    }
    pthread_mutex_lock(&ev->mutex);
    while (!ev->set) {
        if (pthread_cond_timedwait(&ev->cond, &ev->mutex, &ts) == ETIMEDOUT) {
            break;
        }
    }
    ok = ev->set;
    pthread_mutex_unlock(&ev->mutex);
    return ok;
}

#endif /* _WIN32 */
