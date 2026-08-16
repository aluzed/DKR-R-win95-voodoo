/* E02-S02 - the C++ bridge tested, in the exact forms `ultramodern` uses.
 *
 * The bridge does not have to be a complete `std::thread`: it has to be correct
 * on the few forms the calling code actually uses. Every test below therefore
 * reproduces a real line of `ultramodern`, quoted above it.
 *
 * Like `test_threading.cpp`, one source for both targets.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../threading.hpp"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#endif

using dkr::win95::thread;
using dkr::win95::mutex;
using dkr::win95::lock_guard;
using dkr::win95::unique_lock;
using dkr::win95::condition_variable;
using dkr::win95::cv_status;

static int failures = 0;
static int checks   = 0;

static void expect(const char *what, long long got, long long want)
{
    checks++;
    if (got == want) {
        printf("  ok    %-52s %lld\n", what, got);
    } else {
        printf("  FAIL  %-52s expected %lld, got %lld\n", what, want, got);
        failures++;
    }
    fflush(stdout);
}

static void expect_true(const char *what, int cond)
{
    checks++;
    printf("  %s %s\n", cond ? "ok   " : "FAIL ", what);
    if (!cond) { failures++; }
    fflush(stdout);
}

/* ========================================================================== *
 * 1. Variadic construction, as in threads.cpp:273
 * ========================================================================== *
 *
 *   context->host_thread = std::thread{_thread_func, PASS_RDRAM t_, entrypoint,
 *                                      arg, t->context};
 *
 * Four arguments, of different types, and a move assignment onto a
 * default-constructed member. It is the most demanding form in the file.
 */
static volatile long   v_sum   = 0;
static volatile long   v_calls = 0;

static void four_args(uint8_t *rdram, int id, const char *name, void *ctx)
{
    /* The values are checked inside the thread, not afterwards: it is argument
       passing that is being tested. */
    if (rdram == (uint8_t *)0x1234 && id == 7 &&
        strcmp(name, "gfx") == 0 && ctx == (void *)0xABCD) {
        __sync_fetch_and_add(&v_sum, 1);
    }
    __sync_fetch_and_add(&v_calls, 1);
}

static void test_variadic_construction(void)
{
    thread t;                       /* default-constructed member */

    puts("Variadic construction and move assignment");
    expect_true("a default-constructed thread is not joinable", !t.joinable());

    v_sum = 0; v_calls = 0;
    t = thread{four_args, (uint8_t *)0x1234, 7, "gfx", (void *)0xABCD};
    expect_true("it becomes joinable", t.joinable());
    t.join();
    expect_true("and is not joinable after join", !t.joinable());

    expect("the body was called once", v_calls, 1);
    expect("the four arguments arrived intact", v_sum, 1);
}

/* ========================================================================== *
 * 2. The arguments are copied, as std::thread does
 * ========================================================================== *
 *
 * `events.cpp:576` passes `&gfx_thread_ready`, a local variable - but also
 * `rdram`, a value. A bridge that kept references to its arguments would read a
 * dead stack as soon as the caller returned.
 */
static volatile long copied_ok = 0;

static void takes_value(int by_value)
{
    if (by_value == 99) { __sync_fetch_and_add(&copied_ok, 1); }
}

static void test_arguments_are_copied(void)
{
    thread t;

    puts("The arguments are copied, not referenced");
    copied_ok = 0;
    {
        int local = 99;
        t = thread{takes_value, local};
        local = 0;              /* if the argument were a reference, the thread would read 0 */
    }
    t.join();
    expect("the thread saw the value as of construction time", copied_ok, 1);
}

/* ========================================================================== *
 * 3. detach, as in timer.cpp:144-145
 * ========================================================================== *
 *
 *   timer_context.thread = std::thread{ timer_thread, PASS_RDRAM1 };
 *   timer_context.thread.detach();
 *
 * Detached immediately after construction - the case where the handle dies
 * before the thread has started.
 */
static dkr_sem detach_done;

static void detached_body(int n)
{
    (void)n;
    dkr_sem_signal(&detach_done, 1);
}

static void test_detach(void)
{
    thread t;

    puts("Immediate detach");
    dkr_sem_init(&detach_done, 0);

    t = thread{detached_body, 5};
    t.detach();
    expect_true("after detach, no longer joinable", !t.joinable());
    expect_true("the detached thread did run",
                dkr_sem_wait_timeout(&detach_done, 5000) != 0);
    dkr_sem_destroy(&detach_done);
}

/* ========================================================================== *
 * 4. lock_guard in both forms, as in events.cpp and renderer_context.cpp
 * ========================================================================== *
 *
 *   std::lock_guard lock{ events_context.message_mutex };      (deduction)
 *   std::lock_guard<std::mutex> lock(graphic_config_mutex);    (explicit)
 */
#define GUARD_ROUNDS 20000

static mutex guard_mutex;
static long  guard_counter = 0;

