/* E02-S03 - monotonic time base for Windows 95.
 *
 * The contract, the measurements that dictated it and what differs from what the
 * ticket assumed are in `clock.h` and `docs/research/win95-clock.md`. This file
 * contains only the implementation.
 *
 * Like `threading.cpp`, it carries two implementations: Windows, the target, and
 * a POSIX vehicle that exists only to run the same test suite on the host. The
 * conversion to the VR4300 counter and the 32-bit accumulation are shared - they
 * are pure functions.
 */
#include "clock.h"
#include "compat.h"

#include <stddef.h>

/* ========================================================================== *
 * Shared: the conversion to the VR4300 counter
 * ========================================================================== */

unsigned long long dkr_clock_ticks_to_vr4300(unsigned long long ticks,
                                             unsigned long long frequency)
{
    unsigned long long whole, remainder;

    if (frequency == 0) {
        return 0;
    }
    /* `ticks * 46875000` would overflow beyond about 46 hours. We separate the
       quotient from the remainder: `remainder` is smaller than `frequency`, so
       its product by 46,875,000 fits in 64 bits with an enormous margin as long
       as the frequency stays under 393 GHz. */
    whole     = ticks / frequency;
    remainder = ticks % frequency;

    return whole * DKR_VR4300_COUNTER_HZ
         + (remainder * DKR_VR4300_COUNTER_HZ) / frequency;
}


#if defined(_WIN32)

/* ========================================================================== *
 * Windows - the target
 * ========================================================================== */

#include <windows.h>
#include <mmsystem.h>

#include "startup.h"

static dkr_clock_source   clock_source    = DKR_CLOCK_SOURCE_NONE;
static unsigned long long clock_frequency = 0;
static unsigned long long clock_origin    = 0;
static int                period_begun    = 0;

/* The only action that must survive an abnormal exit. Kept apart from
   `dkr_clock_shutdown` because a cleanup called from an exception filter must do
   the strict minimum: no state to reset, no allocation, nothing that could
   block. */
static void dkr_clock_release_period(void)
{
    if (period_begun) {
        timeEndPeriod(1);
        period_begun = 0;
    }
}

/* Accumulation state for the 32-bit fallback. See `tick64.c`: same reasoning,
   same function, a single copy. */
static dkr_tick64_state   timegettime_state = { 0, 0 };

/* --- Validation ------------------------------------------------------------ *
 *
 * A source is not chosen because it answers, but because it behaves. So we ask
 * it never to step backwards over a tight sampling. It is cheap - a few thousand
 * readings - and it discards at startup a source whose defect would otherwise
 * show up mid-game, as a timer that jumps.
 */
#define VALIDATION_SAMPLES 4096

static int qpc_is_sane(unsigned long long *frequency_out)
{
    LARGE_INTEGER freq, previous, now;
    int i;

    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
        return 0;
    }
    if (!QueryPerformanceCounter(&previous)) {
        return 0;
    }
    for (i = 0; i < VALIDATION_SAMPLES; i++) {
        if (!QueryPerformanceCounter(&now)) {
            return 0;
        }
        if (now.QuadPart < previous.QuadPart) {
            return 0;                       /* one backstep disqualifies it */
        }
        previous = now;
    }
    *frequency_out = (unsigned long long)freq.QuadPart;
    return 1;
}

static unsigned long long qpc_raw(void)
{
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return (unsigned long long)v.QuadPart;
}

static unsigned long long timegettime_raw(void)
{
    /* 32 bits, wraps at 49.7 days, accumulated by E01-S03's function. */
    return dkr_tick64_step(&timegettime_state, (unsigned long)timeGetTime());
}

