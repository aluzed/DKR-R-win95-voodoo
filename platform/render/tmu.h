/* E05-S02 — allocating the TMU's texture memory.
 *
 * Glide has no texture manager. It exposes the TMU's memory as a raw address
 * space: you pick an address, download to it with `grTexDownloadMipMap`, and
 * bind that address for drawing. Allocation, fragmentation and eviction are
 * entirely ours to write.
 *
 * ## Measurement decided the design
 *
 * Measured on the card (`docs/research/win95-tmu.md`):
 *
 *     addressable space  0x00000000 .. 0x001FFFF8 per TMU, that is 2047 KiB
 *     granularity        8 bytes — a 1x1 texture costs 8 for 2 useful ones
 *     cost               exactly width x height x depth, otherwise
 *
 * And above all: **every texture size is a power of two**. That is not a
 * statistical observation, it is a consequence — the dimensions the (LOD, aspect
 * ratio) pair can express are powers of two, and the depth is one or two bytes.
 *
 * The ticket considered a size-class allocator "if the game's textures fall into
 * a small number of sizes". Measurement gives better than a distribution: it
 * gives a property. A **buddy** allocator is exactly a size-class allocator with
 * coalescing, and on requests that are all powers of two it produces **no
 * external fragmentation** at all — never an unusable hole between two textures.
 *
 * That matters here more than elsewhere. On a 1998 card without paging,
 * fragmentation that refuses a texture does not degrade performance: it makes a
 * piece of scenery go missing.
 *
 * ## What remains assumed, and cannot stay that way for long
 *
 * The size of the smallest block. It is 128 bytes, that is an 8x8 texture at 16
 * bits, because going down to the hardware granularity of 8 bytes would multiply
 * the tracking table by sixteen for textures that probably do not exist. **That
 * "probably" is the only place in this file that does not rest on a
 * measurement**, and it will fall as soon as the ROM allows the real sizes to be
 * measured.
 *
 * ## This module does not talk to Glide
 *
 * The download goes through a function pointer supplied by the caller. That is
 * what allows the allocator and the eviction policy to be tested on the host,
 * exhaustively, with no card — and it is necessary: the missing ROM forbids
 * testing them in the game.
 */
#ifndef DKR_RENDER_TMU_H
#define DKR_RENDER_TMU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Measured hardware granularity. The allocator does not go down to it, but every
   address it returns is a multiple of it — that is what the card requires. */
#define DKR_TMU_GRANULARITY   8u

/* The smallest managed block. See the header: this is the only assumption. */
#define DKR_TMU_MIN_BLOCK     128u

/* The buddy tree covers 2 MiB, the size of a Voodoo 2 TMU. A larger card is
   handled by opening only part of the tree; a smaller one, by reserving the top.
   Both cases go through `dkr_tmu_init`. */
#define DKR_TMU_SPAN          0x200000u
#define DKR_TMU_LEAVES        (DKR_TMU_SPAN / DKR_TMU_MIN_BLOCK)   /* 16384 */
#define DKR_TMU_NODES         (2u * DKR_TMU_LEAVES)                /* 32768 */

/* Returned by `dkr_tmu_alloc` when there is no room. Zero is a perfectly valid
   address on this card — measurement says so, `grTexMinAddress` returns zero —
   so the sentinel cannot be zero. */
#define DKR_TMU_NONE          0xFFFFFFFFu

/* --- The state of a tree node ----------------------------------------------- */
typedef enum {
    DKR_TMU_FREE = 0,     /* free and whole */
    DKR_TMU_SPLIT,        /* split in two: its children carry the information */
    DKR_TMU_USED,         /* allocated whole */
    DKR_TMU_RESERVED      /* outside real memory, never allocatable */
} dkr_tmu_node_state;

/* --- A resident texture ------------------------------------------------------ */
typedef struct {
    unsigned long long key;        /* identity, opaque: we compare, we do not interpret */
    unsigned int       address;
    unsigned int       bytes;
    unsigned long      last_used;  /* logical timestamp, for least-recently-used */
    unsigned char      pinned;     /* protected from eviction for one frame */
    unsigned char      live;
} dkr_tmu_resident;