static void guard_body(int form)
{
    for (int i = 0; i < GUARD_ROUNDS; i++) {
        if (form == 0) {
            lock_guard lock{ guard_mutex };            /* deduced form */
            guard_counter++;
        } else {
            lock_guard<mutex> lock(guard_mutex);       /* explicit form */
            guard_counter++;
        }
    }
}

static void test_lock_guard(void)
{
    puts("lock_guard, deduction and explicit form");
    guard_counter = 0;

    thread a{guard_body, 0};
    thread b{guard_body, 1};
    a.join();
    b.join();

    expect("no increment lost", guard_counter, 2 * GUARD_ROUNDS);
}

/* ========================================================================== *
 * 5. Lifetime faults are reported
 * ========================================================================== *
 *
 * `std::thread` calls `std::terminate` if a joinable thread is destroyed, or if
 * a joinable thread is overwritten by assignment. The bridge must have the same
 * character: a loud fault, not a quiet leak.
 */
static int fatal_seen = 0;

static void capture_fatal(const char *m) { (void)m; fatal_seen++; }

static void noop(void) {}

static void test_lifetime_faults(void)
{
    puts("Lifetime faults");
    dkr_threading_set_fatal_handler(capture_fatal);

    /* Overwrite a still-joinable thread. */
    fatal_seen = 0;
    {
        thread t{noop};
        thread u{noop};
        t = static_cast<thread &&>(u);      /* t is still joinable: a fault */
        expect("overwriting a joinable thread is reported", fatal_seen, 1);
        /* The handler returned, so the assignment took place: `t` now holds
           `u`'s thread, and `u` is empty. One join only. */
        t.join();
    }

    /* join on an already joined thread. */
    fatal_seen = 0;
    {
        thread t{noop};
        t.join();
        t.join();
        expect("the double join is reported", fatal_seen, 1);
    }

    dkr_threading_set_fatal_handler(0);
}

/* ========================================================================== *
 * 6. Condition variable - patch 0013's message queue
 * ========================================================================== *
 *
 * A literal reproduction of `ExternalMessageQueue`: a producer deposits under
 * the lock then calls `notify_one`, a consumer waits on
 * `wait(lock, predicate)`.
 *
 * What is established: nothing is lost. The producer emits its N messages as
 * fast as it can, without caring whether the consumer is already waiting - that
 * is exactly the window where a badly built condition variable loses a wake-up,
 * and where the consumer would sleep forever.
 */
#define CV_MESSAGES 3000

static mutex              q_mutex;
static condition_variable q_cond;
static int                q_buffer[CV_MESSAGES];
static int                q_count    = 0;
static long               q_consumed = 0;
static long               q_sum      = 0;

static void cv_producer(int n)
{
    for (int i = 1; i <= n; i++) {
        {
            lock_guard lock{ q_mutex };
            q_buffer[q_count++] = i;
        }
        q_cond.notify_one();
    }
}

static void cv_consumer(int n)
{
    for (int i = 0; i < n; i++) {
        unique_lock lock{ q_mutex };
        q_cond.wait(lock, [] { return q_count > 0; });
        q_sum += q_buffer[--q_count];
        q_consumed++;
    }
}

static void test_condition_variable(void)
{
    puts("Condition variable: producer / consumer");

    q_count = 0; q_consumed = 0; q_sum = 0;

    /* The consumer starts first and waits on an empty queue; then the producer
       overtakes it. Both orders are thus exercised. */
    thread c{cv_consumer, CV_MESSAGES};
    thread p{cv_producer, CV_MESSAGES};
    p.join();
    c.join();

    expect("every message consumed", q_consumed, CV_MESSAGES);
    expect("and none lost or counted twice", q_sum,
           (long long)CV_MESSAGES * (CV_MESSAGES + 1) / 2);
}

/* ========================================================================== *
 * 7. A signal emitted before the wait is not lost
 * ========================================================================== *
 *
 * The dangerous window, isolated: the producer deposits **everything** before
 * the consumer exists. A condition variable with no memory - and a condition
 * variable has none - would wake nobody; it is the predicate that must save the
 * wait. We check that the combination of the two holds.
 */
static void test_notify_before_wait(void)
{
    puts("Signal before wait");

    q_count = 0; q_consumed = 0; q_sum = 0;

    cv_producer(10);                /* everything deposited, nobody waiting */
    q_cond.notify_all();            /* and the wake-up goes nowhere */

    thread c{cv_consumer, 10};      /* the consumer only arrives afterwards */
    c.join();

    expect("the ten messages are found again", q_consumed, 10);
    expect("sum intact", q_sum, 55);
}

/* ========================================================================== *
 * 8. notify_all wakes every waiter
 * ========================================================================== */
static mutex              bc_mutex;
static condition_variable bc_cond;
static bool               bc_open  = false;
static volatile long      bc_woken = 0;

static void bc_waiter(int unused)
{
    (void)unused;
    unique_lock lock{ bc_mutex };
    bc_cond.wait(lock, [] { return bc_open; });
    __sync_fetch_and_add(&bc_woken, 1);
}

