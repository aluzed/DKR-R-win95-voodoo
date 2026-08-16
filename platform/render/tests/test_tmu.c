/* E05-S02 — the TMU allocator, tested without a card.
 *
 * The missing ROM forbids testing the allocator in the game, and the card says
 * nothing about what it receives: `grTexDownloadMipMap` returns no error code.
 * This suite is therefore the only available check on the reasoning, and it must
 * be exhaustive where it can be.
 *
 * It checks three things of different natures:
 *
 *   - **the tree's invariants** — two allocations never overlap, and none leaves
 *     real memory. This is checked by brute force over every live allocation,
 *     not by sampling;
 *   - **the absence of external fragmentation**, the property that justified
 *     choosing the buddy over a size-class allocator;
 *   - **the eviction policy**, whose interesting case is not "evict the oldest"
 *     but "do not evict what the current frame needs".
 */
#include "render/tmu.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok   " : "FAIL ", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", ok ? "ok   " : "FAIL ", what);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static void report(const char *fmt, unsigned long a, unsigned long b)
{
    char line[200];
    sprintf(line, fmt, a, b);
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

/* --- The fake transfer -------------------------------------------------------- *
 *
 * It records what it is given, which allows checking not only that the allocator
 * returns coherent addresses, but that it **downloads the right amount to the
 * right place** — two things an allocator can get right separately and wrong
 * together. */
typedef struct { unsigned int address, bytes; } transfer;
static transfer g_transfers[4096];
static int      g_transfer_count;
static int      g_refuse_next;

static int fake_download(void *user, int tmu, unsigned int address,
                         const void *data, unsigned int bytes)
{
    (void)user; (void)tmu; (void)data;
    if (g_refuse_next) { g_refuse_next = 0; return 0; }
    if (g_transfer_count < 4096) {
        g_transfers[g_transfer_count].address = address;
        g_transfers[g_transfer_count].bytes   = bytes;
        g_transfer_count++;
    }
    return 1;
}

/* --- The invariants ----------------------------------------------------------- */

/* The live blocks do not overlap and fit inside [base, limit). */
static int no_overlap(const dkr_tmu *t)
{
    int i, j;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        const dkr_tmu_resident *a = &t->resident[i];
        if (!a->live) { continue; }
        if (a->address < t->base) { return 0; }
        if (a->address + a->bytes > t->limit) { return 0; }
        if ((a->address % DKR_TMU_GRANULARITY) != 0u) { return 0; }
        for (j = i + 1; j < DKR_TMU_MAX_RESIDENT; j++) {
            const dkr_tmu_resident *b = &t->resident[j];
            if (!b->live) { continue; }
            if (a->address < b->address + b->bytes &&
                b->address < a->address + a->bytes) {
                return 0;
            }
        }
    }
    return 1;
}

static int live_count(const dkr_tmu *t)
{
    int i, n = 0;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        if (t->resident[i].live) { n++; }
    }
    return n;
}

