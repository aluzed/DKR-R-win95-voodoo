/* E05-S01 — implementation. The contract and the traps live in `glide.h`. */
#include "glide.h"

#include <windows.h>
#include <string.h>

#include "win95/startup.h"

/* --- The Glide 2.x constants ---------------------------------------------- *
 *
 * Copied from 3dfx's `glide.h` rather than included: the target does not have
 * the SDK, and a handful of constants beats one more dependency. Each one is
 * verified by the E09-S01 demonstration, which opened a context and drew a
 * triangle with these values.
 */
typedef unsigned int  FxU32;
typedef unsigned char FxU8;
typedef int           FxBool;

#define GR_RESOLUTION_320x240   0x0
#define GR_RESOLUTION_400x300   0x4
#define GR_RESOLUTION_512x384   0x6
#define GR_RESOLUTION_640x480   0x7
#define GR_REFRESH_60Hz         0x0
#define GR_COLORFORMAT_ARGB     0x0
#define GR_ORIGIN_UPPER_LEFT    0x0
#define GR_BUFFER_BACKBUFFER    0x1
#define GR_WDEPTHVALUE_FARTHEST 0xFFFF

/* Glide 2.x's `GrVertex`. The order is not intuitive — `ooz` and `a` sit between
   the colours and `oow` — and a "logical" layout compiles perfectly while
   rendering permuted colours: Glide reads the floats at the wrong offsets,
   without the slightest error. Verified on screen by the E09-S01 demonstration,
   where a red vertex came out green. */
typedef struct {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    float tmuvtx[3 * 4];
} GrVertex;

typedef FxU32  (WINAPI *pfn_grGlideInit)(void);
typedef void   (WINAPI *pfn_grGlideShutdown)(void);
typedef FxBool (WINAPI *pfn_grSstQueryHardware)(void *);
typedef void   (WINAPI *pfn_grSstSelect)(int);
typedef FxBool (WINAPI *pfn_grSstWinOpen)(FxU32, int, int, int, int, int, int);
typedef void   (WINAPI *pfn_grSstWinClose)(void);
typedef void   (WINAPI *pfn_grBufferClear)(FxU32, FxU8, FxU32);
typedef void   (WINAPI *pfn_grBufferSwap)(int);
typedef void   (WINAPI *pfn_grDrawTriangle)(const void *, const void *, const void *);
typedef void   (WINAPI *pfn_grGlideGetVersion)(char *);
typedef FxBool (WINAPI *pfn_grLfbLock)(FxU32, FxU32, FxU32, FxU32, FxU32, void *);
typedef FxBool (WINAPI *pfn_grLfbUnlock)(FxU32, FxU32);
typedef void   (WINAPI *pfn_grSstIdle)(void);

/* Glide 2.x's `GrLfbInfo_t`. The `size` field must be filled in before the call:
   Glide uses it to know which version of the structure it is being handed, and
   leaving it at zero makes the lock fail with no further explanation. */
typedef struct {
    int    size;
    void  *lfbPtr;
    FxU32  strideInBytes;
    FxU32  writeMode;
    FxU32  origin;
} GrLfbInfo_t;

#define GR_LFB_READ_ONLY        0x00
#define GR_BUFFER_FRONTBUFFER   0x0
#define GR_LFBWRITEMODE_ANY     0xFF

static struct {
    HMODULE dll;
    pfn_grGlideInit        init;
    pfn_grGlideShutdown    shutdown;
    pfn_grSstQueryHardware query;
    pfn_grSstSelect        select;
    pfn_grSstWinOpen       win_open;
    pfn_grSstWinClose      win_close;
    pfn_grBufferClear      clear;
    pfn_grBufferSwap       swap;
    pfn_grDrawTriangle     triangle;
    pfn_grGlideGetVersion  version;
    pfn_grLfbLock          lfb_lock;
    pfn_grLfbUnlock        lfb_unlock;
    pfn_grSstIdle          idle;

    int  initialised;   /* grGlideInit called */
    int  context_open;  /* grSstWinOpen succeeded */
    int  hardware_known;
    dkr_glide_hardware hw;
    dkr_glide_context ctx;
} g;

const char *dkr_glide_result_text(dkr_glide_result r)
{
    switch (r) {
    case DKR_GLIDE_OK:             return "success";
    case DKR_GLIDE_ERR_NO_LIBRARY: return "glide2x.dll not found - 3dfx driver missing?";
    case DKR_GLIDE_ERR_NO_SYMBOL:  return "glide2x.dll incomplete - unexpected version";
    /* This text follows a message box the player has just dismissed, and it must
       tie back to it explicitly - otherwise they will read two problems where
       there is only one. See `docs/research/win95-glide-no-card.md`: the box
       comes from glide2x.dll itself, at load time, and nothing lets us get in
       ahead of it. */
    case DKR_GLIDE_ERR_NO_BOARD:
        return "no 3dfx card detected - which is what the glide2x.dll message "
               "said as well";
    case DKR_GLIDE_ERR_NO_MEMORY:  return "the card does not have enough frame-buffer memory";
    default:                       return "context open refused";
    }
}