#define DKR_TMU_MAX_RESIDENT 512

/* --- The counters ------------------------------------------------------------ *
 *
 * The ticket wants them readable in-game, and rightly so: a texture-cache miss
 * does not show up in a log, it is felt on the controller. A download in the
 * middle of a race is a hitch — a 1998 PCI bus takes time to move 64 KiB, and
 * this happens inside a 16 ms frame. */
typedef struct {
    unsigned long hits;            /* already resident */
    unsigned long misses;          /* had to download */
    unsigned long evictions;
    unsigned long downloads;       /* number of calls to the transfer function */
    unsigned long download_bytes;
    unsigned long failures;        /* no room found even after eviction */
    unsigned long downloads_this_frame;
    unsigned long bytes_this_frame;
    unsigned long peak_bytes;      /* peak occupancy reached */
} dkr_tmu_stats;

/* The transfer to the card. Returns non-zero on success.
   `user` is the caller's context; `data` and `bytes` describe the already
   decoded texture — decoding is E04-S07 and does not belong here. */
typedef int (*dkr_tmu_download_fn)(void *user, int tmu, unsigned int address,
                                   const void *data, unsigned int bytes);

typedef struct dkr_tmu {
    int                 index;         /* 0 or 1: which TMU */
    unsigned int        base, limit;   /* the genuinely usable space */
    unsigned char       node[DKR_TMU_NODES];
    unsigned int        used_bytes;

    dkr_tmu_resident    resident[DKR_TMU_MAX_RESIDENT];
    unsigned long       clock;         /* logical timestamp, advances on each use */

    dkr_tmu_download_fn download;
    void               *download_user;

    dkr_tmu_stats       stats;
} dkr_tmu;

/* Prepares the allocator over `[base, limit)`. The addresses come from
   `grTexMinAddress` and `grTexMaxAddress` — they are not assumed, and
   measurement showed that `base` is zero on this card, which rules out using it
   as a sentinel. */
void dkr_tmu_init(dkr_tmu *t, int index, unsigned int base, unsigned int limit,
                  dkr_tmu_download_fn download, void *user);

/* Allocates `bytes` bytes. Returns the address, or `DKR_TMU_NONE`.
   The size is rounded up to the next power of two: this has no effect on
   textures, whose size is already one. */
unsigned int dkr_tmu_alloc(dkr_tmu *t, unsigned int bytes);

/* Returns a block. The address must be the one `alloc` handed out. */
void dkr_tmu_free(dkr_tmu *t, unsigned int address, unsigned int bytes);

/* --- The cache --------------------------------------------------------------- *
 *
 * `dkr_tmu_acquire` is the engine's entry point: "give me this texture's
 * address, downloading it if you must". It counts hits and misses, evicts
 * least-recently-used when there is no room, and returns `DKR_TMU_NONE` if it
 * cannot manage.
 *
 * `bytes` must come from `grTexTextureMemRequired` and not from a computation:
 * measurement showed a rounding case (a 1x1 texture costs 8 bytes for 2 useful
 * ones), and packing according to a computation would make two textures overlap.
 * The symptom would not be an error but a piece of scenery wearing another's
 * pattern. */
unsigned int dkr_tmu_acquire(dkr_tmu *t, unsigned long long key,
                             const void *data, unsigned int bytes);

/* Marks the start of a frame: resets the per-frame counters and lifts every
   pin. */
void dkr_tmu_begin_frame(dkr_tmu *t);

/* Protects a texture from eviction until the end of the frame. Without this, a
   frame asking for more textures than the TMU holds would evict the ones it has
   just downloaded — the worst possible case, where a lot is transferred to
   display nothing more. */
void dkr_tmu_pin(dkr_tmu *t, unsigned long long key);

/* Empties everything: to be called on a level change. */
void dkr_tmu_reset(dkr_tmu *t);

/* Current occupancy, in bytes. */
unsigned int dkr_tmu_used(const dkr_tmu *t);

/* Writes a status line readable on screen (E08-S01). `out` receives at most
   `size` characters, terminator included. */
void dkr_tmu_format_status(const dkr_tmu *t, char *out, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_TMU_H */
