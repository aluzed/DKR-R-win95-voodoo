/* E05-S01 — the Voodoo presents itself as a `dkr_render_backend`.
 *
 * `glide.c` opens the card; this file wires it onto the E04-S01 interface, so
 * that the chain assembled in `tests/test_pipeline.c` can run through it without
 * changing a line. It is the only way to compare the card against the reference
 * rasteriser on *the same* input, which is the whole reason the oracle exists.
 *
 * ## What this module does not do, and why
 *
 * It uploads no texture. `texture_upload` returns zero and says so. Placing a
 * texture in TMU memory is an allocation problem — 2 MB per TMU, no paging,
 * imposed granularity — which has its own ticket (E05-S02), and translating the
 * RDP combiner into `grTexCombine` has another (E05-S03). Writing them here to
 * "have everything" would produce a naive allocator that would have to be thrown
 * away.
 *
 * The untextured modes, on the other hand, are complete: vertex colour,
 * blending, depth, back faces, scissor, alpha test, fog.
 *
 * ## The Glide 2.x constants are written from memory — hence verified
 *
 * There is no `glide.h` on this machine: the 3dfx driver's DLL does not come
 * with its header. The values below come from the Glide 2.4 specification, from
 * memory, and **a wrong value causes no error at all**: Glide does not validate
 * its enumerations, it programs the register and the image comes out different.
 * That is exactly the kind of fault one then blames on the display-list decoder.
 *
 * Hence `tools/win95/witnesses/glide_state_probe.c`: every mode translated here
 * is exercised on the card and **read back through `grLfbLock`**. What
 * measurement confirms is marked CONFIRMED and dated; the rest carries ASSUMED
 * and must not be believed. See `docs/research/win95-glide-states.md`.
 */
#include "glide.h"
#include "backend.h"
#include "tmu.h"
#include "combiner.h"

#include <string.h>

#define WINAPI __stdcall

typedef unsigned int  FxU32;
typedef unsigned char FxU8;
typedef int           FxBool;

/* --- The Glide 2.x enumerations ---------------------------------------------- */

/* Colour combiner. */
#define GR_COMBINE_FUNCTION_ZERO          0x0
#define GR_COMBINE_FUNCTION_LOCAL         0x1
#define GR_COMBINE_FUNCTION_LOCAL_ALPHA   0x2
#define GR_COMBINE_FUNCTION_SCALE_OTHER   0x3
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4

#define GR_COMBINE_FACTOR_ZERO            0x0
#define GR_COMBINE_FACTOR_LOCAL           0x1
#define GR_COMBINE_FACTOR_ONE             0x8

#define GR_COMBINE_LOCAL_ITERATED         0x0
#define GR_COMBINE_LOCAL_CONSTANT         0x1

#define GR_COMBINE_OTHER_ITERATED         0x0
#define GR_COMBINE_OTHER_TEXTURE          0x1
#define GR_COMBINE_OTHER_CONSTANT         0x2

/* Blending. */
#define GR_BLEND_ZERO                     0x0
#define GR_BLEND_SRC_ALPHA                0x1
#define GR_BLEND_ONE                      0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA      0x5

/* Comparisons — shared by the depth test and the alpha test. */
#define GR_CMP_NEVER                      0x0
#define GR_CMP_LESS                       0x1
#define GR_CMP_GREATER                    0x4
#define GR_CMP_GEQUAL                     0x6
#define GR_CMP_ALWAYS                     0x7

/* Depth buffer. */
#define GR_DEPTHBUFFER_DISABLE            0x0
#define GR_DEPTHBUFFER_ZBUFFER            0x1
#define GR_DEPTHBUFFER_WBUFFER            0x2

/* Back faces. */
#define GR_CULL_DISABLE                   0x0
#define GR_CULL_NEGATIVE                  0x1
#define GR_CULL_POSITIVE                  0x2

/* Fog. */
#define GR_FOG_DISABLE                    0x0
#define GR_FOG_WITH_ITERATED_ALPHA        0x1

/* --- Textures ---------------------------------------------------------------- *
 *
 * These values are not from memory: `tmu_probe.c` validated them on the card by
 * comparing `grTexTextureMemRequired` against the analytic size. A wrong LOD or
 * aspect ratio would have given a visibly wrong size.
 * See `docs/research/win95-tmu.md`. */
#define GR_LOD_256   0    /* the LOD names the largest dimension, and decreases */
#define GR_LOD_1     8
#define GR_ASPECT_8x1  0
#define GR_ASPECT_1x1  3
#define GR_ASPECT_1x8  6
#define GR_TEXFMT_ARGB_1555  0x0B
#define GR_TEXFMT_INTENSITY_8 0x03
#define GR_MIPMAPLEVELMASK_BOTH  0x03
#define GR_TMU0  0
#define GR_TMU1  1

/* The texture combiner, when there is a texture. */
#define GR_TEXTURECOMBINE_ZERO   0x0
#define GR_TEXTURECOMBINE_DECAL  0x1

