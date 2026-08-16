/* E02-S03 - what time base Windows 95 really offers.
 *
 * The ticket draws up a table of candidate sources with, for each, an assumed
 * resolution. This program replaces it with measurements taken on the machine:
 * `QueryPerformanceFrequency` "varies with the chipset", and `timeGetTime`'s
 * resolution depends on `timeBeginPeriod`, whose real effect must be observed.
 *
 * What is measured, for each source:
 *
 *   frequency      what the system announces
 *   resolution     the smallest non-zero gap observed between two consecutive
 *                  reads - that is the real granularity, not the announced one
 *   monotonicity   a step backwards, even by one tick, disqualifies a source
 *   cost           the average duration of one call
 *
 * **The cost measured here does not transfer to real hardware.** 86Box's emulation
 * is functional and not temporal; the figure gives the order of magnitude and the
 * ranking of the sources against each other, not a 1998 Pentium II's budget.
 * E09-S04 will decide on a real machine.
 *
 * Writes its report to D:\CLOCK.TXT, readable from the host.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

static FILE *out;

static void say(const char *fmt, ...)
{
    char    line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    if (out) { fputs(line, out); fflush(out); }
}

/* --- Resolution: the smallest non-zero step actually observed -------------- *
 *
 * We do not ask the system for its resolution, we observe it. A source that
 * announces the microsecond and only advances every 55 ms is the trap this
 * measurement exists to avoid.
 */
static unsigned long resolution_ms_gettickcount(void)
{
    DWORD start = GetTickCount();
    DWORD now;
    do { now = GetTickCount(); } while (now == start);
    return (unsigned long)(now - start);
}

static unsigned long resolution_ms_timegettime(void)
{
    DWORD start = timeGetTime();
    DWORD now;
    do { now = timeGetTime(); } while (now == start);
    return (unsigned long)(now - start);
}

/* For QPC we return the step in counter units, and convert it afterwards. */
static LONGLONG resolution_ticks_qpc(void)
{
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    do { QueryPerformanceCounter(&b); } while (b.QuadPart == a.QuadPart);
    return b.QuadPart - a.QuadPart;
}

/* --- Monotonicity --------------------------------------------------------- *
 *
 * A step backwards, even by a single tick, disqualifies a source: the whole
 * simulation rests on a base that never takes one. We sample tightly, where a
 * defect would show.
 */
#define MONOTONIC_SAMPLES 200000

static long monotonic_faults_qpc(void)
{
    LARGE_INTEGER previous, now;
    long faults = 0;
    int  i;
    QueryPerformanceCounter(&previous);
    for (i = 0; i < MONOTONIC_SAMPLES; i++) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart < previous.QuadPart) { faults++; }
        previous = now;
    }
    return faults;
}

static long monotonic_faults_timegettime(void)
{
    DWORD previous = timeGetTime(), now;
    long  faults = 0;
    int   i;
    for (i = 0; i < MONOTONIC_SAMPLES; i++) {
        now = timeGetTime();
        if ((long)(now - previous) < 0) { faults++; }
        previous = now;
    }
    return faults;
}

/* --- The cost of a call --------------------------------------------------- *
 *
 * The time base is consulted several times per frame. On a Pentium II, an
 * expensive system call repeated becomes a budget item in its own right
 * (E08-S01). The outer measurement goes through `timeGetTime`, whose resolution
 * we now know.
 */
#define COST_CALLS 200000

static double cost_ns(void (*fn)(void), int calls)
{
    DWORD start, elapsed;
    int   i;
    /* An empty round first, so as not to measure the first cache miss. */
    for (i = 0; i < 1000; i++) { fn(); }
    start = timeGetTime();
    for (i = 0; i < calls; i++) { fn(); }
    elapsed = timeGetTime() - start;
    return (elapsed * 1000000.0) / (double)calls;   /* ms -> ns per call */
}

static void call_gettickcount(void) { volatile DWORD v = GetTickCount(); (void)v; }
static void call_timegettime(void)  { volatile DWORD v = timeGetTime();  (void)v; }
static void call_qpc(void)          { LARGE_INTEGER v; QueryPerformanceCounter(&v); }

int main(void)
{
    LARGE_INTEGER freq, dummy;
    TIMECAPS      caps;
    BOOL          has_qpc;
    unsigned long res_gtc, res_tgt_before, res_tgt_after;
    LONGLONG      res_qpc_ticks;

    out = fopen("D:\\CLOCK.TXT", "w");
    say("Windows 95's time sources - measured, not assumed\n\n");

    /* --- What the system announces ---------------------------------------- */

    has_qpc = QueryPerformanceFrequency(&freq) && freq.QuadPart > 0
              && QueryPerformanceCounter(&dummy);
    if (has_qpc) {
        say("QueryPerformanceFrequency : %ld Hz\n", (long)freq.QuadPart);
    } else {
        say("QueryPerformanceFrequency : ABSENT or inconsistent\n");
    }

    if (timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR) {
        say("timeGetDevCaps            : period from %lu to %lu ms\n",
            (unsigned long)caps.wPeriodMin, (unsigned long)caps.wPeriodMax);
    } else {
        say("timeGetDevCaps            : failed\n");
    }

    /* --- The resolution actually observed ---------------------------------- */

    say("\nObserved resolution (smallest non-zero step)\n");

    res_gtc = resolution_ms_gettickcount();
    say("  GetTickCount            : %lu ms\n", res_gtc);

    res_tgt_before = resolution_ms_timegettime();
    say("  timeGetTime  (before)   : %lu ms\n", res_tgt_before);

    /* This is where the ticket's only setting is decided: `timeBeginPeriod(1)`
       must bring the granularity down to the millisecond. If it does not, the rest
       of the choice changes. */
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        res_tgt_after = resolution_ms_timegettime();
        say("  timeGetTime  (after timeBeginPeriod(1)) : %lu ms\n", res_tgt_after);
    } else {
        res_tgt_after = res_tgt_before;
        say("  timeBeginPeriod(1)      : REFUSED\n");
    }

    if (has_qpc) {
        res_qpc_ticks = resolution_ticks_qpc();
        say("  QueryPerformanceCounter : %ld ticks = %.3f us\n",
            (long)res_qpc_ticks,
            (double)res_qpc_ticks * 1000000.0 / (double)freq.QuadPart);
    }

    /* --- Monotonicity ------------------------------------------------------ */

    say("\nMonotonicity over %d consecutive reads\n", MONOTONIC_SAMPLES);
    if (has_qpc) {
        say("  QueryPerformanceCounter : %ld step(s) backwards\n",
            monotonic_faults_qpc());
    }
    say("  timeGetTime             : %ld step(s) backwards\n",
        monotonic_faults_timegettime());

    /* --- Cost -------------------------------------------------------------- */

    say("\nCost per call - order of magnitude under emulation, NOT transferable\n");
    say("  GetTickCount            : %.0f ns\n", cost_ns(call_gettickcount, COST_CALLS));
    say("  timeGetTime             : %.0f ns\n", cost_ns(call_timegettime, COST_CALLS));
    if (has_qpc) {
        say("  QueryPerformanceCounter : %.0f ns\n", cost_ns(call_qpc, COST_CALLS));
    }

    /* Releases the setting: under Windows 9x, a `timeBeginPeriod` left in place
       degrades the whole system until the next reboot. */
    timeEndPeriod(1);

    say("\nreport finished\n");
    if (out) { fclose(out); }
    return 0;
}