/* --- Resolutions ----------------------------------------------------------- */

static const struct { int glide_id, w, h; } RESOLUTIONS[DKR_GLIDE_RES_COUNT] = {
    { GR_RESOLUTION_640x480, 640, 480 },
    { GR_RESOLUTION_512x384, 512, 384 },
    { GR_RESOLUTION_400x300, 400, 300 },
    { GR_RESOLUTION_320x240, 320, 240 },
};

/* What a resolution costs in frame-buffer memory.
 *
 * Two 16-bit colour buffers plus a 16-bit depth buffer: three surfaces of
 * `w * h * 2` bytes. That is ADR 0002's calculation, which rules out triple
 * buffering for this reason — 2.34 MiB against 2 MB on the 8 MB Voodoo 2. */
static unsigned fb_cost_kb(int w, int h)
{
    return (unsigned)(((long)w * h * 2 * 3) / 1024);
}

/* --- Detection ------------------------------------------------------------- */

static void unload(void)
{
    if (g.dll) {
        FreeLibrary(g.dll);
    }
    memset(&g, 0, sizeof(g));
}

static void *sym(const char *decorated)
{
    return (void *)GetProcAddress(g.dll, decorated);
}

dkr_glide_result dkr_glide_detect(dkr_glide_hardware *out)
{
    /* The structure `grSstQueryHardware` returns is read by offsets rather than
       through a declaration: its exact layout varies between Glide versions, and
       only the first fields interest us. The buffer is generously sized — Glide
       writes the configuration of every possible card, and a buffer that was too
       short would be overrun in silence. */
    FxU32 hw[128];
    char  version_text[80];

    /* Idempotent, and it took the machine to learn that.
     *
     * The first version unloaded and reloaded `glide2x.dll` on every call. The
     * witness called `detect` then `open`, which detected again: the second
     * `grGlideInit` met a library that still held the card, and Glide refused
     * with **"Mutual exclusion prohibits this"** — a message that does not name
     * its cause.
     *
     * A detection with side effects is not a detection. This one simply returns
     * what it already knows. */
    if (g.hardware_known) {
        if (out) { *out = g.hw; }
        return DKR_GLIDE_OK;
    }
    if (g.dll) {
        unload();
    }
    memset(&g, 0, sizeof(g));

    g.dll = LoadLibraryA("glide2x.dll");
    if (!g.dll) {
        return DKR_GLIDE_ERR_NO_LIBRARY;
    }

    /* The names carry the full stdcall decoration: `glide2x.dll` exports
       `_grGlideInit@0` and not `grGlideInit`. */
    g.init      = (pfn_grGlideInit)        sym("_grGlideInit@0");
    g.shutdown  = (pfn_grGlideShutdown)    sym("_grGlideShutdown@0");
    g.query     = (pfn_grSstQueryHardware) sym("_grSstQueryHardware@4");
    g.select    = (pfn_grSstSelect)        sym("_grSstSelect@4");
    g.win_open  = (pfn_grSstWinOpen)       sym("_grSstWinOpen@28");
    g.win_close = (pfn_grSstWinClose)      sym("_grSstWinClose@0");
    g.clear     = (pfn_grBufferClear)      sym("_grBufferClear@12");
    g.swap      = (pfn_grBufferSwap)       sym("_grBufferSwap@4");
    g.triangle  = (pfn_grDrawTriangle)     sym("_grDrawTriangle@12");
    g.version   = (pfn_grGlideGetVersion)  sym("_grGlideGetVersion@4");
    /* Read-back is optional: a Glide that does not export it stays usable for
       drawing, only comparison becomes impossible. It therefore does not go into
       the list of mandatory symbols. */
    g.lfb_lock   = (pfn_grLfbLock)   sym("_grLfbLock@24");
    g.lfb_unlock = (pfn_grLfbUnlock) sym("_grLfbUnlock@8");
    /* Needed before any read-back: see `dkr_glide_read_framebuffer`. Optional,
       like the lfb pair — a Glide without it still renders, it just cannot be
       measured reliably. */
    g.idle       = (pfn_grSstIdle)   sym("_grSstIdle@0");

    if (!g.init || !g.shutdown || !g.query || !g.select || !g.win_open ||
        !g.win_close || !g.clear || !g.swap || !g.triangle) {
        unload();
        return DKR_GLIDE_ERR_NO_SYMBOL;
    }

    g.init();
    g.initialised = 1;

    memset(hw, 0, sizeof(hw));
    if (!g.query(hw)) {
        g.shutdown();
        unload();
        return DKR_GLIDE_ERR_NO_BOARD;
    }

    {
        dkr_glide_hardware *const info = &g.hw;
        int i;
        memset(info, 0, sizeof(*info));
        /* Layout of `GrHwConfiguration`: num_sst, then for card 0 type, fbRam,
           fbiRev, nTexelfx, sliDetect, then (tmuRev, tmuRam) per TMU. The memory
           figures are in megabytes. */
        info->board_count  = (int)hw[0];
        info->tmu_count    = (int)hw[4];
        info->fb_memory_kb = hw[2] * 1024u;
        info->sli          = (int)hw[5];
        for (i = 0; i < 3 && i < info->tmu_count; i++) {
            info->tmu_memory_kb[i] = hw[7 + (unsigned)i * 2] * 1024u;
        }
        if (g.version) {
            /* "Glide 2.54" -> 0x254, without depending on the exact shape of
               the text: we take the digits and the dot. */
            const char *p;
            memset(version_text, 0, sizeof(version_text));
            g.version(version_text);
            version_text[sizeof(version_text) - 1] = '\0';
            for (p = version_text; *p; p++) {
                if (*p >= '0' && *p <= '9' && p[1] == '.') {
                    info->glide_version = (unsigned)(p[0] - '0') * 0x100u
                                       + (unsigned)(p[2] - '0') * 0x10u
                                       + (unsigned)(p[3] >= '0' && p[3] <= '9'
                                                    ? p[3] - '0' : 0);
                    break;
                }
            }
        }
        if (info->board_count <= 0) {
            g.shutdown();
            unload();
            return DKR_GLIDE_ERR_NO_BOARD;
        }
        g.hardware_known = 1;
        if (out) { *out = *info; }
    }
    return DKR_GLIDE_OK;
}

