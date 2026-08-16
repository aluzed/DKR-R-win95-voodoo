/* E05-S02 — implementation. The contract and the measurements live in `tmu.h`. */
#include "tmu.h"

#include <stdio.h>
#include <string.h>

/* --- The buddy tree ---------------------------------------------------------- *
 *
 * Laid out like a binary heap: node 0 covers the whole space, node `i` has
 * `2i+1` and `2i+2` as children. No pointers, no linked lists — the entire
 * structure fits in a 32 KiB byte array, and an address follows from the index
 * by arithmetic.
 *
 * That is what makes it testable: there is no hidden state to corrupt, and a
 * full walk of the tree checks the invariant in a few lines. */

static unsigned int node_size(unsigned int index)
{
    /* Node 0 covers DKR_TMU_SPAN, its two children half of it, and so on. The
       depth is the position of the high bit of (index+1). */
    unsigned int span = DKR_TMU_SPAN;
    unsigned int i = index + 1u;
    while (i > 1u) { i >>= 1; span >>= 1; }
    return span;
}

static unsigned int node_offset(unsigned int index)
{
    /* The offset is rebuilt on the way up: every time we are the right child,
       we have crossed half of the parent block. */
    unsigned int offset = 0, size = node_size(index), i = index;
    while (i > 0u) {
        if ((i & 1u) == 0u) {          /* right child: i = 2p + 2 */
            offset += size;
        }
        size <<= 1;
        i = (i - 1u) >> 1;
    }
    return offset;
}

static unsigned int round_up_pow2(unsigned int v)
{
    unsigned int p = DKR_TMU_MIN_BLOCK;
    while (p < v) { p <<= 1; }
    return p;
}

/* Marks as unusable everything that reaches past real memory.
 *
 * The measured space runs from 0 to 0x1FFFF8: **eight bytes short** of the TMU
 * being exactly 2 MiB, while the tree covers a full 2 MiB. Without this reserve,
 * the allocator would hand out an address the card does not accept. The defect
 * would be rare — memory would have to be nearly full — hence discovered late,
 * and on a loaded level rather than on a test. */
static void reserve_outside(dkr_tmu *t, unsigned int index)
{
    const unsigned int start = t->base + node_offset(index);
    const unsigned int size  = node_size(index);

    if (start >= t->limit) {
        t->node[index] = DKR_TMU_RESERVED;      /* entirely outside */
        return;
    }
    if (start + size <= t->limit) {
        return;                                  /* entirely inside */
    }
    /* Straddling: split and start again on both halves. */
    if (size <= DKR_TMU_MIN_BLOCK) {
        t->node[index] = DKR_TMU_RESERVED;       /* no more splitting: discard */
        return;
    }
    t->node[index] = DKR_TMU_SPLIT;
    reserve_outside(t, index * 2u + 1u);
    reserve_outside(t, index * 2u + 2u);
}

void dkr_tmu_init(dkr_tmu *t, int index, unsigned int base, unsigned int limit,
                  dkr_tmu_download_fn download, void *user)
{
    if (!t) { return; }
    memset(t, 0, sizeof(*t));
    t->index          = index;
    t->base           = base;
    t->limit          = limit;
    t->download       = download;
    t->download_user  = user;
    reserve_outside(t, 0u);
}

/* Recursive descent: find a free block of `want` bytes, splitting as needed.
   Returns the offset relative to `base`, or DKR_TMU_NONE.
 *
 * The first place found is taken, with no search for the best. On requests that
 * are all powers of two, "the first" and "the best" name the same block: the
 * tree holds no intermediate-sized block where one could do better. */
