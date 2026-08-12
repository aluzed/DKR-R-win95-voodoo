/* E00-S04 — coût de l'unité vectorielle du RSP, avec et sans SIMD.
 *
 * Le microcode audio de Rare est recompilé instruction par instruction, et ses
 * opérations vectorielles passent par l'émulation de `librecomp`. Celle-ci a
 * deux implémentations, choisies à la compilation par `rsp_vu.hpp` :
 *
 *   x86-64 / arm64  ->  SIMD, via SSE4.1 (ou sse2neon)
 *   tout le reste   ->  SISD, boucle scalaire sur les huit voies
 *
 * Sur x86 32 bits, aucune des deux conditions d'architecture n'est vraie : le
 * chemin scalaire est donc retenu **automatiquement**, sans rien à écrire. La
 * question n'est pas de le faire exister, elle est de savoir ce qu'il coûte.
 *
 * Ce banc mesure les opérations dans les proportions où le microcode les
 * emploie réellement — profil relevé sur `aspMain.cpp` :
 *
 *   vmadh 33 · vmulf 26 · vxor 24 · vmadn 17 · vmacf 14 · vadd 13 · vmudn 10
 *   vand 8 · vsar 6 · vmadm 6 · vmudm 5 · vaddc 5 · vmudl 4 · vge 4 · vcl 4
 *   vmudh 3 · vsub 2                          (184 instructions vectorielles)
 *
 * Les huit opérations retenues ci-dessous couvrent 80 % de ce profil.
 *
 * Se compile pour l'hôte (SIMD) et pour la cible (SISD) sans changement de
 * source : c'est le même code, seule l'architecture change.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "librecomp/rsp.hpp"        /* doit precéder rsp_vu_impl.hpp */
#include "librecomp/rsp_vu_impl.hpp"

/* `dmem` est declare extern par rsp.hpp ; le microcode le definit ailleurs.
   Ce banc n'appelle que l'unite vectorielle, mais le symbole doit exister. */
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

/* Le microcode audio manipule des échantillons 16 bits signés et des
   coefficients ADPCM : on remplit les registres avec des valeurs de cet
   ordre plutôt qu'avec des motifs dégénérés, dont les saturations et les
   chemins rapides ne seraient pas représentatifs. */
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

    /* Les huit opérations dominantes du microcode audio. L'accumulateur est
       volontairement laissé vivant d'une itération à l'autre : c'est ainsi que
       le microcode les enchaîne (multiplication-accumulation). */
    MEASURE("vmadh", (rsp.VMADH<0>(rsp.vpu.r[1], rsp.vpu.r[2], rsp.vpu.r[3])));
    MEASURE("vmulf", (rsp.VMULF<0>(rsp.vpu.r[4], rsp.vpu.r[5], rsp.vpu.r[6])));
    MEASURE("vxor",  (rsp.VXOR<0>(rsp.vpu.r[7], rsp.vpu.r[8], rsp.vpu.r[9])));
    MEASURE("vmadn", (rsp.VMADN<0>(rsp.vpu.r[10], rsp.vpu.r[11], rsp.vpu.r[12])));
    MEASURE("vmacf", (rsp.VMACF<0>(rsp.vpu.r[13], rsp.vpu.r[14], rsp.vpu.r[15])));
    MEASURE("vadd",  (rsp.VADD<0>(rsp.vpu.r[16], rsp.vpu.r[17], rsp.vpu.r[18])));
    MEASURE("vmudn", (rsp.VMUDN<0>(rsp.vpu.r[19], rsp.vpu.r[20], rsp.vpu.r[21])));
    MEASURE("vand",  (rsp.VAND<0>(rsp.vpu.r[22], rsp.vpu.r[23], rsp.vpu.r[24])));

    rlen += sprintf(report + rlen,
        "Cout de l'unite vectorielle du RSP\r\n"
        "==================================\r\n"
        "implementation : %s\r\n"
        "iterations     : %ld par operation\r\n\r\n",
        Accuracy::RSP::SIMD ? "SIMD (SSE4.1)" : "SISD (scalaire, 8 voies)",
        iters);
    rlen += sprintf(report + rlen, "%-10s %14s\r\n", "operation", "ns/op");
    for (int i = 0; i < n; i++)
        rlen += sprintf(report + rlen, "%-10s %14.2f\r\n", results[i].name, results[i].ns);
    rlen += sprintf(report + rlen, "\r\nmoyenne des 8 dominantes : %.2f ns/op\r\n", total / n);

    /* Somme empirique : ne pas laisser le compilateur supprimer les calculs. */
    rlen += sprintf(report + rlen, "temoin (ignorer) : %u\r\n",
                    (unsigned)(rsp.vpu.r[1].u16(0) ^ rsp.vpu.r[13].u16(3)));

    fputs(report, stdout);
#ifdef _WIN32
    {
        FILE *f = fopen("D:\\RSPVU.TXT", "wb");
        if (f) { fwrite(report, 1, (size_t)rlen, f); fclose(f); }
        MessageBoxA(NULL, "Mesure terminee.\n\nResultat dans D:\\RSPVU.TXT",
                    "Banc RSP", MB_ICONINFORMATION);
    }
#endif
    return 0;
}