/* --- Opening --------------------------------------------------------------- */

/* Called back by E02-S03's exception filter. A passthrough Voodoo that keeps
   control leaves the screen black until reboot. */
static void restore_display_on_crash(void)
{
    dkr_glide_shutdown();
}

dkr_glide_result dkr_glide_open(dkr_glide_resolution wanted,
                                dkr_glide_context *out)
{
    dkr_glide_hardware hw;
    dkr_glide_result   r;
    int                i;

    r = dkr_glide_detect(&hw);
    if (r != DKR_GLIDE_OK) {
        return r;
    }
    g.select(0);

    if (wanted < 0 || wanted >= DKR_GLIDE_RES_COUNT) {
        wanted = DKR_GLIDE_RES_640x480;
    }

    /* The fallback walks down from the requested resolution. The budget is
       computed, not guessed: a failing `grSstWinOpen` does not say why, and
       "640x480 does not fit in 2 MB" is a sentence one can show. */
    for (i = (int)wanted; i < DKR_GLIDE_RES_COUNT; i++) {
        if (fb_cost_kb(RESOLUTIONS[i].w, RESOLUTIONS[i].h) > hw.fb_memory_kb) {
            continue;
        }
        if (g.win_open(0, RESOLUTIONS[i].glide_id, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
            g.context_open   = 1;
            g.ctx.resolution = (dkr_glide_resolution)i;
            g.ctx.width      = RESOLUTIONS[i].w;
            g.ctx.height     = RESOLUTIONS[i].h;
            g.ctx.buffers    = 2;
            g.ctx.depth_buffer = 1;
            if (out) { *out = g.ctx; }
            /* Registered **after** opening: before it there would be nothing
               to restore, and the registry is fixed in size. */
            dkr_win95_at_abnormal_exit(restore_display_on_crash);
            return DKR_GLIDE_OK;
        }
    }

    /* None fitted. Telling "not enough memory" from "refused" helps the caller
       write something useful. */
    r = (fb_cost_kb(RESOLUTIONS[wanted].w, RESOLUTIONS[wanted].h) > hw.fb_memory_kb)
        ? DKR_GLIDE_ERR_NO_MEMORY : DKR_GLIDE_ERR_OPEN;
    g.shutdown();
    unload();
    return r;
}

void dkr_glide_clear(unsigned argb)
{
    if (g.context_open) {
        g.clear(argb, 0, GR_WDEPTHVALUE_FARTHEST);
    }
}

void dkr_glide_swap(void)
{
    if (g.context_open) {
        /* 1: synchronise with the retrace. The choice between this and an
           immediate swap is measured in E06-S04; by default we avoid
           tearing. */
        g.swap(1);
    }
}

void dkr_glide_shutdown(void)
{
    /* Idempotent, and it has to be: the exception filter may call it after the
       normal shutdown has already run. */
    if (g.context_open) {
        g.win_close();
        g.context_open = 0;
    }
    if (g.initialised) {
        g.shutdown();
        g.initialised = 0;
    }
    unload();
}

void dkr_glide_draw_test_triangle(void)
{
    GrVertex a, b, c;
    const float w = (float)g.ctx.width;
    const float h = (float)g.ctx.height;

    if (!g.context_open) {
        return;
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));

    a.x = w * 0.5f;  a.y = h * 0.15f; a.r = 255.0f; a.g = 0.0f;   a.b = 0.0f;
    b.x = w * 0.85f; b.y = h * 0.85f; b.r = 0.0f;   b.g = 255.0f; b.b = 0.0f;
    c.x = w * 0.15f; c.y = h * 0.85f; c.r = 0.0f;   c.g = 0.0f;   c.b = 255.0f;
    a.a = b.a = c.a = 255.0f;
    a.oow = b.oow = c.oow = 1.0f;
    a.ooz = b.ooz = c.ooz = 1.0f;

    g.triangle(&a, &b, &c);
}