static unsigned int take(dkr_tmu *t, unsigned int index, unsigned int want)
{
    const unsigned int size = node_size(index);

    if (size < want || t->node[index] == DKR_TMU_RESERVED ||
        t->node[index] == DKR_TMU_USED) {
        return DKR_TMU_NONE;
    }
    if (size == want) {
        if (t->node[index] != DKR_TMU_FREE) {
            return DKR_TMU_NONE;                 /* already split: nothing whole here */
        }
        t->node[index] = DKR_TMU_USED;
        return node_offset(index);
    }
    /* Too large: split, then descend. */
    if (t->node[index] == DKR_TMU_FREE) {
        t->node[index] = DKR_TMU_SPLIT;
    }
    {
        unsigned int r = take(t, index * 2u + 1u, want);
        if (r != DKR_TMU_NONE) { return r; }
        return take(t, index * 2u + 2u, want);
    }
}

unsigned int dkr_tmu_alloc(dkr_tmu *t, unsigned int bytes)
{
    unsigned int want, offset;

    if (!t || bytes == 0u) { return DKR_TMU_NONE; }
    want = round_up_pow2(bytes);
    if (want > DKR_TMU_SPAN) { return DKR_TMU_NONE; }

    offset = take(t, 0u, want);
    if (offset == DKR_TMU_NONE) { return DKR_TMU_NONE; }

    t->used_bytes += want;
    if (t->used_bytes > t->stats.peak_bytes) {
        t->stats.peak_bytes = t->used_bytes;
    }
    return t->base + offset;
}

/* Frees and **coalesces**. Coalescing is what distinguishes the buddy from a
   plain size-class allocator: without it, a level replacing its 64x64 textures
   with 128x128 ones would fail while the room exists, scattered. */
static int give_back(dkr_tmu *t, unsigned int index, unsigned int offset,
                     unsigned int want)
{
    const unsigned int size = node_size(index);

    if (size < want) { return 0; }
    if (size == want) {
        if (t->node[index] != DKR_TMU_USED || node_offset(index) != offset) {
            return 0;
        }
        t->node[index] = DKR_TMU_FREE;
        return 1;
    }
    if (t->node[index] != DKR_TMU_SPLIT) { return 0; }
    {
        const unsigned int left  = index * 2u + 1u;
        const unsigned int right = index * 2u + 2u;
        if (!give_back(t, left, offset, want) &&
            !give_back(t, right, offset, want)) {
            return 0;
        }
        /* Both halves free: the parent becomes a whole block again. */
        if (t->node[left] == DKR_TMU_FREE && t->node[right] == DKR_TMU_FREE) {
            t->node[index] = DKR_TMU_FREE;
        }
        return 1;
    }
}

void dkr_tmu_free(dkr_tmu *t, unsigned int address, unsigned int bytes)
{
    unsigned int want;
    if (!t || bytes == 0u || address < t->base) { return; }
    want = round_up_pow2(bytes);
    if (give_back(t, 0u, address - t->base, want)) {
        t->used_bytes -= (want <= t->used_bytes) ? want : t->used_bytes;
    }
}

unsigned int dkr_tmu_used(const dkr_tmu *t) { return t ? t->used_bytes : 0u; }

/* --- The residency cache ------------------------------------------------------ */

static dkr_tmu_resident *find_resident(dkr_tmu *t, unsigned long long key)
{
    int i;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        if (t->resident[i].live && t->resident[i].key == key) {
            return &t->resident[i];
        }
    }
    return 0;
}

static dkr_tmu_resident *free_slot(dkr_tmu *t)
{
    int i;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        if (!t->resident[i].live) { return &t->resident[i]; }
    }
    return 0;
}

/* Picks the victim: the least recently used, and **not pinned**.
 *
 * Returns NULL if there is nothing to evict, which is not the same thing as
 * "memory is full": it means everything resident serves the current frame.
 * Carrying on evicting would then be harmful — we would endlessly re-download
 * what we need, paying the bus each time to display nothing more. */
static dkr_tmu_resident *pick_victim(dkr_tmu *t)
{
    dkr_tmu_resident *victim = 0;
    int i;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        dkr_tmu_resident *r = &t->resident[i];
        if (!r->live || r->pinned) { continue; }
        if (!victim || r->last_used < victim->last_used) { victim = r; }
    }
    return victim;
}

