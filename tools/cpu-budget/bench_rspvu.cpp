/* E00-S04 - the cost of the RSP's vector unit, with and without SIMD.
 *
 * Rare's audio microcode is recompiled instruction by instruction, and its
 * vector operations go through `librecomp`'s emulation. That emulation has two
 * implementations, chosen at compile time by `rsp_vu.hpp`:
 *
 *   x86-64 / arm64  ->  SIMD, through SSE4.1 (or sse2neon)
 *   everything else ->  SISD, a scalar loop over the eight lanes
 *
 * On 32-bit x86, neither architecture condition holds: the scalar path is
 * therefore chosen **automatically**, with nothing to write. The question is not
 * to make it exist, it is to know what it costs.
 *
 * This bench measures the operations in the proportions the microcode actually
 * uses them - a profile taken from `aspMain.cpp`:
 *
 *   vmadh 33 · vmulf 26 · vxor 24 · vmadn 17 · vmacf 14 · vadd 13 · vmudn 10
 *   vand 8 · vsar 6 · vmadm 6 · vmudm 5 · vaddc 5 · vmudl 4 · vge 4 · vcl 4
 *   vmudh 3 · vsub 2                          (184 instructions vectorielles)
 *
 * The eight operations chosen below cover 80 % of that profile.
 *
 * Compiles for the host (SIMD) and for the target (SISD) with no source change:
 * it is the same code, only the architecture changes.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "librecomp/rsp.hpp"        /* must precede rsp_vu_impl.hpp */
#include "librecomp/rsp_vu_impl.hpp"

/* `dmem` is declared extern by rsp.hpp; the microcode defines it elsewhere.
   This bench only calls the vector unit, but the symbol must exist. */
uint8_t dmem[0x1000];

#ifdef _WIN32
#  include <windows.h>
static double now_seconds(void)
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}
#else
#  include <time.h>
static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* The audio microcode manipulates signed 16-bit samples and ADPCM coefficients:
   we fill the registers with values of that magnitude rather than with
   degenerate patterns, whose saturations and fast paths would not be
   representative. */
static void seed_registers(RSP &rsp)
{
    for (int r = 0; r < 32; r++)
        for (int lane = 0; lane < 8; lane++)
            rsp.vpu.r[r].u16(lane) = (uint16_t)(0x1234u * (r + 1) + 0x0507u * lane);
}

struct result { const char *name; double ns; };

int main(int argc, char **argv)
{
    const long iters = (argc > 1) ? atol(argv[1]) : 2000000L;
    static RSP rsp{};
    result results[9];
    int n = 0;
    double total = 0.0;
    char report[4096];
    int rlen = 0;

    seed_registers(rsp);

#define MEASURE(label, expr)                                            \
    do {                                                                \
        double t0 = now_seconds();                                      \
        for (long i = 0; i < iters; i++) { expr; }                      \
        double dt = now_seconds() - t0;                                 \
        results[n].name = label;                                        \
        results[n].ns = dt * 1e9 / (double)iters;                       \
        total += results[n].ns;                                         \
        n++;                                                            \
    } while (0)

    /* The eight dominant operations of the audio microcode. The accumulator is
       deliberately left live from one iteration to the next: that is how the
       microcode chains them (multiply-accumulate). */
    MEASURE("vmadh", (rsp.VMADH<0>(rsp.vpu.r[1], rsp.vpu.r[2], rsp.vpu.r[3])));
    MEASURE("vmulf", (rsp.VMULF<0>(rsp.vpu.r[4], rsp.vpu.r[5], rsp.vpu.r[6])));
    MEASURE("vxor",  (rsp.VXOR<0>(rsp.vpu.r[7], rsp.vpu.r[8], rsp.vpu.r[9])));
    MEASURE("vmadn", (rsp.VMADN<0>(rsp.vpu.r[10], rsp.vpu.r[11], rsp.vpu.r[12])));
    MEASURE("vmacf", (rsp.VMACF<0>(rsp.vpu.r[13], rsp.vpu.r[14], rsp.vpu.r[15])));
    MEASURE("vadd",  (rsp.VADD<0>(rsp.vpu.r[16], rsp.vpu.r[17], rsp.vpu.r[18])));
    MEASURE("vmudn", (rsp.VMUDN<0>(rsp.vpu.r[19], rsp.vpu.r[20], rsp.vpu.r[21])));
    MEASURE("vand",  (rsp.VAND<0>(rsp.vpu.r[22], rsp.vpu.r[23], rsp.vpu.r[24])));

    rlen += sprintf(report + rlen,
        "Cost of the RSP's vector unit\r\n"
        "=============================\r\n"
        "implementation : %s\r\n"
        "iterations     : %ld per operation\r\n\r\n",
        Accuracy::RSP::SIMD ? "SIMD (SSE4.1)" : "SISD (scalar, 8 lanes)",
        iters);
    rlen += sprintf(report + rlen, "%-10s %14s\r\n", "operation", "ns/op");
    for (int i = 0; i < n; i++)
        rlen += sprintf(report + rlen, "%-10s %14.2f\r\n", results[i].name, results[i].ns);
    rlen += sprintf(report + rlen, "\r\nmean of the 8 dominant ops: %.2f ns/op\r\n", total / n);

    /* An empirical sum: do not let the compiler delete the computations. */
    rlen += sprintf(report + rlen, "witness (ignore): %u\r\n",
                    (unsigned)(rsp.vpu.r[1].u16(0) ^ rsp.vpu.r[13].u16(3)));

    fputs(report, stdout);
#ifdef _WIN32
    {
        FILE *f = fopen("D:\\RSPVU.TXT", "wb");
        if (f) { fwrite(report, 1, (size_t)rlen, f); fclose(f); }
        MessageBoxA(NULL, "Measurement complete.\n\nResult in D:\\RSPVU.TXT",
                    "RSP bench", MB_ICONINFORMATION);
    }
#endif
    return 0;
}
