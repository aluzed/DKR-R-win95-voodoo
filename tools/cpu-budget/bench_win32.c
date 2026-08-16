/* E00-S03 - bench for the recompiled code, on the Windows 95 side.
 *
 * The same protocol as `bench.c` (the Linux host), the same set of functions,
 * the same inputs: the only difference is the machine. That is what makes the
 * ratio between the two measurements usable - the factor sought is the
 * normalisation between a modern core and the target's Pentium II.
 *
 * The result is written to D:\CPUBUDG.TXT, readable from the host through
 * mtools once the machine is stopped.
 *
 * Two departures from the Linux version, both imposed by the target:
 *
 *  - the emulated RDRAM is reduced to 16 MiB instead of 512. The Linux version
 *    allowed itself a window covering KSEG0 and KSEG1 because memory is free
 *    there; here, committing 512 MiB on a 64 MB machine would page, and a
 *    timing measurement under paging is worthless. The ELF's PT_LOAD segments
 *    stop at 11.04 MiB, so 16 MiB covers them all.
 *  - faults are caught by SetUnhandledExceptionFilter rather than by a signal
 *    handler. A function that addresses outside the window is discarded and
 *    reported, instead of bringing the bench down.
 *
 * The subset finally measured is written into the report: the comparison with
 * the host must bear on the same functions, otherwise it does not compare
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
    return EXCEPTION_EXECUTE_HANDLER;   /* never reached */
}

/* --- loading the PT_LOAD segments (big-endian ELF, MIPS) ------------------ */
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

/* --- measured functions --------------------------------------------------- */
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
        "CPU budget - recompiled code on the target machine\r\n"
        "=================================================\r\n");

    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        MessageBoxA(NULL, "QueryPerformanceCounter unavailable", "CPU bench", MB_ICONERROR);
        return 2;
    }
    rlen += sprintf(report + rlen, "counter frequency : %ld Hz\r\n", (long)freq.QuadPart);

    /* Reserve then commit: the window is small, but explicit. */
    rdram = (uint8_t *)VirtualAlloc(NULL, RDRAM_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!rdram) {
        MessageBoxA(NULL, "VirtualAlloc of 16 MiB failed", "CPU bench", MB_ICONERROR);
        return 2;
    }
    memset(rdram, 0, RDRAM_BYTES);

    if (load_elf(elf_path) <= 0) {
        char msg[256];
        sprintf(msg, "unreadable ELF: %s", elf_path);
        MessageBoxA(NULL, msg, "CPU bench", MB_ICONERROR);
        return 2;
    }
    rlen += sprintf(report + rlen, "memory image loaded from %s\r\n\r\n", elf_path);

    SetUnhandledExceptionFilter(on_fault);

    /* Selection pass: the same eight trial calls as on the host. */
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
    rlen += sprintf(report + rlen, "functions kept : %d / %d\r\n\r\n",
                    usable_count, (int)ENTRY_COUNT);
    rlen += sprintf(report + rlen, "%-34s %12s %12s\r\n", "function", "calls", "ns/call");

    for (i = 0; i < ENTRY_COUNT; i++) {
        long done = 0;
        double ns;
        if (!usable[i]) {
            rlen += sprintf(report + rlen, "%-34s %12s %12s\r\n", entries[i].name, "-", "discarded");
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

    MessageBoxA(NULL, "Measurement complete.\n\nResult in D:\\CPUBUDG.TXT",
                "CPU bench", MB_ICONINFORMATION);
    return 0;
}
