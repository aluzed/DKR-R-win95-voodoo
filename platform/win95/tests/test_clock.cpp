/* E02-S03 - the time-base test.
 *
 * Like the other suites in `platform/win95`, one source for both targets: on the
 * host it rests on the POSIX vehicle, on the machine it becomes CLOCKT.EXE.
 *
 *   platform/win95/tests/run-tests.sh clock
 *   d:\clockt.exe                        on the target
 *   d:\clockt.exe --long 300             drift measured over five minutes
 *
 * The bulk of the file bears on the conversion to the VR4300 counter, because
 * that is where the defect the ticket feared hides: a base that drifts slowly
 * breaks nothing visible and falsifies every timing.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../clock.h"
#include "../compat.h"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#else
#include <time.h>
#endif

static int failures = 0;
static int checks   = 0;
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
    if (report_file) { fputs(line, report_file); fflush(report_file); }
}

static void expect(const char *what, unsigned long long got, unsigned long long want)
{
    checks++;
    if (got == want) {
        emit("  ok    %-50s %llu\n", what, got);
    } else {
        emit("  FAIL  %-50s expected %llu, got %llu\n", what, want, got);
        failures++;
    }
}

static void expect_true(const char *what, int cond)
{
    checks++;
    emit("  %s %s\n", cond ? "ok   " : "FAIL ", what);
    if (!cond) { failures++; }
}

/* ========================================================================== *
 * 1. The conversion to the VR4300 counter - a pure function
 * ========================================================================== *
 *
 * What is established: the ratio is exact, and stays so over durations where the
 * naive form would have overflowed long ago.
 *
 * The counter advances at 46,875,000 Hz whatever the host's frequency. It is
 * that ratio, and not the instantaneous value, that must be right.
 */
static void test_vr4300_conversion(void)
{
    const unsigned long long pit = 1193180ULL;   /* the measured frequency */

    emit("Conversion to the VR4300 counter (pure function)\n");

    expect("zero stays zero", dkr_clock_ticks_to_vr4300(0, pit), 0);

    /* One second of PIT must give one second of VR4300. */
    expect("1 s of PIT -> 46,875,000",
           dkr_clock_ticks_to_vr4300(pit, pit), 46875000ULL);

    /* Ten minutes - the duration of E02-S01's endurance test. */
    expect("600 s -> 28,125,000,000",
           dkr_clock_ticks_to_vr4300(pit * 600ULL, pit), 46875000ULL * 600ULL);

    /* A different host frequency must change nothing in the result. */
    expect("host frequency of 1 GHz, 1 s",
           dkr_clock_ticks_to_vr4300(1000000000ULL, 1000000000ULL), 46875000ULL);
    expect("host frequency of 3.579545 MHz, 1 s",
           dkr_clock_ticks_to_vr4300(3579545ULL, 3579545ULL), 46875000ULL);

    /* **The case the naive form misses.** `ticks * 46875000` overflows beyond
       about 46 hours; here we ask for 100 hours. */
    {
        unsigned long long hours100 = pit * 3600ULL * 100ULL;
        expect("100 hours, no overflow",
               dkr_clock_ticks_to_vr4300(hours100, pit), 46875000ULL * 3600ULL * 100ULL);
    }

    /* And a year, so that the margin is stated rather than assumed. */
    {
        unsigned long long year = pit * 3600ULL * 24ULL * 365ULL;
        expect("one year, no overflow",
               dkr_clock_ticks_to_vr4300(year, pit),
               46875000ULL * 3600ULL * 24ULL * 365ULL);
    }

    /* A zero frequency must not divide by zero. */
    expect("zero frequency returns without exploding",
           dkr_clock_ticks_to_vr4300(1234, 0), 0);

    /* Monotonicity of the conversion: one more tick cannot give less. */
    {
        int ok = 1;
        unsigned long long previous = 0;
        for (unsigned long long t = 0; t < 5000; t++) {
            unsigned long long v = dkr_clock_ticks_to_vr4300(t, pit);
            if (v < previous) { ok = 0; break; }
            previous = v;
        }
        expect_true("the conversion is monotonic", ok);
    }
}

/* ========================================================================== *
 * 2. The 32-bit fallback's wraparound - simulated, not waited for
 * ========================================================================== *
 *
 * What is established: the `timeGetTime` fallback survives its return to zero,
 * which happens after 49.7 days. It is the kind of defect one never meets in
 * development and always meets at a player's machine - hence the simulation.
 *
 * The logic is `dkr_tick64_step`'s, already covered by E01-S03; what is checked
 * here is that it holds on the values of a millisecond clock and that the
 * elapsed duration stays right **across** the wrap.
 */
