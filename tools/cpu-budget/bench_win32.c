/* E00-S03 — banc d'essai du code recompilé, côté Windows 95.
 *
 * Même protocole que `bench.c` (hôte Linux), même jeu de fonctions, mêmes
 * entrées : la seule différence est la machine. C'est ce qui rend le rapport
 * entre les deux mesures exploitable — le facteur cherché est la normalisation
 * entre un cœur moderne et le Pentium II de la cible.
 *
 * Le résultat est écrit dans D:\CPUBUDG.TXT, lisible depuis l'hôte par mtools
 * une fois la machine arrêtée.
 *
 * Deux écarts avec la version Linux, tous deux imposés par la cible :
 *
 *  - la RDRAM émulée est réduite à 16 Mio au lieu de 512. La version Linux
 *    s'offrait une fenêtre couvrant KSEG0 et KSEG1 parce que la mémoire y est
 *    gratuite ; ici, committer 512 Mio sur une machine de 64 Mo ferait pagi-
 *    ner, et une mesure de temps sous pagination ne vaut rien. Les segments
 *    PT_LOAD de l'ELF s'arrêtent à 11,04 Mio, donc 16 Mio les couvrent tous.
 *  - les fautes sont interceptées par SetUnhandledExceptionFilter plutôt que
 *    par un gestionnaire de signal. Une fonction qui adresse hors de la fenêtre
 *    est écartée et signalée, au lieu de faire tomber le banc.
 *
 * Le sous-ensemble finalement mesuré est écrit dans le rapport : la comparaison
 * avec l'hôte doit porter sur les mêmes fonctions, sans quoi elle ne compare
 * rien.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>

#include "recomp.h"

#define RDRAM_BYTES   (16u * 1024u * 1024u)
#define ELF_LOAD_BASE 0x80000000u

static uint8_t *rdram;
static jmp_buf   escape;
static volatile LONG faulted;

static LONG WINAPI on_fault(EXCEPTION_POINTERS *info)
{
    (void)info;
    faulted = 1;
    longjmp(escape, 1);
    return EXCEPTION_EXECUTE_HANDLER;   /* jamais atteint */
}

/* --- chargement des segments PT_LOAD (ELF big endian, MIPS) --------------- */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int load_elf(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *elf;
    uint32_t phoff;
    uint16_t phentsize, phnum, i;
    int loaded = 0;

    if (!f) return -1;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    elf = (uint8_t *)malloc((size_t)size);
    if (!elf) { fclose(f); return -1; }
    if (fread(elf, 1, (size_t)size, f) != (size_t)size) { free(elf); fclose(f); return -1; }
    fclose(f);

    phoff = be32(elf + 28);
    phentsize = be16(elf + 42);
    phnum = be16(elf + 44);
    for (i = 0; i < phnum; i++) {
        const uint8_t *ph = elf + phoff + (uint32_t)i * phentsize;
        uint32_t vaddr, off, filesz, dst;
        if (be32(ph) != 1) continue;
        off = be32(ph + 4); vaddr = be32(ph + 8); filesz = be32(ph + 16);
        if (vaddr < ELF_LOAD_BASE) continue;
        dst = vaddr - ELF_LOAD_BASE;
        if ((uint64_t)dst + filesz > RDRAM_BYTES) continue;
        memcpy(rdram + dst, elf + off, filesz);
        loaded++;
    }
    free(elf);
    return loaded;
}

/* --- fonctions mesurées --------------------------------------------------- */
typedef void (*fn_t)(uint8_t *, recomp_context *);
typedef struct { const char *name; fn_t fn; } entry_t;

#define F(n) extern void n(uint8_t *, recomp_context *);
#include "bench_decls.h"
#undef F
#define F(n) { #n, n },
static entry_t entries[] = {
#include "bench_decls.h"
};
#undef F
#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

static void seed_context(recomp_context *ctx, uint32_t salt)
{
    int i;
    memset(ctx, 0, sizeof(*ctx));
    ctx->r29 = (gpr)(int32_t)0x803FF000;
    ctx->r31 = (gpr)(int32_t)0x80000000;
    ctx->r28 = (gpr)(int32_t)0x80100000;
    for (i = 4; i <= 7; i++)
        ((gpr *)ctx)[i] = (gpr)(int32_t)(0x80200000u + ((salt * 0x2Cu + (uint32_t)i * 0x40u) & 0xFFFFu));
}

