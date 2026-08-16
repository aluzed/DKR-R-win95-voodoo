/* What the TMU really demands - measured before writing the allocator.
 *
 * Ticket E05-S02 asks to measure before designing. The measurement it aims at
 * bears on the game - number of textures per level, reuse pattern - and assumes
 * the ROM. This one bears on **the hardware**, does not assume it, and governs
 * the allocator just as much: the TMU's alignment and granularity decide the
 * fragmentation, and assuming is particularly risky here.
 *
 * ## Why these are not constants one can read in a book
 *
 * Glide has no texture manager. It exposes the TMU's memory as a raw address
 * space: the application picks an address, downloads there, and binds that
 * address for drawing. `grTexTextureMemRequired` says how many bytes a texture
 * occupies - and that number includes the rounding the card imposes, which is not
 * deducible from the width and the height.
 *
 * An allocator content with `width x height x 2` would stack the textures too
 * tightly. The symptom would not be an error: it would be one texture overwriting
 * another, hence a piece of scenery carrying another's pattern, in a place that
 * depends on the load order. That is to say, the kind of defect one chases for
 * days.
 *
 * ## This witness also validates its own constants
 *
 * As with the states (`win95-glide-states.md`), there is no `glide.h` on this
 * machine and the enumerations are written from memory. They are checkable here
 * without extra hardware: for a 16-bit texture without mipmaps, the expected size
 * is known analytically. If `GR_LOD_*` or `GR_ASPECT_*` were wrong, the returned
 * size would say so immediately.
 */
#include "render/glide.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) { g_fails++; }
}

/* --- Glide 2.x's texture enumerations ---------------------------------------- */

/* The level of detail names the **largest dimension**, and decreases: 256 is
   zero, 1 is eight. That is the reverse of intuition and the first thing to
   check. */
#define GR_LOD_256   0
#define GR_LOD_128   1
#define GR_LOD_64    2
#define GR_LOD_32    3
#define GR_LOD_16    4
#define GR_LOD_8     5
#define GR_LOD_4     6
#define GR_LOD_2     7
#define GR_LOD_1     8

#define GR_ASPECT_8x1  0
#define GR_ASPECT_4x1  1
#define GR_ASPECT_2x1  2
#define GR_ASPECT_1x1  3
#define GR_ASPECT_1x2  4
#define GR_ASPECT_1x4  5
#define GR_ASPECT_1x8  6

#define GR_TEXFMT_RGB_565    0x0A
#define GR_TEXFMT_ARGB_1555  0x0B
#define GR_TEXFMT_ARGB_4444  0x0C
#define GR_TEXFMT_ALPHA_8    0x02
#define GR_TEXFMT_P_8        0x05

#define GR_MIPMAPLEVELMASK_BOTH  0x03
#define GR_TMU0  0
#define GR_TMU1  1

typedef struct {
    int   smallLod;
    int   largeLod;
    int   aspectRatio;
    int   format;
    void *data;
} GrTexInfo;

typedef unsigned int (__stdcall *pfn_min_max)(int tmu);
typedef unsigned int (__stdcall *pfn_required)(unsigned evenOdd, GrTexInfo *info);
typedef void         (__stdcall *pfn_download)(int tmu, unsigned start,
                                               unsigned evenOdd, GrTexInfo *info);

/* The analytical size of a 16-bit texture, from the (lod, aspect) pair. This is
   what the allocator *would believe* if nobody asked the card. */
static void dims_of(int lod, int aspect, int *w, int *h)
{
    const int big = 256 >> lod;
    switch (aspect) {
    case GR_ASPECT_8x1: *w = big; *h = big / 8; break;
    case GR_ASPECT_4x1: *w = big; *h = big / 4; break;
    case GR_ASPECT_2x1: *w = big; *h = big / 2; break;
    case GR_ASPECT_1x1: *w = big; *h = big;     break;
    case GR_ASPECT_1x2: *w = big / 2; *h = big; break;
    case GR_ASPECT_1x4: *w = big / 4; *h = big; break;
    default:            *w = big / 8; *h = big; break;
    }
    if (*w < 1) { *w = 1; }
    if (*h < 1) { *h = 1; }
}

