/* E02-S01 - the threading and synchronisation layer, tested.
 *
 * **One source, two runs.** Compiled for the host it rests on
 * `threading.cpp`'s POSIX vehicle; compiled for the target it becomes
 * THREADS.EXE and runs under emulated Windows 95. It is deliberately the same
 * file: a synchronisation test that only runs on the host proves nothing about
 * the target, and two different files always end up diverging.
 *
 *   platform/win95/tests/run-tests.sh              on the host
 *   scripts/Push-To-Win95-VM.sh build/win95/bin/THREADS.EXE   on the target
 *
 * Endurance mode, for the defects that do not come out in ten seconds:
 *
 *   ./test_threading --stress 600                  ten minutes
 *
 * What each test establishes is written above it. A test one cannot say what it
 * proves proves nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../threading.h"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#else
#include <time.h>
#endif

/* ========================================================================== *
 * Harness
 * ========================================================================== */

static int failures = 0;
static int checks   = 0;

/* The report goes to standard output *and* to a file.
 *
 * On the target the file is not a convenience: it is the only channel. A program
 * launched from Windows 95 writes into a DOS window that closes with it, and the
 * machine is driven by screen capture from the host - reading forty lines that
 * way is not practical. The file lands on D:, which the host reads back directly
 * from the FAT16 image.
 *
 * It is flushed after every line, for the same reason as the startup log: if a
 * test freezes the machine, it is the last line written that will say which.
 */
static FILE *report_file = NULL;

static void emit(const char *fmt, ...)
{
    char    line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    fputs(line, stdout);
    fflush(stdout);
    if (report_file) {
        fputs(line, report_file);
        fflush(report_file);
    }
}

static void expect(const char *what, long long got, long long want)
{
    checks++;
    if (got == want) {
        emit("  ok    %-52s %lld\n", what, got);
    } else {
        emit("  FAIL  %-52s expected %lld, got %lld\n", what, want, got);
        failures++;
    }
}

static void expect_true(const char *what, int condition)
{
    checks++;
    if (condition) {
        emit("  ok    %s\n", what);
    } else {
        emit("  FAIL  %s\n", what);
        failures++;
    }
}

