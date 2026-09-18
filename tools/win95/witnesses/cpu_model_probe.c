/* E00-S03 / E09-S04 - how wrong is 86Box's Pentium II model?
 *
 * The port's go/no-go rests on a frame measured at 170 ms inside an *emulated*
 * Pentium II 400, of which 125 ms is recompiled MIPS code. The absolute figure is
 * 86Box's prediction; the split is a ratio and survives a uniformly mis-scaled
 * model. What no ratio survives is a model that is wrong **differently** for
 * different workloads - and `cpu-budget.md` names exactly where that could happen:
 * the model "reproduces neither the real caches, nor the branch prediction, nor
 * the period's memory bandwidth".
 *
 * That matters because the two halves of the frame have very different locality.
 * The recompiled code chases an eight-megabyte RDRAM image; the renderer works on
 * smaller, warmer buffers. If the memory hierarchy is not modelled, the 73.5 %
 * term is the one being flattered, and real silicon would be *worse* than the
 * verdict says rather than better.
 *
 * ## Why this does not cite a published benchmark
 *
 * Dhrystone and its kind have published Pentium II numbers, and they vary by
 * compiler, version and who ran them. Calibrating against a figure whose
 * provenance cannot be checked would put the same uncertainty back in by another
 * door.
 *
 * These four kernels are chosen so that their cost on a real Pentium II follows
 * from the **documented architecture** instead: a 3-wide core, a 16 KB L1 data
 * cache, 512 KB of L2 at half clock, and a 100 MHz memory bus.
 *
 *   dep_add    a dependent chain of `add reg,reg`. Latency 1, so it runs at one
 *              cycle per instruction whatever the issue width. This is the
 *              yardstick: if the model gets this wrong, nothing else it says
 *              means anything.
 *
 *   ind_add    four independent chains. A 3-wide core retires three per cycle,
 *              so this should land near a third of `dep_add`. It tests whether
 *              superscalar issue is modelled at all.
 *
 *   l1_chase   a pointer chase inside 4 KB, which fits L1 with room to spare.
 *              The P2's load-use latency is three cycles, and a chase exposes it
 *              directly because each load waits for the one before.
 *
 *   mem_chase  the same chase over eight megabytes - the size of the game's
 *              RDRAM, and sixteen times the L2. Every step is a miss to main
 *              memory. On a 100 MHz bus that is on the order of fifty cycles and
 *              more, not three.
 *
 * **The decisive number is the last ratio**, `mem_chase / l1_chase`. On silicon it
 * is tens. If 86Box reports single digits, the memory hierarchy is not in the
 * model, and every figure this project has taken from it understates the cost of
 * whichever code walks the most memory.
 *
 * The stride is 64 bytes - one cache line - and the walk order is scrambled by a
 * large odd step so that sequential prefetch cannot turn the chase into a stream.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"

#define CPU_MHZ 400.0   /* what 86Box is asked to model; see the report */

static FILE *g_out;

