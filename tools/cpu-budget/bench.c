/* E00-S03 - bench for the code recompiled by N64Recomp.
 *
 * Runs a set of real DKR leaf functions against a memory image loaded from the
 * matching ELF, and measures the time per call. Compiled identically in 64-bit
 * (the reference) and in 32-bit without SSE (the Win95 target).
 *
 * The game's functions expect state this bench does not rebuild; those that
 * fault are discarded by a signal handler and do not count in the measurement.
 * The chosen subset is identical in both configurations, which is the only thing
 * the comparison requires.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>

#include "recomp.h"

/* The full KSEG0/KSEG1 window: stops the slightest stray pointer from faulting. */
#define RDRAM_WINDOW 0x20000000u
#define ELF_LOAD_BASE 0x80000000u

static uint8_t *rdram;
static sigjmp_buf escape;
static volatile sig_atomic_t faulted;

static void on_fault(int sig) { (void)sig; faulted = 1; siglongjmp(escape, 1); }

/* --- loading the ELF's PT_LOAD segments (big-endian, MIPS) ---------------- */
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int load_elf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *elf = malloc((size_t)size);
    if (fread(elf, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 0; }
    fclose(f);

    uint32_t phoff = be32(elf + 28);
    uint16_t phentsize = be16(elf + 42), phnum = be16(elf + 44);
    int loaded = 0;
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *ph = elf + phoff + (uint32_t)i * phentsize;
        if (be32(ph) != 1) continue;                    /* PT_LOAD */
        uint32_t off = be32(ph + 4), vaddr = be32(ph + 8), filesz = be32(ph + 16);
        if (vaddr < ELF_LOAD_BASE) continue;
        uint32_t dst = vaddr - ELF_LOAD_BASE;
        if ((uint64_t)dst + filesz > RDRAM_WINDOW) continue;
        memcpy(rdram + dst, elf + off, filesz);
        loaded++;
    }
    free(elf);
    return loaded;
}

/* --- fonctions mesurees --------------------------------------------------- */
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

/* A deterministic starting state: plausible registers, stack inside RDRAM. */
static void seed_context(recomp_context *ctx, uint32_t salt) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->r29 = (gpr)(int32_t)0x803FF000;               /* sp */
    ctx->r31 = (gpr)(int32_t)0x80000000;               /* ra */
    ctx->r28 = (gpr)(int32_t)0x80100000;               /* gp */
    for (int i = 4; i <= 7; i++) {                      /* a0..a3 */
        ((gpr *)ctx)[i] = (gpr)(int32_t)(0x80200000u + ((salt * 0x2Cu + (uint32_t)i * 0x40u) & 0xFFFFu));
    }
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <elf> <iterations>\n", argv[0]); return 2; }
    long iterations = atol(argv[2]);

    rdram = calloc(RDRAM_WINDOW, 1);
    if (!rdram) { fprintf(stderr, "RDRAM allocation failed\n"); return 2; }
    int segments = load_elf(argv[1]);
    if (segments <= 0) { fprintf(stderr, "no PT_LOAD segments loaded\n"); return 2; }

    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_fault; sigemptyset(&sa.sa_mask); sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGFPE, &sa, NULL);

    /* Selection pass: we keep what survives a few calls. */
    static int usable[ENTRY_COUNT];
    int kept = 0;
    recomp_context ctx;
    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        int ok = 1;
        for (uint32_t t = 0; t < 8 && ok; t++) {
            faulted = 0;
            if (sigsetjmp(escape, 1) == 0) { seed_context(&ctx, t); entries[i].fn(rdram, &ctx); }
            else ok = 0;
        }
        usable[i] = ok;
        kept += ok;
    }

    fprintf(stderr, "segments loaded: %d, functions kept: %d / %zu\n",
            segments, kept, ENTRY_COUNT);
    if (kept == 0) { fprintf(stderr, "no usable function\n"); return 2; }

    /* Measurement: each kept function, `iterations` times. */
    double total = 0.0;
    long calls = 0;
    printf("%-34s %12s %14s\n", "function", "calls", "ns/call");
    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        if (!usable[i]) continue;
        long done = 0;
        double t0, dt;
        /* The recovery point is set ONCE, outside the measured loop: sigsetjmp
           with mask saving makes a system call, and paying that per call would
           amount to measuring the harness. */
        seed_context(&ctx, 0);
        t0 = now_s();
        if (sigsetjmp(escape, 0) == 0) {
            for (long k = 0; k < iterations; k++) {
                /* Only the arguments vary; the rest of the context is already set. */
                ctx.r4 = (gpr)(int32_t)(0x80200000u + ((uint32_t)k * 0x2Cu & 0xFFFFu));
                ctx.r5 = (gpr)(int32_t)(0x80200000u + ((uint32_t)k * 0x14u & 0xFFFFu));
                entries[i].fn(rdram, &ctx);
                done++;
            }
        }
        dt = now_s() - t0;
        if (done == 0) continue;
        total += dt; calls += done;
        printf("%-34s %12ld %14.1f\n", entries[i].name, done, dt * 1e9 / (double)done);
    }
    printf("\nTOTAL %ld calls in %.4f s -> %.1f ns/call (mean)\n",
           calls, total, total * 1e9 / (double)calls);
    printf("BENCH_SECONDS %.6f\n", total);
    printf("BENCH_CALLS %ld\n", calls);
    return 0;
}
