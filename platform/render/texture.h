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

/* The same, for a tile that is a **window into a wider image**.
 *
 * `src_row_texels` is how far apart two rows of the source are, in texels of
 * this format; `width` stays the number of texels actually wanted from each row.
 * Passing `src_row_texels == width` is exactly `dkr_texture_convert`, which is
 * how that function is now implemented.
 *
 * It exists because the timer digits on this game's vehicle-select screen are
 * two glyphs side by side in a 32-texel-wide buffer, drawn with a 16-texel tile.
 * Read linearly they interleave, which is what "shredded" looked like. See
 * `docs/research/win95-hud-digits.md`. */
int dkr_texture_convert_strided(const unsigned char *rdram,
                                unsigned int rdram_size, int native,
                                unsigned int address,
                                dkr_n64_format format, dkr_n64_size size,
                                int width, int height, int src_row_texels,
                                unsigned short *out, dkr_texture_stats *stats);

/* **The RDP's odd-row swap, for blocks loaded with `dxt == 0`.**
 *
 * The RDP fetches a texel from texture memory at an address it exclusive-ors by
 * a word-swap constant on **odd rows** -- angrylion's `fetch_texel` does
 * `taddr ^= (t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR`. `LoadBlock` normally
 * applies the matching swap as it fills, so the two cancel and the texels sit in
 * memory exactly as they are sampled. **With `dxt == 0` the load does not swap**,
 * and the fetch still does: every odd row comes out with its texels exchanged in
 * pairs.
 *
 * `swap_texels` is that exchange in texels -- the two halves of a 64-bit word,
 * so `4 / bytes-per-texel-in-a-bank`: 2 for 16- and 32-bit texels, 4 for 8-bit,
 * 8 for 4-bit. Zero disables it, which is every texture loaded with a non-zero
 * `dxt`.
 *
 * Measured, not assumed: with `swap_texels == 2` this game's timer digits come
 * out as clean numerals at their declared 16x15, where reading them plainly gives
 * a speckled blob. See `docs/research/win95-hud-digits.md`. */
int dkr_texture_convert_swapped(const unsigned char *rdram,
                                unsigned int rdram_size, int native,
                                unsigned int address,
                                dkr_n64_format format, dkr_n64_size size,
                                int width, int height, int src_row_texels,
                                int swap_texels,
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