int main(void)
{
    dkr_glide_hardware   hw;
    dkr_glide_context    ctx;
    pfn_min_max  tex_min, tex_max;
    pfn_required required;
    pfn_download download;

    g_out = fopen("D:\\TMU.TXT", "w");
    say("what the TMU demands, measured on the card\n\n");

    if (dkr_glide_detect(&hw) != DKR_GLIDE_OK) {
        say("FAILED: no board\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    say("  TMUs present     : %d\n", hw.tmu_count);
    say("  memory per TMU   : %u KB, %u KB\n",
        hw.tmu_memory_kb[0], hw.tmu_memory_kb[1]);

    if (dkr_glide_open(DKR_GLIDE_RES_640x480, &ctx) != DKR_GLIDE_OK) {
        say("FAILED: context refused\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }

    tex_min  = (pfn_min_max)  dkr_glide_symbol("_grTexMinAddress@4");
    tex_max  = (pfn_min_max)  dkr_glide_symbol("_grTexMaxAddress@4");
    required = (pfn_required) dkr_glide_symbol("_grTexTextureMemRequired@8");
    download = (pfn_download) dkr_glide_symbol("_grTexDownloadMipMap@16");

    say("\n-- the symbols --\n");
    check("grTexMinAddress",        tex_min  != 0);
    check("grTexMaxAddress",        tex_max  != 0);
    check("grTexTextureMemRequired",required != 0);
    check("grTexDownloadMipMap",    download != 0);

    /* --- The addressable space ----------------------------------------------- *
     *
     * It is not "zero to two megabytes". Glide reserves, and the minimum address
     * is not necessarily zero. An allocator starting from zero would overwrite
     * what the library put there. */
    if (tex_min && tex_max) {
        int t;
        say("\n-- the addressable space, per TMU --\n");
        for (t = 0; t < hw.tmu_count && t < 3; t++) {
            const unsigned lo = tex_min(t);
            const unsigned hi = tex_max(t);
            say("  TMU %d: from 0x%08X to 0x%08X, that is %u KB usable\n",
                t, lo, hi, (hi - lo) / 1024u);
        }
        check("TMU 0's space is not empty", tex_max(0) > tex_min(0));
        if (hw.tmu_count > 1) {
            check("both TMUs expose the same space",
                  tex_min(0) == tex_min(1) && tex_max(0) == tex_max(1));
        }
    }

    /* --- What a texture really costs ----------------------------------------- */
    if (required) {
        static const struct { int lod, aspect; const char *name; } CASES[] = {
            { GR_LOD_256, GR_ASPECT_1x1, "256x256" },
            { GR_LOD_128, GR_ASPECT_1x1, "128x128" },
            { GR_LOD_64,  GR_ASPECT_1x1, "64x64"   },
            { GR_LOD_32,  GR_ASPECT_1x1, "32x32"   },
            { GR_LOD_16,  GR_ASPECT_1x1, "16x16"   },
            { GR_LOD_8,   GR_ASPECT_1x1, "8x8"     },
            { GR_LOD_4,   GR_ASPECT_1x1, "4x4"     },
            { GR_LOD_2,   GR_ASPECT_1x1, "2x2"     },
            { GR_LOD_1,   GR_ASPECT_1x1, "1x1"     },
            { GR_LOD_64,  GR_ASPECT_2x1, "64x32"   },
            { GR_LOD_64,  GR_ASPECT_4x1, "64x16"   },
            { GR_LOD_64,  GR_ASPECT_8x1, "64x8"    },
            { GR_LOD_64,  GR_ASPECT_1x2, "32x64"   },
            { GR_LOD_64,  GR_ASPECT_1x8, "8x64"    },
            { GR_LOD_32,  GR_ASPECT_2x1, "32x16"   },
        };
        const int n = (int)(sizeof(CASES) / sizeof(CASES[0]));
        int i, exact = 0, rounded = 0;

        say("\n-- what a 16-bit texture costs in TMU memory --\n");
        say("  %-9s %8s %8s %s\n", "size", "computed", "card", "");
        for (i = 0; i < n; i++) {
            GrTexInfo info;
            int w = 0, h = 0;
            unsigned got, want;

            dims_of(CASES[i].lod, CASES[i].aspect, &w, &h);
            want = (unsigned)(w * h * 2);

            memset(&info, 0, sizeof(info));
            info.smallLod    = CASES[i].lod;
            info.largeLod    = CASES[i].lod;    /* no mipmap: a single level */
            info.aspectRatio = CASES[i].aspect;
            info.format      = GR_TEXFMT_RGB_565;
            info.data        = 0;

            got = required(GR_MIPMAPLEVELMASK_BOTH, &info);
            say("  %-9s %8u %8u %s\n", CASES[i].name, want, got,
                (got == want) ? "" : (got > want ? "<-- rounded up" : "<-- SMALLER ?!"));
            if (got == want)      { exact++; }
            else if (got > want)  { rounded++; }
        }
        say("\n  exact: %d, rounded up: %d, out of %d\n", exact, rounded, n);

        /* If everything is exact, the allocator can pack as tightly as possible.
           Otherwise it must ask the card for the size of *every* texture - which
           the allocator will do anyway, but it is useful to know whether we are
           wasting space. */
        check("no returned size is smaller than the computation",
              exact + rounded == n);

        /* --- The address granularity ----------------------------------------- *
         *
         * Two consecutive textures are placed at `addr += required(...)`. What
         * remains is whether the address itself must be aligned. We deduce it from
         * the smallest returned cost: if it is 8 for a 1x1 texture in 16 bits -
         * that is, 2 useful bytes - the alignment is 8. */
        {
            GrTexInfo info;
            unsigned smallest;
            memset(&info, 0, sizeof(info));
            info.smallLod = info.largeLod = GR_LOD_1;
            info.aspectRatio = GR_ASPECT_1x1;
            info.format = GR_TEXFMT_RGB_565;
            smallest = required(GR_MIPMAPLEVELMASK_BOTH, &info);
            say("\n  smallest possible allocation: %u bytes\n", smallest);
            say("  (a 1x1 texture in 16 bits occupies only 2 useful bytes)\n");
        }
    }

    /* --- The formats ---------------------------------------------------------- *
     * An 8-bit format must cost half as much. If it does not, the enumeration
     * value is wrong - and a wrong format value causes no error, it produces an
     * unreadable texture. */
    if (required) {
        static const struct { int fmt; const char *name; int bpp; } FMT[] = {
            { GR_TEXFMT_RGB_565,   "RGB 565",   16 },
            { GR_TEXFMT_ARGB_1555, "ARGB 1555", 16 },
            { GR_TEXFMT_ARGB_4444, "ARGB 4444", 16 },
            { GR_TEXFMT_ALPHA_8,   "ALPHA 8",    8 },
            { GR_TEXFMT_P_8,       "palettised 8", 8 },
        };
        int i, consistent = 0;
        say("\n-- the formats, on a 64x64 texture --\n");
        for (i = 0; i < 5; i++) {
            GrTexInfo info;
            unsigned got;
            const unsigned want = (unsigned)(64 * 64 * FMT[i].bpp / 8);
            memset(&info, 0, sizeof(info));
            info.smallLod = info.largeLod = GR_LOD_64;
            info.aspectRatio = GR_ASPECT_1x1;
            info.format = FMT[i].fmt;
            got = required(GR_MIPMAPLEVELMASK_BOTH, &info);
            say("  %-12s %2d bits: %6u bytes (computed %6u) %s\n",
                FMT[i].name, FMT[i].bpp, got, want, (got == want) ? "" : "<-- differs");
            if (got == want) { consistent++; }
        }
        check("the five formats cost what their depth announces",
              consistent == 5);
    }

    /* --- A real download ------------------------------------------------------ *
     *
     * The measurement is only worth something if one can actually write into that
     * space. We download a recognisable texture; E05-S02 will then verify that it
     * appears on screen, which reading the buffer back makes possible. */
    if (download && tex_min) {
        static unsigned short checker[64 * 64];
        GrTexInfo info;
        int x, y;
        for (y = 0; y < 64; y++) {
            for (x = 0; x < 64; x++) {
                checker[y * 64 + x] = (unsigned short)
                    (((x / 8 + y / 8) & 1) ? 0xF800 : 0x001F);
            }
        }
        memset(&info, 0, sizeof(info));
        info.smallLod = info.largeLod = GR_LOD_64;
        info.aspectRatio = GR_ASPECT_1x1;
        info.format = GR_TEXFMT_RGB_565;
        info.data = checker;
        download(GR_TMU0, tex_min(0), GR_MIPMAPLEVELMASK_BOTH, &info);
        say("\n  download of a 64x64 checkerboard at 0x%08X: returned\n",
            tex_min(0));
        /* Glide returns nothing: the absence of a crash is all one gets here, and
           that is why the real verification is visual. */
    }

    dkr_glide_shutdown();
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
