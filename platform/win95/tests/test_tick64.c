/* E01-S03 - GetTickCount's wraparound, simulated.
 *
 * `GetTickCount` returns to zero after 49.7 days. The defect is never met in
 * testing and is met at a player's machine: that is precisely why the
 * accumulation logic was isolated as a pure function, drivable with chosen
 * values.
 *
 * This test runs on the host, without Windows or an emulator - it calls nothing
 * but `dkr_tick64_step`.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../compat.h"

static int failures = 0;

static void expect(const char *what, unsigned long long got, unsigned long long want)
{
    if (got == want) {
        printf("  ok    %-46s %llu\n", what, got);
    } else {
        printf("  FAIL  %-46s expected %llu, got %llu\n", what, want, got);
        failures++;
    }
}

#define TICK_MAX 0xFFFFFFFFULL

int main(void)
{
    dkr_tick64_state st = { 0, 0 };

    puts("GetTickCount wraparound (E01-S03)");

    /* Normal running: the 64-bit value follows the 32-bit one. */
    expect("starts at zero",              dkr_tick64_step(&st, 0),          0);
    expect("simple progression",          dkr_tick64_step(&st, 1000),       1000);
    expect("progression",                 dkr_tick64_step(&st, 0x7FFFFFFF), 0x7FFFFFFFULL);

    /* Just before the wraparound: 49.7 days of running. */
    expect("last tick before the wrap",   dkr_tick64_step(&st, 0xFFFFFFFF), TICK_MAX);

    /* The wraparound. Untreated, time would step back 49 days - and a duration
       computation would return an enormous or a negative value depending on the
       type. */
    expect("first tick after the wrap",   dkr_tick64_step(&st, 0),          TICK_MAX + 1);
    expect("progression after the wrap",  dkr_tick64_step(&st, 5000),       TICK_MAX + 1 + 5000);

    /* Second wraparound: about 99 days. */
    dkr_tick64_step(&st, 0xFFFFFFFF);
    expect("second return to zero",       dkr_tick64_step(&st, 0),          2 * (TICK_MAX + 1));

    /* Time never steps back, whatever the sequence. That is the only property a
       duration computation depends on. */
    {
        dkr_tick64_state s2 = { 0, 0 };
        unsigned long seq[] = { 0, 100, 0xFFFFFF00, 0xFFFFFFFF, 0, 1, 0xFFFFFFFE, 0, 7 };
        unsigned long long prev = 0;
        int monotonic = 1, i;
        for (i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])); i++) {
            unsigned long long now = dkr_tick64_step(&s2, seq[i]);
            if (now < prev) { monotonic = 0; }
            prev = now;
        }
        expect("monotonic over a sequence with two wraps", (unsigned long long)monotonic, 1);
    }

    /* A known and owned limit: if nobody reads the clock for more than 49 days,
       the wraparound goes unnoticed. It cannot be detected - two readings 49
       days apart and 1 millisecond apart are indistinguishable. The test freezes
       that behaviour so that it stays a documented choice and not a surprise.
       See docs/WIN95-COMPAT.md. */
    {
        dkr_tick64_state s3 = { 0, 0 };
        dkr_tick64_step(&s3, 1000);
        expect("wraparound missed if the clock is not read",
               dkr_tick64_step(&s3, 2000), 2000);
    }

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    puts("\nAll checks pass.");
    return 0;
}