int main(void)
{
    dkr_tmu t;
    /* The bounds actually measured on the card: see `win95-tmu.md`.
       0x1FFFF8 and not 0x200000 — eight bytes are missing, and that is precisely
       what makes reserving the top of the tree necessary. */
    const unsigned int BASE = 0x00000000u, LIMIT = 0x001FFFF8u;

    g_out = fopen("D:\\TMUTEST.TXT", "w");

    /* --- Elementary allocation ---------------------------------------------- */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        const unsigned int a = dkr_tmu_alloc(&t, 8192u);
        const unsigned int b = dkr_tmu_alloc(&t, 8192u);
        check("two allocations succeed",
              a != DKR_TMU_NONE && b != DKR_TMU_NONE);
        check("and give distinct addresses", a != b);
        check("aligned to the hardware granularity",
              (a % DKR_TMU_GRANULARITY) == 0u && (b % DKR_TMU_GRANULARITY) == 0u);
        check("occupancy follows", dkr_tmu_used(&t) == 16384u);
        dkr_tmu_free(&t, a, 8192u);
        dkr_tmu_free(&t, b, 8192u);
        check("and returns to zero after freeing", dkr_tmu_used(&t) == 0u);
    }

    /* --- The missing eighth byte --------------------------------------------- *
     *
     * The tree covers a full 2 MiB, the card only offers 0x1FFFF8. The 2 MiB head
     * block must therefore **not** be allocatable: it would overrun by eight
     * bytes. The defect would be rare — nearly empty memory, a single enormous
     * texture — hence discovered late. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    check("the whole 2 MiB block is refused: it overruns by 8 bytes",
          dkr_tmu_alloc(&t, 0x200000u) == DKR_TMU_NONE);
    check("but the 1 MiB one passes", dkr_tmu_alloc(&t, 0x100000u) != DKR_TMU_NONE);

    /* --- No external fragmentation ------------------------------------------- *
     *
     * This is the property that justified the buddy. We fill with 64x64s, free
     * every other allocation, then ask for 128x128s again: each must find its
     * place in two coalesced neighbouring 64x64s. A size-class allocator without
     * coalescing would fail here, and this is exactly the level-change
     * scenario. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        unsigned int got[256];
        int i, n = 0, reused = 0;
        for (i = 0; i < 256; i++) {
            got[i] = dkr_tmu_alloc(&t, 8192u);        /* 64x64 at 16 bits */
            if (got[i] != DKR_TMU_NONE) { n++; }
        }
        report("  64x64 placed: %lu out of %lu requested", (unsigned long)n, 256ul);
        /* **255, not 256.** Two MiB hold exactly 256 blocks of 8 KiB, but the
           card only offers 0x1FFFF8: the last eight bytes are missing, and they
           fall inside the last 8 KiB block. That block is therefore split, and no
           longer allocatable whole.
           This suite asserted 256 and was wrong — the allocator was right. The
           same eighth byte has already made an assumption lie further up this
           file; it is worth remembering. */
        check("the TMU holds 255 64x64 textures: the 8 missing bytes "
              "amputate the last block", n == 255);

        /* **What the reserve really costs: 128 bytes, not 8 KiB.**
         *
         * The last 8 KiB block is not lost — it is split. Only the last
         * minimum-size block, the one that actually contains the boundary, is
         * reserved. The rest stays available at a finer grain, and the
         * distinction is far from academic: losing 8 KiB per TMU would be the
         * price of four 32x32 textures. */
        {
            int small = 0;
            while (dkr_tmu_alloc(&t, DKR_TMU_MIN_BLOCK) != DKR_TMU_NONE) {
                small++;
            }
            report("  then %lu blocks of %lu bytes in the tail",
                   (unsigned long)small, (unsigned long)DKR_TMU_MIN_BLOCK);
            check("the last block's tail stays usable at a fine grain: "
                  "the reserve costs 128 bytes, not 8 KiB",
                  small == 63);
        }
        dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
        for (i = 0; i < 256; i++) { got[i] = dkr_tmu_alloc(&t, 8192u); }
        for (i = 0; i < 256; i += 2) {
            if (got[i] != DKR_TMU_NONE) { dkr_tmu_free(&t, got[i], 8192u); }
        }
        for (i = 0; i < 128; i++) {
            if (dkr_tmu_alloc(&t, 8192u) != DKR_TMU_NONE) { reused++; }
        }
        check("the 128 freed blocks are entirely taken back", reused == 128);
    }

    /* Coalescing itself: free *everything*, then ask for the largest block memory
       allows. Without coalescing, the tree would stay in pieces. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        unsigned int got[256];
        int i;
        for (i = 0; i < 256; i++) { got[i] = dkr_tmu_alloc(&t, 8192u); }
        for (i = 0; i < 256; i++) { dkr_tmu_free(&t, got[i], 8192u); }
        check("after 256 frees, a 1 MiB block is possible again",
              dkr_tmu_alloc(&t, 0x100000u) != DKR_TMU_NONE);
    }

    /* --- The cache: hits and misses ------------------------------------------ */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    g_transfer_count = 0;
    {
        static unsigned char data[8192];
        unsigned int a1, a2;
        dkr_tmu_begin_frame(&t);
        a1 = dkr_tmu_acquire(&t, 0x1111ull, data, 8192u);
        a2 = dkr_tmu_acquire(&t, 0x1111ull, data, 8192u);
        check("the same key returns the same address", a1 == a2 && a1 != DKR_TMU_NONE);
        check("and it was downloaded only once", g_transfer_count == 1);
        check("the counters tell hits from misses",
              t.stats.hits == 1 && t.stats.misses == 1);
        check("the transfer carries the right address and the right size",
              g_transfers[0].address == a1 && g_transfers[0].bytes == 8192u);
    }

    /* --- Eviction, and its interesting case ----------------------------------- *
     *
     * The easy case is "evict the oldest". The case that counts is the reverse:
     * **do not evict what the current frame needs**. A frame asking for more than
     * the TMU holds would otherwise evict what it has just downloaded, and we
     * would pay the bus for every texture to display nothing more — the worst
     * possible regime. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    g_transfer_count = 0;
    {
        static unsigned char data[8192];
        int i, placed = 0;
        dkr_tmu_begin_frame(&t);
        /* 300 textures of 8 KiB ask for 2.4 MiB: the TMU only holds 256. All
           being pinned by the current frame, the last 44 must **fail** rather
           than chase out the earlier ones. */
        for (i = 0; i < 300; i++) {
            if (dkr_tmu_acquire(&t, (unsigned long long)i, data, 8192u)
                != DKR_TMU_NONE) {
                placed++;
            }
        }
        report("  placed %lu, failures %lu", (unsigned long)placed,
               t.stats.failures);
        check("the frame saturates cleanly rather than chasing itself out",
              placed == 255 && t.stats.failures == 45);
        check("no eviction during the frame", t.stats.evictions == 0);
        check("the live blocks do not overlap", no_overlap(&t));

        /* Next frame: the pins are lifted, eviction becomes possible again, and
           a new texture finds its place. */
        dkr_tmu_begin_frame(&t);
        check("on the next frame, a new texture gets through",
              dkr_tmu_acquire(&t, 0xDEADull, data, 8192u) != DKR_TMU_NONE);
        check("at the price of one eviction", t.stats.evictions == 1);
        check("and the per-frame counters restarted from zero",
              t.stats.downloads_this_frame == 1);
    }

    /* --- Least recently used --------------------------------------------------- */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        static unsigned char data[0x80000];
        unsigned int a, b, c;
        dkr_tmu_begin_frame(&t);
        a = dkr_tmu_acquire(&t, 1ull, data, 0x80000u);   /* 512 KiB each */
        b = dkr_tmu_acquire(&t, 2ull, data, 0x80000u);
        (void)dkr_tmu_acquire(&t, 3ull, data, 0x80000u);
        dkr_tmu_begin_frame(&t);
        /* We reuse 1, which refreshes its timestamp. 2 becomes the oldest. */
        (void)dkr_tmu_acquire(&t, 1ull, data, 0x80000u);
        c = dkr_tmu_acquire(&t, 4ull, data, 0x80000u);
        check("the fourth texture finds the second one's place",
              c != DKR_TMU_NONE && c == b);
        check("and the first, reused, is still there",
              dkr_tmu_acquire(&t, 1ull, data, 0x80000u) == a);
    }

    /* --- A refused transfer must give the block back -------------------------- *
     *
     * Otherwise memory leaks on every failure, and the next failure arrives
     * sooner than the previous one — a degradation that accelerates and is badly
     * diagnosed. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        static unsigned char data[8192];
        unsigned int before;
        dkr_tmu_begin_frame(&t);
        before = dkr_tmu_used(&t);
        g_refuse_next = 1;
        check("a refused transfer returns DKR_TMU_NONE",
              dkr_tmu_acquire(&t, 7ull, data, 8192u) == DKR_TMU_NONE);
        check("and does not leave the block allocated", dkr_tmu_used(&t) == before);
        check("the failure is counted", t.stats.failures == 1);
    }

    /* --- The level change ------------------------------------------------------ */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        static unsigned char data[8192];
        int i;
        dkr_tmu_begin_frame(&t);
        for (i = 0; i < 100; i++) {
            (void)dkr_tmu_acquire(&t, (unsigned long long)i, data, 8192u);
        }
        check("a hundred textures are resident", live_count(&t) == 100);
        dkr_tmu_reset(&t);
        check("the reset empties the TMU", dkr_tmu_used(&t) == 0u &&
                                           live_count(&t) == 0);
        check("but the cumulative counters survive: they will say in the end "
              "whether the port downloaded during the races",
              t.stats.downloads == 100);
    }

    /* --- The status line, readable in game ------------------------------------ */
    {
        char line[160];
        dkr_tmu_format_status(&t, line, sizeof(line));
        printf("  status: %s\n", line);
        if (g_out) { fprintf(g_out, "  status: %s\n", line); }
        check("the status line is not empty", line[0] != 0);
    }

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