static void test_notify_all(void)
{
    puts("notify_all");
    bc_open = false; bc_woken = 0;

    thread w1{bc_waiter, 1};
    thread w2{bc_waiter, 2};
    thread w3{bc_waiter, 3};
    dkr_sleep_ms(60);               /* all three reach their wait */

    {
        lock_guard lock{ bc_mutex };
        bc_open = true;
    }
    bc_cond.notify_all();

    w1.join(); w2.join(); w3.join();
    expect("a single notify_all wakes all three", bc_woken, 3);
}

/* ========================================================================== *
 * 9. wait_for times out, and returns with the lock retaken
 * ========================================================================== *
 *
 * This is `wait_external_message`'s shape: a loop that retests its condition
 * every millisecond. Two things must hold - the timeout must happen when nothing
 * arrives, and the lock must be **retaken** on return, otherwise the caller's
 * `while` would read its condition unprotected.
 */
static mutex              to_mutex;
static condition_variable to_cond;
static bool               to_flag = false;

static void test_wait_for_timeout(void)
{
    puts("wait_for: timeout and lock retaken");

    {
        unique_lock lock{ to_mutex };
        cv_status s = to_cond.wait_for(lock, std::chrono::milliseconds{30});
        expect("times out when nothing arrives",
               (long long)(s == cv_status::timeout), 1);
        expect_true("the lock is retaken on return", lock.owns_lock());
        /* Independent proof: if the lock were not held, this try_lock from the
           same thread would succeed instead of failing. */
        expect("the lock is indeed held by us", (long long)to_mutex.try_lock(), 0);
    }

    /* And it does *not* time out when a signal arrives. */
    to_flag = false;
    thread s{[](int) {
        dkr_sleep_ms(20);
        { lock_guard lock{ to_mutex }; to_flag = true; }
        to_cond.notify_one();
    }, 0};
    {
        unique_lock lock{ to_mutex };
        cv_status st = to_cond.wait_for(lock, std::chrono::milliseconds{4000});
        expect("does not return timeout when the signal arrives",
               (long long)(st == cv_status::no_timeout), 1);
        expect_true("and the condition is true", to_flag);
    }
    s.join();
}

/* ========================================================================== *
 * 10. Repeated hand-off - the lost-wake-up window, hammered
 * ========================================================================== *
 *
 * The previous tests exercise the condition variable, but poorly exercise the
 * window that makes it delicate: the one between releasing the caller's lock and
 * registering the waiter. A signal emitted there must be kept; if it falls, the
 * waiter sleeps forever.
 *
 * In an ordinary producer/consumer flow, a lost wake-up goes unnoticed - the
 * next message emits another one, which catches up. The only way to make it
 * visible is to arrange that there is **never** a next message: a strict
 * hand-off, one message at a time, where the consumer must necessarily fall
 * asleep and where nothing else will come to wake it.
 *
 * Every round crosses the window again. A faulty ordering eventually falls into
 * it, and the test stops terminating.
 */
#define HANDOFF_ROUNDS 20000

static mutex              ho_mutex;
static condition_variable ho_to_consumer;
static condition_variable ho_to_producer;
static int                ho_item  = 0;      /* 0: empty, 1: full */
static long               ho_taken = 0;

static void handoff_consumer(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        unique_lock lock{ ho_mutex };
        ho_to_consumer.wait(lock, [] { return ho_item != 0; });
        ho_item = 0;
        ho_taken++;
        ho_to_producer.notify_one();
    }
}

static void test_handoff(void)
{
    puts("Strict hand-off (the lost-wake-up window)");
    ho_item = 0; ho_taken = 0;

    thread c{handoff_consumer, HANDOFF_ROUNDS};

    for (int i = 0; i < HANDOFF_ROUNDS; i++) {
        unique_lock lock{ ho_mutex };
        ho_to_producer.wait(lock, [] { return ho_item == 0; });
        ho_item = 1;
        ho_to_consumer.notify_one();
    }
    c.join();

    expect("20000 hand-offs without a lost wake-up", ho_taken, HANDOFF_ROUNDS);
}

/* ========================================================================== */

int main(void)
{
#if defined(_WIN32)
    if (dkr_win95_startup("C++ bridge test") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
#endif
    if (!dkr_threading_init()) {
        puts("FAIL: dkr_threading_init");
        return 2;
    }

    test_variadic_construction();
    test_arguments_are_copied();
    test_detach();
    test_lock_guard();
    test_lifetime_faults();
    test_condition_variable();
    test_notify_before_wait();
    test_notify_all();
    test_wait_for_timeout();
    test_handoff();

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    dkr_threading_shutdown();

#if defined(_WIN32)
    {
        FILE *f = fopen("D:\\THRCPP.LOG", "w");
        if (f) {
            fprintf(f, "C++ bridge: %d checks, %d failure(s)\n", checks, failures);
            fclose(f);
        }
    }
#endif
    return failures != 0;
}