static int read_buffer(unsigned which, unsigned *out, int max_pixels,
                       int *width, int *height)
{
    GrLfbInfo_t info;
    int x, y, w, h, count = 0;

    if (!g.context_open || !g.lfb_lock || !g.lfb_unlock || !out) {
        return 0;
    }
    w = g.ctx.width;
    h = g.ctx.height;
    if (width)  { *width  = w; }
    if (height) { *height = h; }

    /* **Wait for the drawing engine before reading it.**
     *
     * Glide is asynchronous: `grDrawTriangle` returns long before the hardware
     * has finished, so a read that does not wait can catch a half-drawn frame.
     * `grSstIdle` drains the FIFO, whichever buffer we go on to read.
     *
     * What it is **not** is the answer to the read lagging a frame behind, and
     * that deserves recording because two plausible stories were tested here and
     * both proved false. `dkr_glide_swap` calls `grBufferSwap(1)`, scheduling the
     * flip for the next vertical retrace; a front-buffer read that follows a
     * present too closely ought therefore to return the previous frame, and the
     * symptom fitted -- the first draw of a run reading as unpainted.
     *
     * Measured on 17 August 2026 by `TEST.EXE`. Adding `grSstIdle` changed
     * nothing, the symbol resolving. Reading the back buffer *before* presenting
     * changed nothing either: back and front agree on all four passes, black then
     * painted three times. **The first draw genuinely does not rasterise**, and
     * no read-back timing is involved.
     *
     * The idle stays, because waiting for the engine before reading it is right
     * on its own terms. It is simply not a fix for anything that was wrong. */
    if (g.idle) { g.idle(); }

    memset(&info, 0, sizeof(info));
    /* Fill in `size` before the call: Glide uses it to recognise the version of
       the structure, and leaving it at zero makes the lock fail with no further
       explanation. */
    info.size = (int)sizeof(info);
    if (!g.lfb_lock(GR_LFB_READ_ONLY, which,
                    GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT, 0, &info) ||
        !info.lfbPtr) {
        return 0;
    }

    for (y = 0; y < h && count < max_pixels; y++) {
        const unsigned short *row =
            (const unsigned short *)((const unsigned char *)info.lfbPtr +
                                     (size_t)y * info.strideInBytes);
        for (x = 0; x < w && count < max_pixels; x++) {
            const unsigned short p = row[x];
            /* 565 to 888. Replicating the high bits is the right extension:
               0x1F must give 0xFF and not 0xF8, otherwise white is not white and
               every colour comparison drifts. */
            const unsigned r = (unsigned)((p >> 11) & 0x1F);
            const unsigned gg = (unsigned)((p >>  5) & 0x3F);
            const unsigned b = (unsigned)(p & 0x1F);
            out[count++] = 0xFF000000u |
                           (((r << 3) | (r >> 2)) << 16) |
                           (((gg << 2) | (gg >> 4)) <<  8) |
                            ((b << 3) | (b >> 2));
        }
    }
    g.lfb_unlock(GR_LFB_READ_ONLY, which);
    return count;
}

int dkr_glide_read_framebuffer(unsigned *out, int max_pixels,
                               int *width, int *height)
{
    return read_buffer(GR_BUFFER_FRONTBUFFER, out, max_pixels, width, height);
}

int dkr_glide_read_backbuffer(unsigned *out, int max_pixels,
                              int *width, int *height)
{
    return read_buffer(GR_BUFFER_BACKBUFFER, out, max_pixels, width, height);
}

/* --- Hooks for the backend layer --------------------------------------------- */

void *dkr_glide_symbol(const char *decorated_name)
{
    if (!g.dll || !decorated_name) {
        return 0;
    }
    return (void *)GetProcAddress(g.dll, decorated_name);
}

void dkr_glide_draw_raw(const void *a, const void *b, const void *c)
{
    if (!g.triangle || !g.context_open || !a || !b || !c) {
        return;
    }
    g.triangle(a, b, c);
}