int dkr_clock_init(void)
{
    unsigned long long frequency = 0;

    if (clock_source != DKR_CLOCK_SOURCE_NONE) {
        return 1;                           /* already in service */
    }

    /* `timeBeginPeriod(1)` first, because the fallback depends on it and because
       measurement on the target shows it costs nothing. On this machine it
       changes nothing either - `timeGetTime` already returns the millisecond -
       but nothing guarantees it will be so elsewhere. */
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        period_begun = 1;
        /* A `timeBeginPeriod` left in place degrades the whole system until
           reboot, and therefore outlives the process. Releasing it on a normal
           exit is not enough: we announce ourselves to the exception filter, so
           that it is undone even if we die. */
        dkr_win95_at_abnormal_exit(&dkr_clock_release_period);
    }

    if (qpc_is_sane(&frequency)) {
        clock_source    = DKR_CLOCK_SOURCE_QPC;
        clock_frequency = frequency;
        clock_origin    = qpc_raw();
    } else {
        /* The fallback is not a silent makeshift: it is named in the log,
           because a session running on a millisecond clock rather than a
           microsecond one behaves differently and that has to be knowable
           without guessing. */
        dkr_win95_log("clock: QueryPerformanceCounter discarded, timeGetTime fallback");
        clock_source    = DKR_CLOCK_SOURCE_TIMEGETTIME;
        clock_frequency = 1000;
        timegettime_state.high = 0;
        timegettime_state.last = 0;
        clock_origin    = timegettime_raw();
    }

    dkr_win95_log_num("clock: frequency (Hz)", (long)clock_frequency);
    return 1;
}

void dkr_clock_shutdown(void)
{
    /* Under Windows 9x, a `timeBeginPeriod` left in place degrades the whole
       system until reboot - including after the process has ended. Releasing it
       is therefore not a courtesy. */
    dkr_clock_release_period();
    clock_source    = DKR_CLOCK_SOURCE_NONE;
    clock_frequency = 0;
}

unsigned long long dkr_clock_now(void)
{
    unsigned long long raw;

    switch (clock_source) {
    case DKR_CLOCK_SOURCE_QPC:         raw = qpc_raw();          break;
    case DKR_CLOCK_SOURCE_TIMEGETTIME: raw = timegettime_raw();  break;
    default:                           return 0;
    }
    return raw - clock_origin;
}

#else

/* ========================================================================== *
 * POSIX - a test vehicle, not a supported platform
 * ========================================================================== */

#include <time.h>

static dkr_clock_source   clock_source    = DKR_CLOCK_SOURCE_NONE;
static unsigned long long clock_frequency = 0;
static unsigned long long clock_origin    = 0;

static unsigned long long monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL
         + (unsigned long long)ts.tv_nsec;
}

int dkr_clock_init(void)
{
    if (clock_source != DKR_CLOCK_SOURCE_NONE) {
        return 1;
    }
    clock_source    = DKR_CLOCK_SOURCE_QPC;   /* the closest equivalent */
    clock_frequency = 1000000000ULL;
    clock_origin    = monotonic_ns();
    return 1;
}

void dkr_clock_shutdown(void)
{
    clock_source    = DKR_CLOCK_SOURCE_NONE;
    clock_frequency = 0;
}

unsigned long long dkr_clock_now(void)
{
    if (clock_source == DKR_CLOCK_SOURCE_NONE) {
        return 0;
    }
    return monotonic_ns() - clock_origin;
}

#endif /* _WIN32 */


/* ========================================================================== *
 * Shared: what follows from `dkr_clock_now`
 * ========================================================================== */

dkr_clock_source dkr_clock_source_in_use(void) { return clock_source; }
unsigned long long dkr_clock_frequency(void)   { return clock_frequency; }

const char *dkr_clock_source_name(void)
{
    switch (clock_source) {
    case DKR_CLOCK_SOURCE_QPC:         return "QueryPerformanceCounter";
    case DKR_CLOCK_SOURCE_TIMEGETTIME: return "timeGetTime";
    default:                           return "none";
    }
}

unsigned long long dkr_clock_now_us(void)
{
    unsigned long long ticks = dkr_clock_now();
    if (clock_frequency == 0) {
        return 0;
    }
    /* The same overflow precaution as for the VR4300, and for the same reason:
       the naive product would cap out at a few hours. */
    return (ticks / clock_frequency) * 1000000ULL
         + ((ticks % clock_frequency) * 1000000ULL) / clock_frequency;
}

unsigned long long dkr_clock_vr4300_count(void)
{
    return dkr_clock_ticks_to_vr4300(dkr_clock_now(), clock_frequency);
}
