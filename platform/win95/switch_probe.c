/* E08-S01 - implementation. See switch_probe.h. */
#include "switch_probe.h"

#include "clock.h"
#include "threading.h"

#include <stdio.h>

#define PROBE_BUDGET_US 300000ULL   /* per measurement, whatever the count */

static dkr_sem g_ping, g_pong;
static volatile long g_rounds;

static void pong_thread(void *unused)
{
    long i;
    (void)unused;
    for (i = 0; i < g_rounds; i++) {
        dkr_sem_wait(&g_ping);
        dkr_sem_signal(&g_pong, 1);
    }
}

/* Mean time of one iteration of `body`, in nanoseconds, over up to `n`
   iterations or PROBE_BUDGET_US, whichever ends first. */
#define TIME_LOOP(n, body, out_ns, out_count) do {                      \
        const unsigned long long t0_ = dkr_clock_now_us();              \
        unsigned long long t_ = t0_;                                    \
        long i_;                                                        \
        for (i_ = 0; i_ < (n); i_++) {                                  \
            body;                                                       \
            if ((i_ & 255) == 255) {                                    \
                t_ = dkr_clock_now_us();                                \
                if (t_ - t0_ > PROBE_BUDGET_US) { i_++; break; }        \
            }                                                           \
        }                                                               \
        t_ = dkr_clock_now_us();                                        \
        (out_count) = i_;                                               \
        (out_ns) = i_ ? (unsigned long)(((t_ - t0_) * 1000ULL) / (unsigned long long)i_) : 0UL; \
    } while (0)

void dkr_switch_probe(void)
{
    dkr_mutex m;
    dkr_condvar cv;
    dkr_sem s;
    unsigned long ns;
    long count;

    if (!dkr_clock_init()) {
        fprintf(stderr, "[boot][switch] no clock, probe skipped\n");
        return;
    }

    /* Uncontended mutex: GetCurrentThreadId, then the benaphore's fast path. */
    if (dkr_mutex_init(&m)) {
        TIME_LOOP(200000L, (dkr_mutex_lock(&m), dkr_mutex_unlock(&m)), ns, count);
        fprintf(stderr, "[boot][switch] mutex lock+unlock, uncontended: %lu ns (%ld)\n",
                ns, count);
        dkr_mutex_destroy(&m);
    }

    /* notify_one with nobody waiting: two more mutex round trips, no kernel. */
    if (dkr_condvar_init(&cv)) {
        TIME_LOOP(200000L, dkr_condvar_notify_one(&cv), ns, count);
        fprintf(stderr, "[boot][switch] condvar notify_one, no waiter: %lu ns (%ld)\n",
                ns, count);
        dkr_condvar_destroy(&cv);
    }

    /* A semaphore signalled then taken by the same thread: two kernel calls,
       no switch. */
    if (dkr_sem_init(&s, 0)) {
        TIME_LOOP(100000L, (dkr_sem_signal(&s, 1), dkr_sem_wait(&s)), ns, count);
        fprintf(stderr, "[boot][switch] semaphore signal+wait, same thread: %lu ns (%ld)\n",
                ns, count);
        dkr_sem_destroy(&s);
    }

    /* Ping-pong between two threads: each round is two signals, two waits and
       two thread switches. */
    if (dkr_sem_init(&g_ping, 0) && dkr_sem_init(&g_pong, 0)) {
        dkr_thread *t;
        g_rounds = 20000L;
        t = dkr_thread_start(pong_thread, NULL, 0);
        if (t != NULL) {
            TIME_LOOP(g_rounds, (dkr_sem_signal(&g_ping, 1), dkr_sem_wait(&g_pong)),
                      ns, count);
            /* Release the partner from whatever round it waits in. */
            g_rounds = count;
            dkr_sem_signal(&g_ping, 1);
            dkr_thread_release(t);
            fprintf(stderr, "[boot][switch] ping-pong round trip, two switches: %lu ns (%ld)\n",
                    ns, count);
        }
    }
}