typedef struct {
    int   smallLod;
    int   largeLod;
    int   aspectRatio;
    int   format;
    void *data;
} GrTexInfo;

typedef unsigned int (WINAPI *pfn_tex_addr)(int tmu);
typedef unsigned int (WINAPI *pfn_tex_req)(unsigned evenOdd, GrTexInfo *info);
typedef void         (WINAPI *pfn_tex_dl)(int tmu, unsigned start,
                                          unsigned evenOdd, GrTexInfo *info);
typedef void         (WINAPI *pfn_tex_src)(int tmu, unsigned start,
                                           unsigned evenOdd, GrTexInfo *info);
typedef void         (WINAPI *pfn_tex_comb)(int tmu, FxU32 rgbFn, FxU32 rgbFac,
                                            FxU32 aFn, FxU32 aFac,
                                            FxBool rgbInv, FxBool aInv);
typedef void         (WINAPI *pfn_tex_mode)(int tmu, FxU32 a, FxU32 b);

typedef void (WINAPI *pfn_5)(FxU32, FxU32, FxU32, FxU32, FxBool);
typedef void (WINAPI *pfn_4)(FxU32, FxU32, FxU32, FxU32);
typedef void (WINAPI *pfn_1)(FxU32);

static struct {
    int   ready;
    pfn_5 color_combine;
    pfn_5 alpha_combine;
    pfn_4 blend_function;
    pfn_4 clip_window;
    pfn_1 depth_mode;
    pfn_1 depth_function;
    pfn_1 depth_mask;
    pfn_1 cull_mode;
    pfn_1 alpha_test_function;
    pfn_1 alpha_test_reference;
    pfn_1 fog_mode;
    pfn_1 fog_color;
    pfn_1 constant_color;

    pfn_tex_addr tex_min, tex_max;
    pfn_tex_req  tex_required;
    pfn_tex_dl   tex_download;
    pfn_tex_src  tex_source;
    pfn_tex_comb tex_combine;
    pfn_tex_mode tex_filter, tex_clamp;
} gs;

/* --- The resident textures ---------------------------------------------------- *
 *
 * The handle returned to the caller is an index into this table, offset by one:
 * zero means failure, and that is `backend.h`'s contract. The table keeps what
 * is needed to **rebind** the texture at draw time — Glide requires the same
 * `GrTexInfo` to be handed to `grTexSource` as to `grTexDownloadMipMap`. */
typedef struct {
    unsigned long long key;
    unsigned int       address;
    unsigned int       bytes;    /* what it occupies, to detect overlap */
    GrTexInfo          info;
    unsigned char      tmu;      /* which unit it resides on */
    unsigned char      live;
} glide_texture;

/* --- Why an upload fails -----------------------------------------------------
 *
 * `gl_texture_upload` returns zero for four distinct reasons, and the caller only
 * sees the zero. Measured on the machine: 25,896 uploads for 21,211 refusals,
 * one in two — with nothing to say which of the four. We fixed slot exhaustion
 * on the assumption that it was to blame, and the figure did not move by one.
 * Separating the causes costs four integers. */
enum {
    GL_TEX_FAIL_ASPECT = 0,  /* dimensions refused by the card */
    GL_TEX_FAIL_SIZE,        /* grTexCalcMemRequired returns zero */
    GL_TEX_FAIL_SLOT,        /* descriptor table full */
    GL_TEX_FAIL_MEMORY,      /* TMU allocator exhausted */
    GL_TEX_FAIL_COUNT
};
static unsigned long g_tex_failures[GL_TEX_FAIL_COUNT];

unsigned long dkr_glide_backend_upload_failure(int kind)
{
    if (kind < 0 || kind >= GL_TEX_FAIL_COUNT) { return 0; }
    return g_tex_failures[kind];
}

#define GLIDE_MAX_TEXTURES 512
static glide_texture g_tex[GLIDE_MAX_TEXTURES];
static dkr_tmu       g_tmu[2];
static int           g_tmu_count;

/* The transfer to the card, called by the allocator.
   `data` carries the already filled `GrTexInfo`: the allocator knows neither
   LODs nor aspect ratios, and has no business knowing them. */
static int glide_download(void *user, int tmu, unsigned int address,
                          const void *data, unsigned int bytes)
{
    (void)user; (void)bytes;
    if (!gs.tex_download || !data) { return 0; }
    gs.tex_download(tmu, address, GR_MIPMAPLEVELMASK_BOTH, (GrTexInfo *)data);
    /* Glide returns nothing. The absence of any way to check here is precisely
       why `glide_texture_probe.c` goes and reads the frame buffer back. */
    return 1;
}