static void say(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

/* --- The kernels ------------------------------------------------------------ */

static unsigned dep_add(unsigned long iterations)
{
    unsigned acc = 1u;
    unsigned long i;
    for (i = 0; i < iterations; i++) {
        __asm__ __volatile__(
            "addl $1, %0\n\t" "addl $1, %0\n\t" "addl $1, %0\n\t" "addl $1, %0\n\t"
            "addl $1, %0\n\t" "addl $1, %0\n\t" "addl $1, %0\n\t" "addl $1, %0\n\t"
            : "+r"(acc) :: );
    }
    return acc;
}

static unsigned ind_add(unsigned long iterations)
{
    unsigned a = 1u, b = 2u, c = 3u, d = 4u;
    unsigned long i;
    for (i = 0; i < iterations; i++) {
        __asm__ __volatile__(
            "addl $1, %0\n\t" "addl $1, %1\n\t" "addl $1, %2\n\t" "addl $1, %3\n\t"
            "addl $1, %0\n\t" "addl $1, %1\n\t" "addl $1, %2\n\t" "addl $1, %3\n\t"
            : "+r"(a), "+r"(b), "+r"(c), "+r"(d) :: );
    }
    return a + b + c + d;
}

/* Builds a closed pointer chase over `bytes`, one stop per 64-byte line, visited
   in an order a prefetcher cannot follow. */
static unsigned int *build_chase(unsigned long bytes, unsigned long *steps_out)
{
    const unsigned long lines = bytes / 64u;
    unsigned int *a = (unsigned int *)malloc(bytes);
    unsigned long i, at = 0;
    if (!a) { *steps_out = 0; return 0; }
    memset(a, 0, bytes);
    /* A step coprime with `lines` visits every line exactly once before closing. */
    for (i = 0; i < lines; i++) {
        const unsigned long next = (at + 1009u) % lines;
        a[at * 16u] = (unsigned int)(next * 16u);
        at = next;
    }
    *steps_out = lines;
    return a;
}

static unsigned chase(const unsigned int *a, unsigned long steps)
{
    unsigned int at = 0;
    unsigned long i;
    for (i = 0; i < steps; i++) { at = a[at]; }
    return at;
}

/* **Branch prediction, the second thing the report says is not modelled.**
 *
 * It matters as much as memory here: translated MIPS is dense with branches, and a
 * Pentium II pays about fifteen cycles for a mispredict against roughly nothing for
 * a correct one. Two loops over the same array of bytes, one whose values alternate
 * in a pattern the predictor learns, one whose values are unpredictable. The
 * difference is the misprediction cost as the model charges it; on silicon it is
 * ten to fifteen cycles.
 *
 * If the two come out equal, branches are free in the model, and every count this
 * project has taken of recompiled-code time is missing whatever the real predictor
 * would have charged. */
/* `mask` wraps the walk inside the buffer. The first version indexed `pattern[i]`
   with `i` running to `n`, which is sixty times the 64 KB allocated - it read four
   megabytes past the end, faulted, and left the report file open at zero bytes.
   The screen's fingerprint, identical twenty minutes apart, is what showed it:
   nothing was still running. */
static unsigned branchy(const unsigned char *pattern, unsigned long n,
                        unsigned long mask)
{
    unsigned taken = 0;
    unsigned long i;
    for (i = 0; i < n; i++) {
        if (pattern[i & mask] & 1u) { taken += 3u; } else { taken ^= 7u; }
    }
    return taken;
}

/* --- Timing ------------------------------------------------------------------ */

static double per_op_cycles(unsigned long long us, double ops)
{
    return ops > 0.0 ? ((double)us * CPU_MHZ) / ops : 0.0;
}

int main(void)
{
    unsigned long long t0, t1;
    double dep_c, ind_c, l1_c, mem_c;
    unsigned sink = 0;
    unsigned int *small_a, *l2_a, *big_a;
    unsigned long small_n = 0, l2_n = 0, big_n = 0;
    unsigned char *pat_easy, *pat_hard;
    double l2_c, br_easy, br_hard;
    unsigned long bi;

    g_out = fopen("D:\\CPUMODEL.TXT", "w");
    if (!dkr_clock_init()) {
        fputs("no usable clock source\n", stdout);
        return 2;
    }
    say("clock %.0f Hz, modelling %.0f MHz\n",
        (double)dkr_clock_frequency(), CPU_MHZ);

    /* Each kernel runs long enough that the 838 ns tick is noise. */
    t0 = dkr_clock_now_us(); sink += dep_add(2000000ul); t1 = dkr_clock_now_us();
    dep_c = per_op_cycles(t1 - t0, 2000000.0 * 8.0);

    t0 = dkr_clock_now_us(); sink += ind_add(2000000ul); t1 = dkr_clock_now_us();
    ind_c = per_op_cycles(t1 - t0, 2000000.0 * 8.0);

    small_a = build_chase(4096ul, &small_n);
    /* 256 KB: comfortably inside the 512 KB L2 and far outside the 16 KB L1, so it
       isolates the middle tier. Three tiers reading alike would mean no hierarchy
       at all rather than a merely optimistic one. */
    l2_a    = build_chase(256ul * 1024ul, &l2_n);
    big_a   = build_chase(8ul * 1024ul * 1024ul, &big_n);
    if (!small_a || !l2_a || !big_a) { fputs("out of memory\n", stdout); return 2; }

    /* Warm the small one; the big one is a miss by construction. */
    sink += chase(small_a, small_n * 4u);
    t0 = dkr_clock_now_us(); sink += chase(small_a, small_n * 200u); t1 = dkr_clock_now_us();
    l1_c = per_op_cycles(t1 - t0, (double)small_n * 200.0);

    sink += chase(l2_a, l2_n * 2u);
    t0 = dkr_clock_now_us(); sink += chase(l2_a, l2_n * 30u); t1 = dkr_clock_now_us();
    l2_c = per_op_cycles(t1 - t0, (double)l2_n * 30.0);

    t0 = dkr_clock_now_us(); sink += chase(big_a, big_n * 2u); t1 = dkr_clock_now_us();
    mem_c = per_op_cycles(t1 - t0, (double)big_n * 2.0);

    /* Branches: the same loop, one pattern learnable and one not. */
    pat_easy = (unsigned char *)malloc(1u << 16);
    pat_hard = (unsigned char *)malloc(1u << 16);
    if (!pat_easy || !pat_hard) { fputs("out of memory\n", stdout); return 2; }
    for (bi = 0; bi < (1ul << 16); bi++) {
        pat_easy[bi] = (unsigned char)(bi & 1u);          /* strict alternation */
        pat_hard[bi] = (unsigned char)((bi * 1103515245ul + 12345ul) >> 16);
    }
    t0 = dkr_clock_now_us(); sink += branchy(pat_easy, (1ul << 16) * 60ul, 0xFFFFul); t1 = dkr_clock_now_us();
    br_easy = per_op_cycles(t1 - t0, 65536.0 * 60.0);
    t0 = dkr_clock_now_us(); sink += branchy(pat_hard, (1ul << 16) * 60ul, 0xFFFFul); t1 = dkr_clock_now_us();
    br_hard = per_op_cycles(t1 - t0, 65536.0 * 60.0);

    say("dep_add   %8.2f cycles/op   (real P2: 1.00, the latency of add)\n", dep_c);
    say("ind_add   %8.2f cycles/op   (real P2: near 0.33, three-wide)\n", ind_c);
    say("l1_chase  %8.2f cycles/step (real P2: about 3, L1 load-use)\n", l1_c);
    say("mem_chase %8.2f cycles/step (real P2: tens, on a 100 MHz bus)\n", mem_c);
    say("l2_chase  %8.2f cycles/step (real P2: about 8-12, L2 at half clock)\n", l2_c);
    say("RATIO mem/L1 = %.1f   -- on silicon this is tens\n",
        l1_c > 0.0 ? mem_c / l1_c : 0.0);
    say("branch predictable   %8.2f cycles/iter\n", br_easy);
    say("branch unpredictable %8.2f cycles/iter\n", br_hard);
    say("MISPREDICT COST = %.2f cycles -- on silicon about 10-15\n",
        br_hard - br_easy);

    free(small_a); free(l2_a); free(big_a);
    free(pat_easy); free(pat_hard);
    if (g_out) { fclose(g_out); }
    return (int)(sink & 1u);
}