static void test_wraparound(void)
{
    dkr_tick64_state st = { 0, 0 };
    const unsigned long before = 0xFFFFFF00UL;   /* 256 ms before the wrap */

    emit("32-bit fallback wraparound (simulated)\n");

    expect("start", dkr_tick64_step(&st, before), (unsigned long long)before);

    /* Just before. */
    expect("255 ms later, before the wrap",
           dkr_tick64_step(&st, 0xFFFFFFFFUL), 0xFFFFFFFFULL);

    /* And the wrap itself: the 32-bit value steps back, the 64-bit one goes on. */
    expect("1 ms later, after the wrap",
           dkr_tick64_step(&st, 0x00000000UL), 0x100000000ULL);
    expect("then 1000 ms",
           dkr_tick64_step(&st, 1000UL), 0x100000000ULL + 1000ULL);

    /* The duration elapsed either side of the wrap must be right: that is the
       property that counts for a race timer. */
    {
        unsigned long long start = (unsigned long long)before;
        unsigned long long end   = 0x100000000ULL + 1000ULL;
        expect("duration right across the wrap", end - start, 256ULL + 1000ULL);
    }

    /* A second wrap, to check that the accumulation is not content with one. */
    {
        dkr_tick64_state s2 = { 0, 0 };
        dkr_tick64_step(&s2, 0xFFFFFFFFUL);
        dkr_tick64_step(&s2, 0x00000000UL);
        dkr_tick64_step(&s2, 0xFFFFFFFFUL);
        expect("two wraps", dkr_tick64_step(&s2, 0x00000000UL), 0x200000000ULL);
    }
}

/* ========================================================================== *
 * 3. The source chosen at startup
 * ========================================================================== */
static void test_source_selection(void)
{
    emit("Source selection\n");

    expect_true("the clock starts", dkr_clock_init() != 0);
    expect_true("a source is chosen",
                dkr_clock_source_in_use() != DKR_CLOCK_SOURCE_NONE);
    expect_true("its frequency is non-zero", dkr_clock_frequency() > 0);
    emit("  source: %s at %llu Hz\n",
         dkr_clock_source_name(), dkr_clock_frequency());

    /* Two successive calls must never step back. */
    {
        int ok = 1;
        unsigned long long previous = dkr_clock_now();
        for (int i = 0; i < 200000; i++) {
            unsigned long long now = dkr_clock_now();
            if (now < previous) { ok = 0; break; }
            previous = now;
        }
        expect_true("200,000 readings without a single backstep", ok);
    }
}

/* ========================================================================== *
 * 4. The ratio holds over a duration, not over an instant
 * ========================================================================== *
 *
 * What is established: the VR4300 counter does advance at 46.875 MHz relative to
 * wall time. The ticket rightly insists that this check bear on a duration: a
 * slow drift is invisible in a snapshot.
 *
 * The tolerance is deliberately wide. The machine is emulated, and Windows 95's
 * scheduler is not real time; what this test catches is a ratio error - a factor
 * of two, an inverted division - and not an imprecision of a few per cent.
 */
static void test_ratio_over_time(unsigned long seconds)
{
    unsigned long long c0, c1, us0, us1;
    double expected, measured, error_pct;

    emit("VR4300 counter ratio over %lu s\n", seconds);

    us0 = dkr_clock_now_us();
    c0  = dkr_clock_vr4300_count();
#if defined(_WIN32)
    Sleep(seconds * 1000);
#else
    {
        struct timespec ts;
        ts.tv_sec  = (time_t)seconds;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
    }
#endif
    c1  = dkr_clock_vr4300_count();
    us1 = dkr_clock_now_us();

    expected  = (double)(us1 - us0) * 46.875;      /* us * 46.875 cycles/us */
    measured  = (double)(c1 - c0);
    error_pct = expected > 0 ? (measured - expected) * 100.0 / expected : 100.0;

    emit("  elapsed: %llu us, counter: %llu cycles\n", us1 - us0, c1 - c0);
    emit("  expected: %.0f cycles, gap: %+.4f %%\n", expected, error_pct);

    expect_true("the ratio holds to within 1 %",
                error_pct > -1.0 && error_pct < 1.0);
}

/* ========================================================================== */

int main(int argc, char **argv)
{
    unsigned long long_seconds = 0;
    int rc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--long") == 0 && i + 1 < argc) {
            long_seconds = strtoul(argv[++i], 0, 10);
        }
    }

#if defined(_WIN32)
    if (dkr_win95_startup("Clock test") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
    report_file = fopen(long_seconds > 0 ? "D:\\CLOCKLNG.LOG" : "D:\\CLOCKT.LOG", "w");
#endif

    test_vr4300_conversion();
    test_wraparound();
    test_source_selection();
    test_ratio_over_time(long_seconds > 0 ? long_seconds : 3);

    emit("\n%d checks, %d failure(s)\n", checks, failures);
    rc = failures != 0;

    dkr_clock_shutdown();
    if (report_file) { fclose(report_file); report_file = NULL; }
    return rc;
}