/* --- The backend's state ---------------------------------------------------- */
static struct {
    int              open;
    int              width, height;
    dkr_render_state current;
    int              has_state;
    unsigned long    triangles;
    /* --- What the TMU is actually pointed at ------------------------------- *
     *
     * The frame brought back on 17 August 2026 is a flat colour under
     * `DKR_FORCE_COMBINE=texel`: every sample over the whole screen returns the
     * same texel. Reading the code got as far as "the handle is right and the
     * download succeeds", and no further, so these count what `bind_texture`
     * really does.
     *
     * `binds_dead` is the one worth having. `bind_texture` returns **silently**
     * when the slot is no longer live, and the TMU then keeps sampling wherever
     * it last pointed - one texture for everything, which is exactly the
     * symptom. A silent early return without a counter is the pattern this
     * repository keeps having to rediscover. */
    unsigned long    binds;
    unsigned long    binds_dead;
    unsigned long    binds_changed;
    unsigned int     last_bound_address;
} b;

/* --- Translations ----------------------------------------------------------- *
 *
 * Each one is a short, distinct function: that is what lets the witness exercise
 * one mode at a time, and hence attribute an image difference to one precise
 * translation rather than to "the state". */

static void bind_texture(dkr_texture_handle handle);

static void apply_combine(dkr_combine_mode m, dkr_texture_handle handle)
{
    if (!gs.color_combine || !gs.alpha_combine) { return; }

    /* **With no texture bound we fall back on the vertex colour, deliberately.**
     *
     * Selecting the texture while none is resident causes no error: the TMU
     * samples whatever happens to sit at the address it was pointing at. The
     * screen is then wrong in a way that *looks* like a combiner defect, and one
     * searches the wrong side for a long while. A frankly untextured render is
     * easier to diagnose. */
    if (handle == 0 || m == DKR_COMBINE_SHADE) {
        gs.color_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_ITERATED, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_ITERATED, 0);
        return;
    }

    bind_texture(handle);
    if (gs.tex_combine) {
        /* Only one TMU used here: the texture passes through as it is.
           Multitexturing across two TMUs is E05-S04, faithful translation of the
           RDP combiner is E05-S03 — what follows covers the modes the decoder
           can already produce, no more. */
        gs.tex_combine(GR_TMU0, GR_TEXTURECOMBINE_DECAL, GR_COMBINE_FACTOR_ZERO,
                       GR_TEXTURECOMBINE_DECAL, GR_COMBINE_FACTOR_ZERO, 0, 0);
    }

    switch (m) {
    case DKR_COMBINE_TEXTURE:
        /* The texel alone: the vertex colour plays no part. */
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
    case DKR_COMBINE_TEXTURE_SHADE_ALPHA:
        /* Texel modulated by the vertex colour, but **texel alpha kept**: that
           is what lets a punched-through texture stay punched through when the
           vertex carries a transparency of its own. */
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
    default:  /* DKR_COMBINE_TEXTURE_SHADE */
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
    }
}

/* Filtering and wrapping. E05-S08 will handle them properly; here we merely
   avoid letting an inherited state decide on our behalf. */
static void apply_texture_modes(const dkr_render_state *st)
{
    /* GR_TEXTUREFILTER_POINT_SAMPLED = 0, BILINEAR = 1.
       GR_TEXTURECLAMP_WRAP = 0, CLAMP = 1 — mirroring does not exist on the
       Voodoo 2 and is handled at decode time, which is noted for E05-S08. */
    if (gs.tex_filter) {
        const FxU32 f = (st->filter == DKR_FILTER_BILINEAR) ? 1u : 0u;
        gs.tex_filter(GR_TMU0, f, f);
    }
    if (gs.tex_clamp) {
        gs.tex_clamp(GR_TMU0,
                     (st->wrap_s == DKR_WRAP_REPEAT) ? 0u : 1u,
                     (st->wrap_t == DKR_WRAP_REPEAT) ? 0u : 1u);
    }
}

