/* E04-S07 -- implementation. The contract and the deliberate shortcut are in
 * `texture.h`. */
#include "texture.h"

#include <stdio.h>
#include <string.h>

/* The largest texture the RDP can hold: 4 KiB of texture memory, so 2048 texels
   at 16 bits. We bound generously -- 256x256 -- to catch an absurd dimension
   coming out of a bad decode without refusing real textures. */
#define MAX_SIDE 256

static unsigned char read8(const unsigned char *rdram, int native, unsigned int a)
{
    return rdram[native ? (a ^ 3u) : a];
}

static unsigned short read16(const unsigned char *rdram, int native, unsigned int a)
{
    return (unsigned short)(((unsigned)read8(rdram, native, a) << 8) |
                            read8(rdram, native, a + 1u));
}

/* --- The conversions --------------------------------------------------------
 *
 * All of them produce RGBA5551 with alpha in bit 0, the layout E05-S02 measured
 * on the card.
 *
 * **Alpha is the trap in these formats.** One alpha bit on the Voodoo against
 * eight on the N64 for IA8 and IA16: what was a transparency gradient becomes a
 * threshold. The result is not wrong in the sense of a wrong colour -- it is
 * *hard* where the game wanted soft, and that shows mostly on shadows and
 * haloes. The threshold sits at midpoint for want of a better choice, and it is
 * flagged here rather than discovered. */
static unsigned short grey_to_5551(unsigned int i, unsigned int alpha)
{
    const unsigned int c = i >> 3;   /* 8 bits down to 5 */
    return (unsigned short)((c << 11) | (c << 6) | (c << 1) | (alpha ? 1u : 0u));
}

unsigned int dkr_texture_bytes(dkr_n64_size size, int width, int height)
{
    const unsigned int n = (unsigned int)width * (unsigned int)height;
    if (width <= 0 || height <= 0) {
        return 0u;
    }
    switch (size) {
    case DKR_N64_SIZ_4:  return (n + 1u) / 2u;
    case DKR_N64_SIZ_8:  return n;
    case DKR_N64_SIZ_16: return n * 2u;
    case DKR_N64_SIZ_32: return n * 4u;
    default:             return 0u;
    }
}

const char *dkr_texture_format_name(dkr_n64_format format, dkr_n64_size size)
{
    static const char *bits[4] = { "4", "8", "16", "32" };
    static char name[16];
    const char *f;
    switch (format) {
    case DKR_N64_FMT_RGBA: f = "RGBA"; break;
    case DKR_N64_FMT_YUV:  f = "YUV";  break;
    case DKR_N64_FMT_CI:   f = "CI";   break;
    case DKR_N64_FMT_IA:   f = "IA";   break;
    case DKR_N64_FMT_I:    f = "I";    break;
    default:               f = "?";    break;
    }
    sprintf(name, "%s%s", f, bits[(int)size & 3]);
    return name;
}

int dkr_texture_convert(const unsigned char *rdram, unsigned int rdram_size,
                        int native, unsigned int address,
                        dkr_n64_format format, dkr_n64_size size,
                        int width, int height,
                        unsigned short *out, dkr_texture_stats *stats)
{
    const unsigned int bytes = dkr_texture_bytes(size, width, height);
    unsigned int i, n;

    if (!rdram || !out || bytes == 0u) {
        return 0;
    }
    if (width > MAX_SIDE || height > MAX_SIDE) {
        if (stats) { stats->too_large++; }
        return 0;
    }
    /* The bound is checked here and once, rather than per texel: the same choice
       as in the display-list decoder, and for the same reason -- one place to
       re-read to be sure nothing leaves RDRAM. */
    if ((unsigned long long)address + bytes > (unsigned long long)rdram_size) {
        if (stats) { stats->out_of_rdram++; }
        return 0;
    }

    n = (unsigned int)width * (unsigned int)height;

    if (format == DKR_N64_FMT_RGBA && size == DKR_N64_SIZ_16) {
        /* DKR's common case, and the only one that is a copy: the RDP already
           stores 5551 with alpha in bit 0. Nothing to convert, only to put back
           into the host's byte order. */
        for (i = 0; i < n; i++) {
            out[i] = read16(rdram, native, address + i * 2u);
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    if (format == DKR_N64_FMT_RGBA && size == DKR_N64_SIZ_32) {
        for (i = 0; i < n; i++) {
            const unsigned int a = address + i * 4u;
            const unsigned int r = read8(rdram, native, a) >> 3;
            const unsigned int g = read8(rdram, native, a + 1u) >> 3;
            const unsigned int b = read8(rdram, native, a + 2u) >> 3;
            const unsigned int al = read8(rdram, native, a + 3u);
            out[i] = (unsigned short)((r << 11) | (g << 6) | (b << 1) |
                                      (al >= 128u ? 1u : 0u));
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    if (format == DKR_N64_FMT_I && size == DKR_N64_SIZ_8) {
        for (i = 0; i < n; i++) {
            out[i] = grey_to_5551(read8(rdram, native, address + i), 1u);
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    if (format == DKR_N64_FMT_I && size == DKR_N64_SIZ_4) {
        for (i = 0; i < n; i++) {
            const unsigned char o = read8(rdram, native, address + i / 2u);
            const unsigned int  q = (i & 1u) ? (o & 0x0Fu) : (unsigned int)(o >> 4);
            /* 4 bits to 8 by replication: 15 must give 255, not 240. */
            out[i] = grey_to_5551((q << 4) | q, 1u);
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    if (format == DKR_N64_FMT_IA && size == DKR_N64_SIZ_16) {
        for (i = 0; i < n; i++) {
            const unsigned short m = read16(rdram, native, address + i * 2u);
            out[i] = grey_to_5551((m >> 8) & 0xFFu, (m & 0xFFu) >= 128u);
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    if (format == DKR_N64_FMT_IA && size == DKR_N64_SIZ_8) {
        for (i = 0; i < n; i++) {
            const unsigned char o = read8(rdram, native, address + i);
            const unsigned int  it = (unsigned int)(o >> 4);
            out[i] = grey_to_5551((it << 4) | it, (o & 0x0Fu) >= 8u);
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    if (format == DKR_N64_FMT_IA && size == DKR_N64_SIZ_4) {
        for (i = 0; i < n; i++) {
            const unsigned char o = read8(rdram, native, address + i / 2u);
            const unsigned int  q = (i & 1u) ? (o & 0x0Fu) : (unsigned int)(o >> 4);
            const unsigned int  it = q >> 1;   /* three bits of intensity */
            const unsigned int  it8 = (it << 5) | (it << 2) | (it >> 1);
            out[i] = grey_to_5551(it8, q & 1u);
        }
        if (stats) { stats->converted++; }
        return 1;
    }

    /* CI4, CI8 and YUV stay out. The indexed formats need the palette, which the
       RDP loads through `LOADTLUT` into the other half of texture memory;
       serving them without it would give arbitrary colours, which is worse than
       an absence because it passes for rendering. We refuse, we count, and the
       caller does not draw rather than drawing wrong. */
    if (stats) { stats->unsupported++; }
    return 0;
}
