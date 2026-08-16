/* E04-S07 -- probe for the texture format decoder.
 *
 * What this checks is not "the conversion runs" but **the three ways it can
 * mislead**:
 *
 *   - a forgotten bit replication, which makes white nearly-white;
 *   - an unsupported format served anyway, producing arbitrary colours that
 *     pass for rendering;
 *   - an out-of-bounds read, which never fails loudly.
 *
 * Both RDRAM layouts are exercised, as for the display-list decoder: the game
 * supplies the XOR-3 interleaved form, the probes build the plain one, and
 * confusing them gives scrambled textures rather than an error.
 */
#include "render/texture.h"

#include <stdio.h>
#include <string.h>

static int g_fails;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "FAIL ", what);
    if (!condition) { g_fails++; }
}

#define RAM 4096u
static unsigned char g_ram[RAM];
static unsigned char g_ram_x[RAM];
static unsigned short g_out[64 * 64];

static void write16(unsigned int a, unsigned int v)
{
    g_ram[a] = (unsigned char)(v >> 8);
    g_ram[a + 1] = (unsigned char)v;
}

/* librecomp's permutation. `i ^ 3` is an involution, so the same loop serves in
   both directions -- which is also what makes it easy to verify. */
static void interleave(void)
{
    unsigned int i;
    for (i = 0; i < RAM; i++) { g_ram_x[i ^ 3u] = g_ram[i]; }
}

int main(void)
{
    dkr_texture_stats st;
    memset(&st, 0, sizeof(st));

    printf("texture format decoding\n\n");

    /* --- RGBA16: DKR's common case, a copy ---------------------------------- */
    memset(g_ram, 0, sizeof(g_ram));
    write16(0x100u, 0xFFFFu);   /* opaque white */
    write16(0x102u, 0xF801u);   /* opaque pure red */
    write16(0x104u, 0x0001u);   /* opaque black */
    write16(0x106u, 0x07C1u);   /* opaque pure green */
    interleave();

    check("RGBA16 converts",
          dkr_texture_convert(g_ram, RAM, 0, 0x100u, DKR_N64_FMT_RGBA,
                              DKR_N64_SIZ_16, 2, 2, g_out, &st) == 1);
    check("and returns the texels unchanged -- it is already 5551",
          g_out[0] == 0xFFFFu && g_out[1] == 0xF801u &&
          g_out[2] == 0x0001u && g_out[3] == 0x07C1u);

    /* The same texture in the game's layout must give exactly the same thing.
       This is the check that catches a conversion which would "work" on the
       probes and scramble everything on the machine. */
    {
        unsigned short other[4];
        memset(other, 0, sizeof(other));
        check("the interleaved layout converts too",
              dkr_texture_convert(g_ram_x, RAM, 1, 0x100u, DKR_N64_FMT_RGBA,
                                  DKR_N64_SIZ_16, 2, 2, other, &st) == 1);
        check("and gives exactly the same result",
              memcmp(other, g_out, sizeof(other)) == 0);
    }

    /* --- I8: the bit replication -------------------------------------------- *
     *
     * 255 must give plain white. A shift alone would give 0xF7DE instead of
     * 0xFFFF -- close enough to pass by eye, wrong enough that no white is ever
     * white. */
    memset(g_ram, 0, sizeof(g_ram));
    g_ram[0x200] = 0xFF;
    g_ram[0x201] = 0x00;
    interleave();
    check("I8 converts",
          dkr_texture_convert(g_ram, RAM, 0, 0x200u, DKR_N64_FMT_I,
                              DKR_N64_SIZ_8, 2, 1, g_out, &st) == 1);
    check("I8: 255 gives plain white, not approximate white",
          ((g_out[0] >> 11) & 0x1Fu) == 0x1Fu &&
          ((g_out[0] >> 6) & 0x1Fu) == 0x1Fu &&
          ((g_out[0] >> 1) & 0x1Fu) == 0x1Fu);
    check("I8: 0 gives black", (g_out[1] & 0xFFFEu) == 0u);

    /* --- IA16: alpha becomes a threshold ------------------------------------- *
     *
     * Eight alpha bits on the N64, **one** on the Voodoo. The gradient becomes a
     * threshold, and that is an accepted loss -- but it must at least fall on
     * the right side. */
    memset(g_ram, 0, sizeof(g_ram));
    write16(0x300u, 0xFF00u);   /* full intensity, zero alpha */
    write16(0x302u, 0xFFFFu);   /* full intensity, full alpha */
    interleave();
    check("IA16 converts",
          dkr_texture_convert(g_ram, RAM, 0, 0x300u, DKR_N64_FMT_IA,
                              DKR_N64_SIZ_16, 2, 1, g_out, &st) == 1);
    check("IA16: zero alpha becomes transparent, full alpha opaque",
          (g_out[0] & 1u) == 0u && (g_out[1] & 1u) == 1u);

    /* --- What must be refused ------------------------------------------------ *
     *
     * Indexed formats need the palette, loaded by a separate command. Serving
     * them without it would give arbitrary colours -- worse than an absence,
     * since that passes for rendering. */
    {
        const unsigned long before = st.unsupported;
        check("CI8 is refused, for want of a palette",
              dkr_texture_convert(g_ram, RAM, 0, 0x100u, DKR_N64_FMT_CI,
                                  DKR_N64_SIZ_8, 4, 4, g_out, &st) == 0);
        check("and the refusal is counted, otherwise it would be invisible",
              st.unsupported == before + 1u);
    }

    /* Out of bounds: the check that is never seen if you leave it out. */
    {
        const unsigned long before = st.out_of_rdram;
        check("a texture running past RDRAM is refused",
              dkr_texture_convert(g_ram, RAM, 0, RAM - 8u, DKR_N64_FMT_RGBA,
                                  DKR_N64_SIZ_16, 16, 16, g_out, &st) == 0);
        check("and the refusal is counted", st.out_of_rdram == before + 1u);
    }

    /* An absurd dimension out of a bad decode must not overrun the conversion
       buffer, which is 256x256. */
    check("a dimension beyond the buffer is refused",
          dkr_texture_convert(g_ram, RAM, 0, 0x100u, DKR_N64_FMT_RGBA,
                              DKR_N64_SIZ_16, 512, 512, g_out, &st) == 0);

    /* And the byte size, on which the bounds check depends. */
    check("the byte size follows the format",
          dkr_texture_bytes(DKR_N64_SIZ_4, 8, 8) == 32u &&
          dkr_texture_bytes(DKR_N64_SIZ_8, 8, 8) == 64u &&
          dkr_texture_bytes(DKR_N64_SIZ_16, 8, 8) == 128u &&
          dkr_texture_bytes(DKR_N64_SIZ_32, 8, 8) == 256u);

    printf("\n  converted=%lu refused=%lu out-of-rdram=%lu too-large=%lu\n",
           st.converted, st.unsupported, st.out_of_rdram, st.too_large);
    printf("\n%d failure(s)\n", g_fails);
    return g_fails != 0;
}