int main(int argc, char **argv)
{
    static char report[16384];
    int rlen = 0;
    LARGE_INTEGER freq, t0, t1;
    recomp_context ctx;
    size_t i;
    long iterations;
    FILE *out;
    int usable_count = 0;
    static int usable[ENTRY_COUNT];

    const char *elf_path = (argc > 1) ? argv[1] : "D:\\DKR.ELF";
    iterations = (argc > 2) ? atol(argv[2]) : 200000L;

    rlen += sprintf(report + rlen,
        "Budget CPU - code recompile sur la machine cible\r\n"
        "===============================================\r\n");

    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        MessageBoxA(NULL, "QueryPerformanceCounter indisponible", "Banc CPU", MB_ICONERROR);
        return 2;
    }
    rlen += sprintf(report + rlen, "frequence du compteur : %ld Hz\r\n", (long)freq.QuadPart);

    /* Reserver puis committer : la fenetre est petite, mais explicite. */
    rdram = (uint8_t *)VirtualAlloc(NULL, RDRAM_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!rdram) {
        MessageBoxA(NULL, "VirtualAlloc 16 Mio a echoue", "Banc CPU", MB_ICONERROR);
        return 2;
    }
    memset(rdram, 0, RDRAM_BYTES);

    if (load_elf(elf_path) <= 0) {
        char msg[256];
        sprintf(msg, "ELF illisible : %s", elf_path);
        MessageBoxA(NULL, msg, "Banc CPU", MB_ICONERROR);
        return 2;
    }
    rlen += sprintf(report + rlen, "image memoire chargee depuis %s\r\n\r\n", elf_path);

    SetUnhandledExceptionFilter(on_fault);

    /* Passe de selection : mêmes huit appels d'essai que sur l'hote. */
    for (i = 0; i < ENTRY_COUNT; i++) {
        int ok = 1;
        uint32_t t;
        for (t = 0; t < 8 && ok; t++) {
            faulted = 0;
            if (setjmp(escape) == 0) { seed_context(&ctx, t); entries[i].fn(rdram, &ctx); }
            else ok = 0;
        }
        usable[i] = ok;
        usable_count += ok;
    }
    rlen += sprintf(report + rlen, "fonctions retenues : %d / %d\r\n\r\n",
                    usable_count, (int)ENTRY_COUNT);
    rlen += sprintf(report + rlen, "%-34s %12s %12s\r\n", "fonction", "appels", "ns/appel");

    for (i = 0; i < ENTRY_COUNT; i++) {
        long done = 0;
        double ns;
        if (!usable[i]) {
            rlen += sprintf(report + rlen, "%-34s %12s %12s\r\n", entries[i].name, "-", "ecartee");
            continue;
        }
        seed_context(&ctx, 0);
        QueryPerformanceCounter(&t0);
        if (setjmp(escape) == 0) {
            long k;
            for (k = 0; k < iterations; k++) {
                ctx.r4 = (gpr)(int32_t)(0x80200000u + ((uint32_t)k * 0x2Cu & 0xFFFFu));
                ctx.r5 = (gpr)(int32_t)(0x80200000u + ((uint32_t)k * 0x14u & 0xFFFFu));
                entries[i].fn(rdram, &ctx);
                done++;
            }
        }
        QueryPerformanceCounter(&t1);
        if (done == 0) continue;
        ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)freq.QuadPart / (double)done;
        rlen += sprintf(report + rlen, "%-34s %12ld %12.1f\r\n", entries[i].name, done, ns);
    }

    out = fopen("D:\\CPUBUDG.TXT", "wb");
    if (out) { fwrite(report, 1, (size_t)rlen, out); fclose(out); }

    MessageBoxA(NULL, "Mesure terminee.\n\nResultat dans D:\\CPUBUDG.TXT",
                "Banc CPU", MB_ICONINFORMATION);
    return 0;
}
