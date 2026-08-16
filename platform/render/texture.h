/* E04-S07 -- decoding the N64 texture formats.
 *
 * The RDP samples eight combinations of format and size. The Voodoo knows only
 * one that concerns us: **RGBA5551**, its native format, measured in E05-S02.
 * All this module does is bring the eight down to that one -- once, at load
 * time, and not per texel at draw time, which a Pentium II would not forgive.
 *
 * ## What makes the problem tractable, and what does not
 *
 * Tractable: DKR mostly uses RGBA16, which is **already 5551**. Conversion there
 * is a copy, byte order aside.
 *
 * Less tractable: the indexed formats (CI4, CI8) need the palette, which the RDP
 * loads through a separate command (`LOADTLUT`) into the other half of texture
 * memory. A decoder that ignored them would produce uniformly black surfaces
 * rather than an error -- hence the count of unsupported formats, which the
 * caller is meant to look at.
 *
 * ## The shortcut, taken deliberately: read RDRAM instead of emulating TMEM
 *
 * The RDP does not draw from RDRAM. It first copies into its 4 KiB of texture
 * memory through `LOADBLOCK` or `LOADTILE`, and samples from there. Emulating
 * that memory faithfully -- with its odd-line word interleave -- is a piece of
 * work in itself.
 *
 * This module reads **straight from RDRAM**, at the `SETTIMG` address, with the
 * dimensions from `SETTILESIZE`. That is exact as long as a texture is loaded in
 * one block and drawn whole, which is the common case, and wrong for atlases
 * where only a tile is loaded. The shortcut is named here rather than discovered
 * later on a shifted texture.
 */
#ifndef DKR_RENDER_TEXTURE_H
#define DKR_RENDER_TEXTURE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The RDP formats, values of `G_IM_FMT_*`. */
typedef enum {
    DKR_N64_FMT_RGBA = 0,
    DKR_N64_FMT_YUV,
    DKR_N64_FMT_CI,
    DKR_N64_FMT_IA,
    DKR_N64_FMT_I
} dkr_n64_format;

/* The sizes, values of `G_IM_SIZ_*`: 4, 8, 16 and 32 bits per texel. */
typedef enum {
    DKR_N64_SIZ_4 = 0,
    DKR_N64_SIZ_8,
    DKR_N64_SIZ_16,
    DKR_N64_SIZ_32
} dkr_n64_size;

/* What the decoder ran into. The counters exist because an unsupported format
   does not show: it produces a black surface, not an error. */
typedef struct {
    unsigned long converted;
    unsigned long unsupported;
    unsigned long too_large;
    unsigned long out_of_rdram;
} dkr_texture_stats;

/* Converts a texture from RDRAM to RGBA5551.
 *
 * `rdram` is the snapshot, `rdram_size` its size, `native` selects librecomp's
 * XOR-3 interleaved layout (see `f3ddkr.h`). `out` receives `width * height`
 * halfwords.
 *
 * Returns 1 on success, 0 otherwise -- and in that case the caller must not draw
 * with it, rather than drawing black.
 */
int dkr_texture_convert(const unsigned char *rdram, unsigned int rdram_size,
                        int native, unsigned int address,
                        dkr_n64_format format, dkr_n64_size size,
                        int width, int height,
                        unsigned short *out, dkr_texture_stats *stats);

/* The size in bytes of a texture of these dimensions in this format. Returns 0
   if the combination makes no sense. */
unsigned int dkr_texture_bytes(dkr_n64_size size, int width, int height);

/* The format's name, for logs. An unsupported format has to be nameable in the
   report, otherwise "unsupported: 1240" points at nothing. */
const char *dkr_texture_format_name(dkr_n64_format format, dkr_n64_size size);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_TEXTURE_H */