unsigned int dkr_tmu_acquire(dkr_tmu *t, unsigned long long key,
                             const void *data, unsigned int bytes)
{
    dkr_tmu_resident *r;
    unsigned int address;

    if (!t || bytes == 0u) { return DKR_TMU_NONE; }

    t->clock++;

    r = find_resident(t, key);
    if (r) {
        r->last_used = t->clock;
        r->pinned    = 1;
        t->stats.hits++;
        return r->address;
    }
    t->stats.misses++;

    /* Make room until the allocation goes through. The loop also stops when
       there is no victim left: see `pick_victim`. */
    for (;;) {
        address = dkr_tmu_alloc(t, bytes);
        if (address != DKR_TMU_NONE) { break; }
        {
            dkr_tmu_resident *victim = pick_victim(t);
            if (!victim) {
                t->stats.failures++;
                return DKR_TMU_NONE;
            }
            dkr_tmu_free(t, victim->address, victim->bytes);
            victim->live = 0;
            t->stats.evictions++;
        }
    }

    r = free_slot(t);
    if (!r) {
        /* The table is full while memory is not. Evicting frees an entry just
           as well as a block. */
        dkr_tmu_resident *victim = pick_victim(t);
        if (!victim) {
            dkr_tmu_free(t, address, bytes);
            t->stats.failures++;
            return DKR_TMU_NONE;
        }
        dkr_tmu_free(t, victim->address, victim->bytes);
        victim->live = 0;
        t->stats.evictions++;
        r = victim;
    }

    if (t->download && !t->download(t->download_user, t->index, address,
                                    data, bytes)) {
        dkr_tmu_free(t, address, bytes);
        t->stats.failures++;
        return DKR_TMU_NONE;
    }
    t->stats.downloads++;
    t->stats.download_bytes      += bytes;
    t->stats.downloads_this_frame++;
    t->stats.bytes_this_frame    += bytes;

    r->key       = key;
    r->address   = address;
    r->bytes     = bytes;
    r->last_used = t->clock;
    r->pinned    = 1;
    r->live      = 1;
    return address;
}

void dkr_tmu_begin_frame(dkr_tmu *t)
{
    int i;
    if (!t) { return; }
    t->stats.downloads_this_frame = 0;
    t->stats.bytes_this_frame     = 0;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        t->resident[i].pinned = 0;
    }
}

void dkr_tmu_pin(dkr_tmu *t, unsigned long long key)
{
    dkr_tmu_resident *r;
    if (!t) { return; }
    r = find_resident(t, key);
    if (r) { r->pinned = 1; }
}

void dkr_tmu_reset(dkr_tmu *t)
{
    unsigned int base, limit;
    dkr_tmu_download_fn dl;
    void *user;
    int index;
    dkr_tmu_stats keep;

    if (!t) { return; }
    /* The cumulative counters survive a level change: they are what will say,
       in the end, whether the port downloaded during the races. */
    keep  = t->stats;
    base  = t->base;  limit = t->limit;
    dl    = t->download; user = t->download_user; index = t->index;
    dkr_tmu_init(t, index, base, limit, dl, user);
    t->stats = keep;
    t->stats.downloads_this_frame = 0;
    t->stats.bytes_this_frame     = 0;
}

void dkr_tmu_format_status(const dkr_tmu *t, char *out, unsigned int size)
{
    char line[160];
    unsigned int n;
    if (!out || size == 0u) { return; }
    if (!t) { out[0] = 0; return; }

    sprintf(line, "TMU%d %uK/%uK  hits %lu/%lu  dl %lu (%lu frame)  evict %lu  fail %lu",
            t->index,
            t->used_bytes / 1024u,
            (t->limit - t->base) / 1024u,
            t->stats.hits, t->stats.hits + t->stats.misses,
            t->stats.downloads, t->stats.downloads_this_frame,
            t->stats.evictions, t->stats.failures);
    n = (unsigned int)strlen(line);
    if (n >= size) { n = size - 1u; }
    memcpy(out, line, n);
    out[n] = 0;
}
