/* E08-S01 - implementation. See sampler.h. */
#include "sampler.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLER_MAX_THREADS 64
#define SAMPLER_CHUNK       16384   /* records buffered before a write */

typedef struct {
    unsigned int eip;
    unsigned int thread;   /* index into the registry */
} sampler_record;

static HANDLE           g_threads[SAMPLER_MAX_THREADS];
static volatile LONG    g_thread_count = 0;
static int              g_enabled = -1;
static sampler_record   g_chunk[SAMPLER_CHUNK];
static FILE            *g_file;

static int sampler_enabled(void)
{
    if (g_enabled < 0) { g_enabled = getenv("DKR_TRACE_SAMPLER") != NULL; }
    return g_enabled;
}

void dkr_sampler_register_thread(void *handle)
{
    HANDLE dup = NULL;
    LONG slot;
    if (!sampler_enabled() || handle == NULL) { return; }
    if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)handle, GetCurrentProcess(), &dup,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return;
    }
    slot = InterlockedIncrement((LONG *)&g_thread_count) - 1;
    if (slot >= SAMPLER_MAX_THREADS) { CloseHandle(dup); return; }
    g_threads[slot] = dup;
}

void dkr_sampler_register_current_thread(void)
{
    dkr_sampler_register_thread(GetCurrentThread());
}

/* The file is a sequence of tagged blocks, so that module maps and samples can
 * interleave: 'M', a u32 length and that many bytes of text (one line per module:
 * base, size, name), or 'R', a u32 count and that many records. */
static void write_modules(FILE *f)
{
    char text[8192];
    size_t used = 0;
    unsigned int length;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    MODULEENTRY32 m;
    if (snap != INVALID_HANDLE_VALUE) {
        m.dwSize = sizeof(m);
        if (Module32First(snap, &m)) {
            do {
                const int n = _snprintf(text + used, sizeof(text) - used, "%08lX %08lX %s\n",
                                        (unsigned long)(size_t)m.modBaseAddr,
                                        (unsigned long)m.modBaseSize, m.szModule);
                if (n < 0 || (size_t)n >= sizeof(text) - used) { break; }
                used += (size_t)n;
            } while (Module32Next(snap, &m));
        }
        CloseHandle(snap);
    }
    length = (unsigned int)used;
    fputc('M', f);
    fwrite(&length, sizeof(length), 1, f);
    fwrite(text, 1, used, f);
}

static void write_records(FILE *f, unsigned int count)
{
    fputc('R', f);
    fwrite(&count, sizeof(count), 1, f);
    fwrite(g_chunk, sizeof(sampler_record), count, f);
}

static DWORD WINAPI sampler_thread(LPVOID unused)
{
    unsigned int used = 0, ticks = 0;
    HANDLE self = GetCurrentThread();
    (void)unused;
    for (;;) {
        LONG i, n;
        Sleep(1);
        ticks++;
        n = g_thread_count;
        if (n > SAMPLER_MAX_THREADS) { n = SAMPLER_MAX_THREADS; }
        for (i = 0; i < n; i++) {
            CONTEXT ctx;
            HANDLE h = g_threads[i];
            if (h == NULL || h == self) { continue; }
            if (SuspendThread(h) == (DWORD)-1) { continue; }
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &ctx)) {
                g_chunk[used].eip = (unsigned int)ctx.Eip;
                g_chunk[used].thread = (unsigned int)i;
                used++;
            }
            ResumeThread(h);
            if (used == SAMPLER_CHUNK) {
                write_records(g_file, used);
                fflush(g_file);
                used = 0;
            }
        }
        /* The module map again every ~10 s: Glide and friends load after the
           sampler starts. The report keeps the last map. */
        if ((ticks % 10000u) == 0u) {
            if (used) { write_records(g_file, used); used = 0; }
            write_modules(g_file);
            fflush(g_file);
        }
    }
    return 0;
}

int dkr_sampler_start(void)
{
    HANDLE t;
    DWORD id;
    if (!sampler_enabled()) { return 0; }
    g_file = fopen("D:\\SAMPLES.BIN", "wb");
    if (!g_file) { return 0; }
    fwrite("DKRS", 1, 4, g_file);
    write_modules(g_file);
    fflush(g_file);
    t = CreateThread(NULL, 0, sampler_thread, NULL, 0, &id);
    if (!t) { return 0; }
    SetThreadPriority(t, THREAD_PRIORITY_TIME_CRITICAL);
    CloseHandle(t);
    return 1;
}
