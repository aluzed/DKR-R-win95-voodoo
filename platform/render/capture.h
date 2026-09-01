#ifndef DKR_RENDER_CAPTURE_H
#define DKR_RENDER_CAPTURE_H

#include <stddef.h>

/* E09-S02 — capturing a graphics task so that it can be replayed.
 *
 * **Why this exists, and it is not a convenience.** Three times in one week a
 * question about the image could not be answered: whether the constant-register
 * repacking changed the sky, whether the residency cache changed anything, and
 * whether the second texture unit is what made the character names stop
 * doubling. Each time the same wall — the scene animates, two runs do not reach
 * a given display list at the same moment of it, and a pixel comparison across
 * them measures the animation. The last of those was left explicitly
 * unattributed in `win95-game-render.md` for want of this file.
 *
 * A capture is a **frozen input**: the same bytes decoded twice give the same
 * image twice, whatever else changed. That is what makes a difference between
 * two renderings attributable to the renderer instead of to the moment.
 *
 * ## What has to be in it
 *
 * The decoder reads two things: the address the display list starts at, and
 * RDRAM. It reads RDRAM at addresses the list itself computes — vertices,
 * matrices, textures, nested lists — so there is no way to know in advance which
 * bytes matter without running it. The whole image is therefore stored.
 *
 * Eight mebibytes a capture. That is deliberate and it is the cheap side of the
 * trade: a capture that omits a byte the list needs produces a *different* image
 * on replay, which is the one failure this file exists to rule out.
 *
 * ## The format
 *
 * A fixed 32-byte header, then the RDRAM image. Every field little-endian, which
 * is both hosts' order — the capture is written on the Windows 95 machine and
 * read on the development machine, and a format that needed swapping would be a
 * second thing to get wrong.
 *
 * `rdram_native` records whether the image is in librecomp's XOR-3 interleaved
 * layout. It is a property of the *capture*, not of the reader: replaying a
 * native image as though it were plain gives plausible opcodes at absurd
 * addresses, which this project has already spent a day on.
 */

#define DKR_CAPTURE_MAGIC   0x314B5044u   /* "DPK1", little-endian */
#define DKR_CAPTURE_VERSION 1u

typedef struct {
    unsigned int magic;         /* DKR_CAPTURE_MAGIC */
    unsigned int version;       /* DKR_CAPTURE_VERSION */
    unsigned int data_ptr;      /* the display list's start address, masked to 24 bits */
    unsigned int rdram_bytes;   /* how much follows the header */
    unsigned int rdram_native;  /* 1 if XOR-3 interleaved, 0 if plain */
    unsigned int list_index;    /* which display list of the run this was */
    unsigned int screen_w;      /* the resolution it was captured at, so that a */
    unsigned int screen_h;      /* replay reproduces the same viewport */
} dkr_capture_header;

#ifdef __cplusplus
extern "C" {
#endif

/* Writes `path` as header + RDRAM. Returns 1 on success.
 *
 * Failure is reported and not fatal: a capture that cannot be written must not
 * take the run down with it, since the run is usually about something else. */
int dkr_capture_write(const char *path, unsigned int data_ptr,
                      const void *rdram, unsigned int rdram_bytes,
                      int rdram_native, unsigned int list_index,
                      int screen_w, int screen_h);

/* Reads a capture. `rdram` receives a pointer to a buffer this function
   allocates and the caller frees; `out` receives the header. Returns 1 on
   success. A truncated or mismatched file is refused with a message naming which
   check failed -- a capture that reads as valid and is not would replay a
   different list and be blamed on the renderer. */
int dkr_capture_read(const char *path, dkr_capture_header *out,
                     unsigned char **rdram);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_CAPTURE_H */