static void apply_blend(dkr_blend_mode m)
{
    if (!gs.blend_function) { return; }
    switch (m) {
    case DKR_BLEND_ALPHA:
        gs.blend_function(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                          GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    case DKR_BLEND_ADDITIVE:
        gs.blend_function(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    default:
        gs.blend_function(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    }
}

/* Which buffer to use. W by default — see `apply_depth`.
 *
 * Exposed so that E05-S05 can **compare the two by measurement** rather than
 * settle it on the W mode's reputation. The ticket explicitly asks for this
 * choice to be justified by a recorded artefact measurement. */
static int g_depth_use_w = 1;

void dkr_glide_backend_depth_mode(int use_w)
{
    g_depth_use_w = use_w;
}

static void apply_depth(dkr_depth_mode m)
{
    if (!gs.depth_mode || !gs.depth_function || !gs.depth_mask) { return; }
    if (m == DKR_DEPTH_DISABLED) {
        gs.depth_mode(GR_DEPTHBUFFER_DISABLE);
        gs.depth_mask(0);
        return;
    }
    /* **W buffer, not Z.**
     *
     * `dkr_render_vertex` already carries `oow = 1/w`, which Glide consumes as
     * it is in w mode. Z mode, on the other hand, reads `ooz` over [0, 65535]
     * while the chain produces a depth over [0, 1]: every vertex would have to
     * be rescaled, hence copied, hence the benefit of having mirrored
     * `GrVertex`'s layout field for field would be lost.
     *
     * **The comparison direction does not flip**, and that is counter-intuitive.
     *
     * The natural reasoning is: the vertex carries `1/w`, a near object has a
     * large `1/w`, so the near one wins with `GR_CMP_GREATER`. This file wrote
     * that first, and the screen stayed **entirely black** — in both draw
     * orders, which rules out a sorting problem.
     *
     * The reasoning forgets that Glide does not store `1/w`: it stores an
     * encoded w value that **grows with distance**, and `grBufferClear` clears
     * to `GR_WDEPTHVALUE_FARTHEST` = 0xFFFF. Since nothing can exceed that
     * maximum, `GR_CMP_GREATER` rejects the entire scene. The comparison is
     * therefore `LESS`, as with a z buffer.
     *
     * The symptom deserved writing down: a black screen is first diagnosed as a
     * geometry or window defect, and one searches a long while before suspecting
     * a depth buffer that works perfectly.
     * Confirmed by read-back on 14 August 2026; see `win95-glide-states.md`. */
    gs.depth_mode(g_depth_use_w ? GR_DEPTHBUFFER_WBUFFER : GR_DEPTHBUFFER_ZBUFFER);
    gs.depth_function(GR_CMP_LESS);
    gs.depth_mask(m == DKR_DEPTH_TEST_AND_WRITE ? 1 : 0);
}

static void apply_cull(dkr_cull_mode m)
{
    if (!gs.cull_mode) { return; }
    /* The chain already culls back faces itself (`dkr_cull_accept`), because the
       reference rasteriser must cull the same triangles as the card. Programming
       the card *as well* would be redundant and would risk culling twice under
       opposite conventions — hence emptying everything. We disable it explicitly
       rather than leave it in an inherited state. */
    (void)m;
    gs.cull_mode(GR_CULL_DISABLE);
}

static void apply_alpha_test(unsigned char enabled, unsigned char reference)
{
    if (!gs.alpha_test_function || !gs.alpha_test_reference) { return; }
    if (!enabled) {
        gs.alpha_test_function(GR_CMP_ALWAYS);
        return;
    }
    gs.alpha_test_reference(reference);
    gs.alpha_test_function(GR_CMP_GEQUAL);
}

static void apply_fog(unsigned char enabled, unsigned int color)
{
    if (!gs.fog_mode) { return; }
    if (!enabled) {
        gs.fog_mode(GR_FOG_DISABLE);
        return;
    }
    if (gs.fog_color) { gs.fog_color(color & 0x00FFFFFFu); }
    /* Iterated alpha rather than a table: DKR computes its fog per vertex, and
       Glide's table would impose a curve that is not its own. */
    gs.fog_mode(GR_FOG_WITH_ITERATED_ALPHA);
}

/* --- The interface ---------------------------------------------------------- */

/* The number of TMUs comes from detection, not from a constant: ADR 0002
   mandates two TMUs without forbidding us from finding only one, in which case
   E05-S04's multipass fallback applies. */
static int hw_tmu_count(void)
{
    dkr_glide_hardware hw;
    if (dkr_glide_detect(&hw) != DKR_GLIDE_OK) { return 0; }
    return hw.tmu_count;
}

static int gl_open(void *self, int width, int height)
{
    dkr_glide_context ctx;
    dkr_glide_resolution wanted = DKR_GLIDE_RES_640x480;

    (void)self;
    if (width <= 320 && height <= 240)      { wanted = DKR_GLIDE_RES_320x240; }
    else if (width <= 400 && height <= 300) { wanted = DKR_GLIDE_RES_400x300; }
    else if (width <= 512 && height <= 384) { wanted = DKR_GLIDE_RES_512x384; }

    memset(&b, 0, sizeof(b));
    if (dkr_glide_open(wanted, &ctx) != DKR_GLIDE_OK) {
        return 0;
    }
    b.open   = 1;
    b.width  = ctx.width;
    b.height = ctx.height;

    if (!gs.ready) {
        gs.color_combine        = (pfn_5)dkr_glide_symbol("_grColorCombine@20");
        gs.alpha_combine        = (pfn_5)dkr_glide_symbol("_grAlphaCombine@20");
        gs.blend_function       = (pfn_4)dkr_glide_symbol("_grAlphaBlendFunction@16");
        gs.clip_window          = (pfn_4)dkr_glide_symbol("_grClipWindow@16");
        gs.depth_mode           = (pfn_1)dkr_glide_symbol("_grDepthBufferMode@4");
        gs.depth_function       = (pfn_1)dkr_glide_symbol("_grDepthBufferFunction@4");
        gs.depth_mask           = (pfn_1)dkr_glide_symbol("_grDepthMask@4");
        gs.cull_mode            = (pfn_1)dkr_glide_symbol("_grCullMode@4");
        gs.alpha_test_function  = (pfn_1)dkr_glide_symbol("_grAlphaTestFunction@4");
        gs.alpha_test_reference = (pfn_1)dkr_glide_symbol("_grAlphaTestReferenceValue@4");
        gs.fog_mode             = (pfn_1)dkr_glide_symbol("_grFogMode@4");
        gs.fog_color            = (pfn_1)dkr_glide_symbol("_grFogColorValue@4");
        gs.constant_color       = (pfn_1)dkr_glide_symbol("_grConstantColorValue@4");
        gs.tex_min      = (pfn_tex_addr)dkr_glide_symbol("_grTexMinAddress@4");
        gs.tex_max      = (pfn_tex_addr)dkr_glide_symbol("_grTexMaxAddress@4");
        gs.tex_required = (pfn_tex_req) dkr_glide_symbol("_grTexTextureMemRequired@8");
        gs.tex_download = (pfn_tex_dl)  dkr_glide_symbol("_grTexDownloadMipMap@16");
        gs.tex_source   = (pfn_tex_src) dkr_glide_symbol("_grTexSource@16");
        gs.tex_combine  = (pfn_tex_comb)dkr_glide_symbol("_grTexCombine@28");
        gs.tex_filter   = (pfn_tex_mode)dkr_glide_symbol("_grTexFilterMode@12");
        gs.tex_clamp    = (pfn_tex_mode)dkr_glide_symbol("_grTexClampMode@12");
        gs.ready = 1;
    }

    /* The TMU's bounds are **asked for**, never assumed. Measurement showed
       `grTexMinAddress` at zero, which rules out using zero as a sentinel —
       hence `DKR_TMU_NONE` at 0xFFFFFFFF. */
    memset(g_tex, 0, sizeof(g_tex));
    g_tmu_count = 0;
    if (gs.tex_min && gs.tex_max) {
        int i;
        const int n = (hw_tmu_count() > 2) ? 2 : hw_tmu_count();
        for (i = 0; i < n; i++) {
            dkr_tmu_init(&g_tmu[i], i, gs.tex_min(i), gs.tex_max(i),
                         glide_download, 0);
        }
        g_tmu_count = n;
    }
    return 1;
}

static void gl_close(void *self)
{
    (void)self;
    dkr_glide_shutdown();
    b.open = 0;
}

static void gl_begin_frame(void *self, unsigned clear_argb)
{
    (void)self;
    b.triangles = 0;
    {
        /* The allocator has to know a frame is starting: that is what lifts the
           previous frame's pins and resets the per-frame counters. Without it,
           nothing would ever be evictable again. */
        int i;
        for (i = 0; i < g_tmu_count; i++) { dkr_tmu_begin_frame(&g_tmu[i]); }
    }

    /* --- Glide only clears depth if writing to it is allowed ---------------- *
     *
     * `grBufferClear` takes a depth value, but it is only written if
     * `grDepthMask` is open. The state left behind at the end of the previous
     * frame closes it as soon as the last triangle was in `DKR_DEPTH_DISABLED`
     * or in test-without-write — that is, almost always, the interface being
     * drawn over the scene.
     *
     * As long as the depth test was inactive this went unnoticed: nothing read
     * the buffer. As soon as it was switched on, the buffer kept the first
     * frame's depths for every following one, and **the screen went black** —
     * everything failed the test against a frozen scene.
     *
     * The symptom is the same as that of an inverted comparison direction,
     * already recorded in `win95-glide-states.md`, and that is what makes this
     * defect expensive: one goes and checks the comparison, finds it right, and
     * searches anywhere but in the clear.
     *
     * We therefore open the mask for the duration of the clear. `has_state` is
     * invalidated so that the next `set_state` lays down the real state rather
     * than believing it already in place — otherwise the block comparison would
     * skip the tidying up. */
    if (gs.depth_mask) {
        gs.depth_mask(1);
        b.has_state = 0;
    }
    dkr_glide_clear(clear_argb);
}

static void gl_present(void *self)
{
    (void)self;
    dkr_glide_swap();
}

static void gl_set_state(void *self, const dkr_render_state *state)
{
    (void)self;
    if (!state) { return; }
    /* The block is `memcmp`-able by construction — that is why it carries an
       explicit padding field. Skipping an identical state avoids a burst of
       register writes per triangle, which is expensive on a 1998 PCI bus. */
    if (b.has_state && memcmp(&b.current, state, sizeof(*state)) == 0) {
        return;
    }
    b.current   = *state;
    b.has_state = 1;

    apply_combine(state->combine, state->texture);
    apply_texture_modes(state);
    apply_blend(state->blend);
    apply_depth(state->depth);
    apply_cull(state->cull);
    apply_alpha_test(state->alpha_test, state->alpha_reference);
    apply_fog(state->fog_enabled, state->fog_color);
}

static void gl_set_scissor(void *self, int x0, int y0, int x1, int y1)
{
    (void)self;
    if (!gs.clip_window) { return; }
    /* Glide refuses a window that overflows the buffer, and the refusal is
       silent: the previous window stays, and two-player split screen starts
       drawing one over the other. So we clamp here. */
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > b.width)  { x1 = b.width; }
    if (y1 > b.height) { y1 = b.height; }
    if (x1 <= x0 || y1 <= y0) { return; }
    gs.clip_window((FxU32)x0, (FxU32)y0, (FxU32)x1, (FxU32)y1);
}

static void gl_draw_triangles(void *self, const dkr_render_vertex *vertices,
                              int count)
{
    int i;
    (void)self;
    if (!vertices || count <= 0) { return; }
    /* `dkr_render_vertex` has `GrVertex`'s layout, field for field —
       `backend_layout_check.c` checks it at compile time. The hand-off therefore
       needs no conversion and no copy, which was the whole point of this
       layer. */
    for (i = 0; i + 2 < count * 3; i += 3) {
        dkr_glide_draw_raw(&vertices[i], &vertices[i + 1], &vertices[i + 2]);
        b.triangles++;
    }
}

static void gl_fill_rect(void *self, int x0, int y0, int x1, int y1,
                         unsigned argb)
{
    /* Two triangles rather than `grBufferClear` on a scissor window: the clear
       ignores blending and the alpha test, whereas DKR uses these rectangles for
       fades to black, which are translucent. */
    dkr_render_vertex v[6];
    const float r = (float)((argb >> 16) & 0xFF);
    const float g = (float)((argb >> 8) & 0xFF);
    const float bl = (float)(argb & 0xFF);
    const float a = (float)((argb >> 24) & 0xFF);
    const float xs[6] = { (float)x0, (float)x1, (float)x1,
                          (float)x0, (float)x1, (float)x0 };
    const float ys[6] = { (float)y0, (float)y0, (float)y1,
                          (float)y0, (float)y1, (float)y1 };
    int i;

    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = r; v[i].g = g; v[i].b = bl; v[i].a = a;
        /* Frontmost and depthless: an interface rectangle takes no part in
           sorting. */
        v[i].oow = 1.0f;
        v[i].z = 0.0f;
        v[i].ooz = 0.0f;
    }
    gl_draw_triangles(self, v, 2);
}

/* Translates (width, height) into a (LOD, aspect ratio) pair.
 *
 * Glide does not know the dimensions: it knows the largest one, and the ratio.
 * Returns zero if the texture is not expressible — dimensions that are not
 * powers of two, or a ratio beyond 8:1. **Refusing is the right behaviour**:
 * approximating would give a texture read askew, which looks like a coordinate
 * defect and is very hard to diagnose. */
static int lod_and_aspect(int w, int h, int *lod, int *aspect)
{
    int big = (w > h) ? w : h;
    int small = (w > h) ? h : w;
    int ratio = 0, k = 0;

    if (w <= 0 || h <= 0 || big > 256) { return 0; }
    if ((w & (w - 1)) != 0 || (h & (h - 1)) != 0) { return 0; }
    if (big / small > 8) { return 0; }

    for (k = 0; (256 >> k) != big; k++) {
        if (k > 8) { return 0; }
    }
    *lod = k;

    /* GR_ASPECT_1x1 is 3; wide ratios go down towards 0, tall ones up towards
       6. */
    ratio = big / small;
    if (w >= h) {
        *aspect = (ratio == 1) ? GR_ASPECT_1x1
                : (ratio == 2) ? 2 : (ratio == 4) ? 1 : GR_ASPECT_8x1;
    } else {
        *aspect = (ratio == 2) ? 4 : (ratio == 4) ? 5 : GR_ASPECT_1x8;
    }
    return 1;
}

static dkr_texture_handle gl_texture_upload(void *self,
                                            const dkr_texture_desc *desc)
{
    int lod = 0, aspect = 0, i, slot = -1;
    unsigned int bytes, address;
    GrTexInfo info;

    (void)self;
    if (!desc || !desc->pixels || g_tmu_count == 0 || !gs.tex_required) {
        return 0;
    }
    if (!lod_and_aspect(desc->width, desc->height, &lod, &aspect)) {
        g_tex_failures[GL_TEX_FAIL_ASPECT]++;
        return 0;
    }

    memset(&info, 0, sizeof(info));
    info.smallLod    = lod;
    info.largeLod    = lod;        /* no mipmap: E05-S08 */
    info.aspectRatio = aspect;
    info.format      = (desc->format == DKR_TEXFMT_INTENSITY8)
                       ? GR_TEXFMT_INTENSITY_8 : GR_TEXFMT_ARGB_1555;
    info.data        = (void *)desc->pixels;

    /* **The size comes from the card, not from a computation.** Measurement
       showed a rounding case — a 1x1 texture costs 8 bytes for 2 useful ones —
       and packing according to a computation would make two textures overlap.
       The symptom would not be an error but a piece of scenery wearing another's
       pattern, in a place that depends on the upload order. */
    bytes = gs.tex_required(GR_MIPMAPLEVELMASK_BOTH, &info);
    if (bytes == 0u) { g_tex_failures[GL_TEX_FAIL_SIZE]++; return 0; }

    for (i = 0; i < GLIDE_MAX_TEXTURES; i++) {
        if (g_tex[i].live && g_tex[i].key == desc->key) { slot = i; break; }
        if (!g_tex[i].live && slot < 0) { slot = i; }
    }
    if (slot < 0) { g_tex_failures[GL_TEX_FAIL_SLOT]++; return 0; }

    /* **We go through the allocator even when the texture is already known.**
     *
     * Returning the handle directly would be faster and would be a trap: the
     * texture's use timestamp would never advance, the allocator would believe
     * it abandoned, and least-recently-used eviction would throw out precisely
     * what the game uses every frame. The symptom would be permanent
     * re-uploading — hence hitches — on the most-seen textures.
     *
     * `dkr_tmu_acquire` alone tells a hit from a miss: it is what keeps the
     * counters, and it must keep them over the whole set of requests. */
    /* **Two spaces, not one.** Each TMU has its own memory, and a texture is
       only samplable from the unit where it resides. The same key can therefore
       legitimately be resident twice — but only if both units sample it, and the
       cache key includes the TMU so that this is never accidental. */
    {
        int target = desc->tmu;
        if (target < 0 || target >= g_tmu_count) { target = 0; }
        g_tex[slot].tmu = (unsigned char)target;
        address = dkr_tmu_acquire(&g_tmu[target], desc->key, &info, bytes);
    }
    if (address == DKR_TMU_NONE) {
        g_tex_failures[GL_TEX_FAIL_MEMORY]++;
        g_tex[slot].live = 0;
        return 0;
    }

    /* --- Make the table follow the allocator's evictions -------------------- *
     *
     * The descriptor table and the TMU allocator led independent lives, and that
     * was a two-faced defect:
     *
     *   - **An evicted texture kept its slot `live`.** The 512 slots filled up
     *     in a dozen frames — DKR uploads some forty per frame — and then
     *     `slot < 0` refused everything. Measured on the machine: 25,853 uploads
     *     for **21,195 refusals**.
     *
     *   - **Worse than the refusal**: as long as the slot lived, it designated
     *     memory the allocator had reassigned. That is exactly the symptom the
     *     `tex_required` comment above fears — a piece of scenery wearing
     *     another's pattern, in a place that depends on the upload order.
     *
     * We therefore invalidate every slot on the same unit whose range overlaps
     * the one just obtained. The allocator remains the sole judge of what
     * resides where; the table merely follows it, which is the only way it does
     * not lie. The sweep costs 512 comparisons per upload, that is a few tens of
     * thousands per frame — negligible next to a single texture conversion. */
    {
        int j;
        for (j = 0; j < GLIDE_MAX_TEXTURES; j++) {
            if (j == slot || !g_tex[j].live) { continue; }
            if (g_tex[j].tmu != g_tex[slot].tmu) { continue; }
            if (g_tex[j].address < address + bytes &&
                address < g_tex[j].address + g_tex[j].bytes) {
                g_tex[j].live = 0;
            }
        }
    }

    g_tex[slot].key     = desc->key;
    g_tex[slot].address = address;
    g_tex[slot].bytes   = bytes;
    g_tex[slot].info    = info;
    g_tex[slot].info.data = 0;   /* the pixels do not belong to us */
    g_tex[slot].live    = 1;
    return (dkr_texture_handle)(slot + 1);
}

static void gl_texture_release(void *self, dkr_texture_handle handle)
{
    (void)self;
    if (handle == 0 || handle > GLIDE_MAX_TEXTURES) { return; }
    /* We forget the handle without freeing the block: it is the allocator that
       decides when to evict, least-recently-used first, and it will do it better
       than the caller. Freeing here would throw away a texture the next frame
       would ask for again — the worst regime, where the bus is paid for
       nothing. */
    g_tex[handle - 1].live = 0;
}

/* Binds the current texture before drawing. Without `grTexSource`, the TMU
   samples whatever sits at the address it was pointing at — hence another
   texture. */
static void bind_texture(dkr_texture_handle handle)
{
    glide_texture *tx;
    if (handle == 0 || handle > GLIDE_MAX_TEXTURES || !gs.tex_source) { return; }
    tx = &g_tex[handle - 1];
    if (!tx->live) { b.binds_dead++; return; }
    b.binds++;
    if (tx->address != b.last_bound_address) {
        b.binds_changed++;
        b.last_bound_address = tx->address;
    }
    /* Bind on the unit where the texture resides, and not on TMU 0 by default:
       binding a TMU 1 address on TMU 0 causes no error, TMU 0 simply sampling
       whatever sits at that address in its own memory. The scenery would then
       wear another's pattern. */
    gs.tex_source(tx->tmu ? GR_TMU1 : GR_TMU0, tx->address,
                  GR_MIPMAPLEVELMASK_BOTH, &tx->info);
}

void dkr_render_backend_glide(dkr_render_backend *out)
{
    if (!out) { return; }
    memset(out, 0, sizeof(*out));
    out->self            = &b;
    out->name            = "3dfx Glide";
    out->open            = gl_open;
    out->close           = gl_close;
    out->begin_frame     = gl_begin_frame;
    out->present         = gl_present;
    out->set_state       = gl_set_state;
    out->set_scissor     = gl_set_scissor;
    out->draw_triangles  = gl_draw_triangles;
    out->fill_rect       = gl_fill_rect;
    out->texture_upload  = gl_texture_upload;
    out->texture_release = gl_texture_release;
}

unsigned long dkr_glide_backend_triangle_count(void)
{
    return b.triangles;
}

void dkr_glide_backend_bind_stats(unsigned long *binds,
                                  unsigned long *dead,
                                  unsigned long *changed)
{
    if (binds)   { *binds   = b.binds; }
    if (dead)    { *dead    = b.binds_dead; }
    if (changed) { *changed = b.binds_changed; }
}

/* Applies a setup from the E05-S03 table, as it is.
 *
 * A direct entry point, used by the measurement harness and meant for the
 * engine. It short-circuits `apply_combine`, whose four modes are only a
 * shorthand: the table covers twenty-nine configurations, and it is the table
 * that must decide, not an enumeration that summarises it.
 *
 * `constant_argb` loads Glide's single constant register. **Which RDP register
 * to put there is a decision of the table** — `DKR_CONST_PRIMITIVE` or
 * `DKR_CONST_ENVIRONMENT` — and the second constant, when it is needed, travels
 * in the vertex alpha. */
void dkr_glide_backend_set_recipe(const dkr_cc_setup *r, unsigned constant_argb)
{
    if (!r) { return; }
    if (gs.constant_color) { gs.constant_color(constant_argb); }
    if (gs.color_combine) {
        gs.color_combine(r->cc_function, r->cc_factor, r->cc_local, r->cc_other, 0);
    }
    if (gs.alpha_combine) {
        gs.alpha_combine(r->ac_function, r->ac_factor, r->ac_local, r->ac_other, 0);
    }
    if (gs.tex_combine && r->uses_texture) {
        gs.tex_combine(GR_TMU0, r->tc_function, r->tc_factor,
                       r->tc_function, r->tc_factor, 0, 0);
    }
}

void dkr_glide_backend_bind(dkr_texture_handle handle)
{
    bind_texture(handle);
}

/* Chains the two units: TMU 1 samples, its output becomes TMU 0's "other"
 * input, and TMU 0's output feeds the colour combiner.
 *
 * **The order of the calls matters.** Glide wants the highest TMU first: it is
 * the one that starts the chain, and programming it after TMU 0 leaves the
 * latter chained onto a unit that is not configured yet. The effect is not an
 * error but an image built from the previous state — hence right as long as
 * nothing changes, and wrong at the first state change, which is the worst
 * moment to notice.
 *
 * `function` and `factor` are passed rather than hard-coded: their enumeration
 * values are measured by `multitex_probe.c`, and this project has already paid
 * twice for assuming such values. */
/* **The single-TMU fallback must be exercisable on a card that has two.**
 *
 * That is the risk the ticket names: "easy to write and easy never to test, for
 * want of single-TMU hardware to hand". Without this flag, the multipass path
 * would only be checked after a user report — hence on someone else's machine,
 * and without a trace. */
static int g_force_single_tmu;

void dkr_glide_backend_force_single_tmu(int force)
{
    g_force_single_tmu = force;
}

int dkr_glide_backend_tmu_count(void)
{
    return g_force_single_tmu ? 1 : g_tmu_count;
}

void dkr_glide_backend_chain(dkr_texture_handle tmu0, dkr_texture_handle tmu1,
                             unsigned char function, unsigned char factor)
{
    if (dkr_glide_backend_tmu_count() < 2 || !gs.tex_combine) { return; }

    bind_texture(tmu1);
    /* TMU 1 merely samples: it has no unit upstream. */
    gs.tex_combine(GR_TMU1, GR_TEXTURECOMBINE_DECAL, 0,
                             GR_TEXTURECOMBINE_DECAL, 0, 0, 0);
    bind_texture(tmu0);
    gs.tex_combine(GR_TMU0, function, factor, function, factor, 0, 0);
}

const dkr_tmu *dkr_glide_backend_tmu(int index)
{
    if (index < 0 || index >= g_tmu_count) { return 0; }
    return &g_tmu[index];
}