static unsigned long now_ms(void)
{
#if defined(_WIN32)
    return (unsigned long)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

/* ========================================================================== *
 * 1. The priority mapping - a pure function, no threads and no Windows
 * ========================================================================== *
 *
 * What is established: the table is the one written in the documentation, it is
 * strictly increasing, and it collapses no level. The ticket announces a "lossy"
 * mapping; it is not lossy in that direction - `ultramodern`'s five levels fit
 * inside Win32's seven classes. The real loss is elsewhere, and is not testable
 * here: see docs/WIN95-THREADING.md.
 */
static void test_priority_mapping(void)
{
    int i;
    int previous;

    emit("Priority mapping (pure function)\n");

    expect("Low        -> THREAD_PRIORITY_BELOW_NORMAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_LOW),       -1);
    expect("Normal     -> THREAD_PRIORITY_NORMAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_NORMAL),     0);
    expect("High       -> THREAD_PRIORITY_ABOVE_NORMAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_HIGH),       1);
    expect("VeryHigh   -> THREAD_PRIORITY_HIGHEST",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_VERY_HIGH),  2);
    expect("Critical   -> THREAD_PRIORITY_TIME_CRITICAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_CRITICAL),  15);

    /* Strictly increasing: without this the intended order would not be the
       obtained order, and two distinct levels could collapse together. */
    previous = dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_LOW);
    for (i = DKR_THREAD_PRIORITY_NORMAL; i <= DKR_THREAD_PRIORITY_CRITICAL; i++) {
        int current = dkr_thread_priority_to_win32(i);
        expect_true("the table is strictly increasing", current > previous);
        previous = current;
    }

    /* Out of range: refuse rather than return an arbitrary level. */
    expect("out-of-range input refused",
           dkr_thread_priority_to_win32(99), DKR_THREAD_PRIORITY_INVALID);
    expect("negative input refused",
           dkr_thread_priority_to_win32(-1), DKR_THREAD_PRIORITY_INVALID);
}

/* ========================================================================== *
 * 2. Creation and joining
 * ========================================================================== *
 *
 * What is established: the thread really does run, joining waits for its end -
 * and not merely for its start - and the created thread's identifier differs
 * from the creator's.
 */
static long          created_ran   = 0;
static unsigned long created_id    = 0;
static unsigned long creator_id    = 0;

static void created_thread_body(void *arg)
{
    (void)arg;
    dkr_sleep_ms(30);           /* joining must wait for this */
    created_id  = dkr_thread_id();
    created_ran = 1;
}

static void test_create_join(void)
{
    dkr_thread *t;

    emit("Creation and joining\n");

    creator_id  = dkr_thread_id();
    created_ran = 0;
    created_id  = 0;

    t = dkr_thread_start(created_thread_body, 0, 0);
    expect_true("the thread starts", t != 0);
    if (!t) {
        return;
    }
    expect_true("joining succeeds", dkr_thread_join(t) != 0);
    /* Read after joining: if `join` returned too early, this would still be
       zero. That is the property the test is after. */
    expect("the thread's body ran", created_ran, 1);
    expect_true("the created thread's id differs from the creator's",
                created_id != 0 && created_id != creator_id);
}

/* ========================================================================== *
 * 3. Detaching - the handle released while the thread is starting
 * ========================================================================== *
 *
 * What is established: releasing the handle *before* the thread has started
 * takes nothing out from under it.
 *
 * That is the least favourable case, and there is nothing theoretical about it.
 * On a single processor the creator keeps its quantum after starting the thread:
 * at the moment `dkr_thread_release` runs, the created thread has generally not
 * run a single instruction. If the handle also carried the function and its
 * argument, the thread would then jump into a freed slot - recycled by the CRT's
 * heap.
 *
 * The test counts the threads that really ran to the end, and the semaphore
 * makes it deterministic: without it, the test would finish before them and
 * would prove nothing.
 *
 * `ultramodern/src/timer.cpp` detaches its timer thread immediately after
 * creating it: this path is genuinely taken.
 */
#define DETACH_THREADS 8

typedef struct {
    dkr_sem      *done;
    volatile long *ran;
} detach_arg;

static void detach_body(void *arg)
{
    detach_arg *a = (detach_arg *)arg;
    __sync_fetch_and_add(a->ran, 1);
    dkr_sem_signal(a->done, 1);
}

static void test_detach(void)
{
    dkr_sem       done;
    detach_arg    arg;
    volatile long ran = 0;
    int           i;
    int           collected = 0;

    emit("Detaching during startup\n");

    expect_true("completion semaphore", dkr_sem_init(&done, 0) != 0);
    arg.done = &done;
    arg.ran  = &ran;

    for (i = 0; i < DETACH_THREADS; i++) {
        dkr_thread *t = dkr_thread_start(detach_body, &arg, 0);
        if (!t) {
            break;
        }
        /* Immediately, without giving the thread the slightest chance to have
           started. */
        dkr_thread_release(t);
    }

    for (i = 0; i < DETACH_THREADS; i++) {
        if (dkr_sem_wait_timeout(&done, 5000)) {
            collected++;
        }
    }
    expect("the eight detached threads ran to the end", collected, DETACH_THREADS);
    expect("and all ran their body", ran, DETACH_THREADS);
    dkr_sem_destroy(&done);
}

/* ========================================================================== *
 * 4. Mutual exclusion under contention
 * ========================================================================== *
 *
 * What is established: two threads incrementing a non-atomic counter under the
 * lock lose none of them. The counter is deliberately a bare `long`, with no
 * `atomic`: it is the lock being tested, not the processor.
 */
#define CONTENTION_ROUNDS 20000

static dkr_mutex contention_mutex;
static long      contention_counter = 0;

static void contention_body(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < CONTENTION_ROUNDS; i++) {
        dkr_mutex_lock(&contention_mutex);
        contention_counter++;
        dkr_mutex_unlock(&contention_mutex);
    }
}

static void test_mutual_exclusion(void)
{
    dkr_thread *a;
    dkr_thread *b;

    emit("Mutual exclusion under contention\n");

    expect_true("the lock initialises", dkr_mutex_init(&contention_mutex) != 0);
    contention_counter = 0;

    a = dkr_thread_start(contention_body, 0, 0);
    b = dkr_thread_start(contention_body, 0, 0);
    expect_true("two threads started", a != 0 && b != 0);
    if (a) { dkr_thread_join(a); }
    if (b) { dkr_thread_join(b); }

    expect("no increment lost", contention_counter, 2 * CONTENTION_ROUNDS);
    dkr_mutex_destroy(&contention_mutex);
}

/* ========================================================================== *
 * 4. The lock is non-reentrant
 * ========================================================================== *
 *
 * What is established: the behavioural difference between CRITICAL_SECTION and
 * `std::mutex` is neutralised. Win32's critical section is recursive;
 * `std::mutex` is not and deadlocks. Code that relied on that deadlock to reveal
 * a defect would see it pass by in silence.
 *
 * The layer detects reentrancy and reports it. The test intercepts the report -
 * without that it would stop the process, which is the intended behaviour in
 * production but inconvenient here.
 */
static int reentrancy_reported = 0;

static void capture_fatal(const char *message)
{
    (void)message;
    reentrancy_reported = 1;
}

static void test_reentrancy_detected(void)
{
    dkr_mutex m;

    emit("The lock is non-reentrant\n");

    dkr_mutex_init(&m);
    reentrancy_reported = 0;
    dkr_threading_set_fatal_handler(capture_fatal);

    dkr_mutex_lock(&m);
    dkr_mutex_lock(&m);           /* the fault: a std::mutex would deadlock */
    expect("reentrancy is reported", reentrancy_reported, 1);

    /* The second lock did not take place: one unlock only. */
    dkr_mutex_unlock(&m);

    /* `try_lock` on one's own lock fails, like a std::mutex's, but reports
       nothing: the caller has already allowed for failure. */
    reentrancy_reported = 0;
    expect_true("try_lock takes a free lock", dkr_mutex_try_lock(&m) != 0);
    expect("try_lock refuses reentrancy", dkr_mutex_try_lock(&m), 0);
    expect("try_lock reports nothing", reentrancy_reported, 0);
    dkr_mutex_unlock(&m);

    dkr_threading_set_fatal_handler(0);
    dkr_mutex_destroy(&m);
}

/* ========================================================================== *
 * 5. Semaphore - a signal that precedes the wait is not lost
 * ========================================================================== *
 *
 * What is established, and it is the property `ultramodern`'s game-thread
 * startup depends on: `osCreateThread` can signal `running` *before* the created
 * thread reaches its `wait`. A primitive with no memory - a badly used
 * auto-reset event, a condition variable with no predicate - would lose that
 * wake-up, and the thread would sleep forever.
 */
static void test_semaphore_signal_before_wait(void)
{
    dkr_sem s;
    int i;

    emit("Semaphore: signal before wait\n");

    expect_true("initialises", dkr_sem_init(&s, 0) != 0);

    /* Three tokens deposited before any wait. */
    dkr_sem_signal(&s, 1);
    dkr_sem_signal(&s, 2);

    for (i = 0; i < 3; i++) {
        expect_true("the token deposited in advance is returned",
                    dkr_sem_wait_timeout(&s, 1000) != 0);
    }
    /* And not one more: `signal(n)` wakes exactly n times. */
    expect("no surplus token", dkr_sem_try_wait(&s), 0);

    /* A non-zero initial count is also a deposit in advance. */
    dkr_sem_destroy(&s);
    expect_true("initialises at 2", dkr_sem_init(&s, 2) != 0);
    expect_true("first initial token", dkr_sem_try_wait(&s) != 0);
    expect_true("second initial token", dkr_sem_try_wait(&s) != 0);
    expect("then nothing more", dkr_sem_try_wait(&s), 0);
    dkr_sem_destroy(&s);
}

/* ========================================================================== *
 * 6. Semaphore - strict ping-pong, the scheduler's exact shape
 * ========================================================================== *
 *
 * What is established: `ultramodern`'s own pattern. Every game thread sleeps on
 * its own semaphore; waking one and going back to sleep is what
 * `run_next_thread_and_wait` does. Only one thread runs at a time, and the
 * alternation must be strict.
 *
 * The shared counter is non-atomic **on purpose**: if the alternation slackened,
 * both threads would touch it at once and the final total would say so. The test
 * therefore detects both the lost wake-up - it would block - and the surplus
 * wake-up.
 */
/* Two threads, two semaphores, each handing back to the other. */
typedef struct {
    dkr_sem      *mine;
    dkr_sem      *other;
    int           rounds;
    int           tag;          /* 0 or 1: which thread must run this round */
    volatile int *turn;
    volatile int *broken;
    volatile long *counter;
} pp_arg;

static void pp_body(void *arg)
{
    pp_arg *p = (pp_arg *)arg;
    int i;

    for (i = 0; i < p->rounds; i++) {
        if (!dkr_sem_wait(p->mine)) {
            *p->broken = 1;
            return;
        }
        /* Nobody else must be running here: that is what the strict alternation
           guarantees, and what the non-atomic counter checks. */
        if (*p->turn != p->tag) {
            *p->broken = 1;
        }
        *p->counter = *p->counter + 1;   /* no compound increment on volatile */
        *p->turn = 1 - p->tag;
        dkr_sem_signal(p->other, 1);
    }
}

#define PINGPONG_ROUNDS 5000

static int run_pingpong(int rounds)
{
    dkr_sem  sem_a, sem_b;
    pp_arg   arg_a, arg_b;
    dkr_thread *ta;
    dkr_thread *tb;
    volatile int  turn   = 0;
    volatile int  broken = 0;
    volatile long counter = 0;

    if (!dkr_sem_init(&sem_a, 0) || !dkr_sem_init(&sem_b, 0)) {
        return -1;
    }

    arg_a.mine = &sem_a; arg_a.other = &sem_b; arg_a.rounds = rounds;
    arg_a.tag = 0; arg_a.turn = &turn; arg_a.broken = &broken;
    arg_a.counter = &counter;

    arg_b.mine = &sem_b; arg_b.other = &sem_a; arg_b.rounds = rounds;
    arg_b.tag = 1; arg_b.turn = &turn; arg_b.broken = &broken;
    arg_b.counter = &counter;

    ta = dkr_thread_start(pp_body, &arg_a, 0);
    tb = dkr_thread_start(pp_body, &arg_b, 0);
    if (!ta || !tb) {
        return -1;
    }

    /* The kick-off: a single token, for a single thread. */
    dkr_sem_signal(&sem_a, 1);

    dkr_thread_join(ta);
    dkr_thread_join(tb);
    dkr_sem_destroy(&sem_a);
    dkr_sem_destroy(&sem_b);

    if (broken) {
        return -1;
    }
    return (counter == (long)rounds * 2) ? 0 : -1;
}

static void test_pingpong(void)
{
    emit("Semaphore: strict ping-pong (the scheduler's pattern)\n");
    expect("5000 round trips with no wake-up lost or surplus",
           run_pingpong(PINGPONG_ROUNDS), 0);
}

/* ========================================================================== *
 * 7. Manual-reset event - waking everybody
 * ========================================================================== *
 *
 * What is established: what the semaphore cannot do. A single `set` releases
 * every waiter, present and future, without the signaller having to know their
 * number.
 */
#define EVENT_WAITERS 4

typedef struct {
    dkr_event    *event;
    volatile long *woken;
} ev_arg;

static void event_body(void *arg)
{
    ev_arg *a = (ev_arg *)arg;
    if (dkr_event_wait(a->event)) {
        __sync_fetch_and_add(a->woken, 1);
    }
}

static void test_event_broadcast(void)
{
    dkr_event   e;
    dkr_thread *threads[EVENT_WAITERS];
    ev_arg      arg;
    volatile long woken = 0;
    int i;

    emit("Manual-reset event\n");

    expect_true("initialises, closed", dkr_event_init(&e, 0) != 0);
    expect("a closed event lets nobody through",
           dkr_event_wait_timeout(&e, 20), 0);

    arg.event = &e;
    arg.woken = &woken;
    for (i = 0; i < EVENT_WAITERS; i++) {
        threads[i] = dkr_thread_start(event_body, &arg, 0);
    }
    dkr_sleep_ms(50);            /* let all four reach their wait */

    dkr_event_set(&e);           /* a single signal for four waiters */
    for (i = 0; i < EVENT_WAITERS; i++) {
        if (threads[i]) { dkr_thread_join(threads[i]); }
    }
    expect("a single set wakes all four", woken, EVENT_WAITERS);

    /* It stays open: that is what distinguishes "manual" from "automatic". */
    expect_true("the event stays open after the wake-up",
                dkr_event_wait_timeout(&e, 20) != 0);

    dkr_event_reset(&e);
    expect("after reset, it refuses again",
           dkr_event_wait_timeout(&e, 20), 0);
    dkr_event_destroy(&e);
}

/* ========================================================================== *
 * 8. Thread-local variables
 * ========================================================================== *
 *
 * What is established: each thread sees its own value, a thread that has stored
 * nothing reads zero, and the slots are handed out without collision. The three
 * slots correspond to the three `thread_local`s in
 * `ultramodern/src/threads.cpp`.
 */
#define TLS_THREADS 6

static int tls_slot_a = -1;
static int tls_slot_b = -1;

typedef struct {
    int           value;
    volatile long *mismatches;
} tls_arg;

static void tls_body(void *arg)
{
    tls_arg *a = (tls_arg *)arg;
    int i;

    /* Before any write: a fresh thread reads zero. */
    if (dkr_tls_get(tls_slot_a) != 0 || dkr_tls_get(tls_slot_b) != 0) {
        __sync_fetch_and_add(a->mismatches, 1);
    }

    dkr_tls_set(tls_slot_a, (void *)(uintptr_t)a->value);
    dkr_tls_set(tls_slot_b, (void *)(uintptr_t)(a->value * 7));

    /* Read back after letting the other threads write theirs: it is isolation
       that is being tested, not memory. */
    for (i = 0; i < 200; i++) {
        dkr_yield();
        if (dkr_tls_get(tls_slot_a) != (void *)(uintptr_t)a->value ||
            dkr_tls_get(tls_slot_b) != (void *)(uintptr_t)(a->value * 7)) {
            __sync_fetch_and_add(a->mismatches, 1);
            return;
        }
    }
}

static void test_tls(void)
{
    dkr_thread   *threads[TLS_THREADS];
    tls_arg       args[TLS_THREADS];
    volatile long mismatches = 0;
    int i;
    int extra;

    emit("Thread-local variables\n");

    tls_slot_a = dkr_tls_reserve();
    tls_slot_b = dkr_tls_reserve();
    expect_true("two distinct slots",
                tls_slot_a >= 0 && tls_slot_b >= 0 && tls_slot_a != tls_slot_b);

    /* The main thread too: it is not a special case. */
    dkr_tls_set(tls_slot_a, (void *)(uintptr_t)0xABCD);

    for (i = 0; i < TLS_THREADS; i++) {
        args[i].value      = i + 1;
        args[i].mismatches = &mismatches;
        threads[i] = dkr_thread_start(tls_body, &args[i], 0);
    }
    for (i = 0; i < TLS_THREADS; i++) {
        if (threads[i]) { dkr_thread_join(threads[i]); }
    }

    expect("no thread saw another's value", mismatches, 0);
    expect("the main thread kept its own",
           (long long)(uintptr_t)dkr_tls_get(tls_slot_a), 0xABCD);

    /* The stock is finite and says so. A frank -1 beats a slot silently shared
       with another use. */
    extra = 0;
    while (dkr_tls_reserve() >= 0) {
        if (++extra > DKR_TLS_SLOTS + 4) {
            break;              /* guard rail: the dispenser does not stop */
        }
    }
    expect("the dispenser runs out at the announced count",
           extra, DKR_TLS_SLOTS - 2);
}

/* ========================================================================== *
 * 9. The CreateSemaphoreW stub is neutralised      (target only)
 * ========================================================================== *
 *
 * What is established: on the machine, `CreateSemaphoreW` returns a semaphore
 * that works - and not the zero KERNEL32's stub would return.
 *
 * This test is the only one in the file with no host equivalent, and it is worth
 * it: it is what separates a machine where `ultramodern` can run from a machine
 * where its scheduler falls apart in silence. It would fail on a Windows 95
 * without `compat.c`'s bridge, which is exactly what it is asked to watch.
 *
 * The call goes through the wide variant **on purpose**. It is the only place in
 * the project where it is called deliberately.
 */
#if defined(_WIN32)
static void test_wide_semaphore_shim(void)
{
    HANDLE h;
    LONG   previous = 0;

    emit("CreateSemaphoreW stub neutralised\n");

    h = CreateSemaphoreW(NULL, 0, 16, NULL);
    expect_true("CreateSemaphoreW returns a handle", h != NULL);
    if (!h) {
        /* With no handle, the rest would only measure derived failures. */
        return;
    }
    expect_true("the returned semaphore signals",
                ReleaseSemaphore(h, 1, &previous) != 0);
    expect("it was indeed at zero before", (long long)previous, 0);
    expect("and the token is taken back",
           (long long)WaitForSingleObject(h, 1000), (long long)WAIT_OBJECT_0);
    CloseHandle(h);
}
#endif

/* ========================================================================== *
 * 10. Endurance
 * ========================================================================== *
 *
 * Synchronisation defects are rare and non-deterministic. A ten-second run does
 * not find them; that is why the ticket asks for ten minutes under load, inside
 * the emulated machine, and not a code review.
 *
 * The loop chains the two patterns that can lose a wake-up - the scheduler's
 * ping-pong and lock contention - and stops at the first anomaly rather than at
 * the end of the allotted time.
 */
static int stress(unsigned long seconds)
{
    unsigned long start = now_ms();
    unsigned long elapsed;
    long          iterations = 0;

    emit("Endurance: %lu seconds\n", seconds);
    fflush(stdout);

    for (;;) {
        if (run_pingpong(500) != 0) {
            emit("  FAIL  ping-pong broken at round %ld\n", iterations);
            return 1;
        }

        contention_counter = 0;
        if (!dkr_mutex_init(&contention_mutex)) {
            emit("  FAIL  the lock failed to initialise\n");
            return 1;
        }
        {
            dkr_thread *a = dkr_thread_start(contention_body, 0, 0);
            dkr_thread *b = dkr_thread_start(contention_body, 0, 0);
            if (!a || !b) {
                emit("  FAIL  thread creation\n");
                return 1;
            }
            dkr_thread_join(a);
            dkr_thread_join(b);
        }
        dkr_mutex_destroy(&contention_mutex);
        if (contention_counter != 2 * CONTENTION_ROUNDS) {
            emit("  FAIL  increment lost at round %ld: %ld\n",
                   iterations, contention_counter);
            return 1;
        }

        iterations++;
        /* Unsigned subtraction: it stays correct if `GetTickCount` wraps during
           the test, which can only happen after 49.7 days but costs nothing to
           cover here. */
        elapsed = now_ms() - start;
        if ((elapsed / 1000u) >= seconds) {
            break;
        }
        if ((iterations % 20) == 0) {
            emit("  %lu s, %ld rounds\n", elapsed / 1000u, iterations);
            fflush(stdout);
        }
    }

    emit("  ok    %ld rounds with no lost wake-up and no deadlock\n", iterations);
    return 0;
}

/* ========================================================================== *
 * Entry point
 * ========================================================================== */

static void run_all(void)
{
    test_priority_mapping();
    test_create_join();
    test_detach();
    test_mutual_exclusion();
    test_reentrancy_detected();
    test_semaphore_signal_before_wait();
    test_pingpong();
    test_event_broadcast();
    test_tls();
#if defined(_WIN32)
    test_wide_semaphore_shim();
#endif
}

int main(int argc, char **argv)
{
    unsigned long stress_seconds = 0;
    int i;
    int rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stress") == 0 && i + 1 < argc) {
            stress_seconds = strtoul(argv[++i], 0, 10);
        }
    }

#if defined(_WIN32)
    /* On the target, the startup log is the only channel that survives an abrupt
       stop. With no screen and no console, it is what will carry the last line
       written before a freeze. */
    if (dkr_win95_startup("Threading and synchronisation test") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
    /* Opened before the first test, not after the last: a report written at the
       end says nothing about the test that froze the machine. Two names, so that
       the endurance run does not overwrite the suite's result - both are launched
       within the same boot of the machine. */
    report_file = fopen(stress_seconds > 0 ? "D:\\STRESS.LOG"
                                           : "D:\\THREADS.LOG", "w");
#else
    {
        const char *path = getenv("DKR_THREADS_LOG");
        if (path) {
            report_file = fopen(path, "w");
        }
    }
#endif

    if (!dkr_threading_init()) {
        emit("FAIL: dkr_threading_init\n");
        return 2;
    }

    if (stress_seconds > 0) {
        rc = stress(stress_seconds);
    } else {
        run_all();
        emit("\n%d checks, %d failure(s)\n", checks, failures);
        rc = failures != 0;
    }

    dkr_threading_shutdown();

    emit("result: %s\n", rc == 0 ? "OK" : "FAILED");
    if (report_file) {
        fclose(report_file);
        report_file = NULL;
    }
#if defined(_WIN32)
    dkr_win95_log(rc == 0 ? "test: OK" : "test: FAILED");
#endif

    return rc;
}
