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
/* `(other - local) * factor + local`. Confirmed on the card by
   `combine_enum_probe.c`: function 7 reads as BLEND. */
#define GR_COMBINE_FUNCTION_BLEND_OTHER   0x7
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4

#define GR_COMBINE_FACTOR_ZERO            0x0
#define GR_COMBINE_FACTOR_LOCAL           0x1
#define GR_COMBINE_FACTOR_ONE             0x8
/* `combine_enum_probe.c` sweeps this value with `other` driven from the constant
   register and finds it reads as `ONE` there. With `other` driven from the
   **texture**, which is how it is used below, it evidently does not: replacing it
   with a plain `FACTOR_ONE` puts the copyright screen back from 17 divergent
   pixels to 801. The sweep's reach is its own configuration, and this name is
   kept with that caveat written beside it rather than renamed on the strength of
   a measurement that did not cover the case. */
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA 0xB

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
/* **Per-channel factors, and where they are legal.** `GR_BLEND_SRC_COLOR` is a
   *destination* factor and `GR_BLEND_DST_COLOR` a *source* one; both are 0x2 and
   which is meant depends on the argument's position. Likewise 0x6 is
   `ONE_MINUS_SRC_COLOR` for the destination and `ONE_MINUS_DST_COLOR` for the
   source. Only the destination forms are used here, and they are named for what
   they are in the position they are used in.

   These two are the only values in this file not yet seen to work on the card.
   The four that were -- ZERO 0x0, SRC_ALPHA 0x1, ONE 0x4, ONE_MINUS_SRC_ALPHA
   0x5 -- match the canonical table position for position, which is why these are
   taken from the same table rather than guessed; it is not the same as having
   measured them, and this comment is here so that a wrong image is diagnosed
   from here first. */
#define GR_BLEND_ONE_MINUS_SRC_COLOR      0x6   /* destination factor only */

/* Comparisons — shared by the depth test and the alpha test. */
#define GR_CMP_LEQUAL                     0x3

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
#define GR_TEXFMT_ALPHA_INTENSITY_88 0x0D
#define GR_TEXFMT_ARGB_4444  0x0C
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
    unsigned long      last_used; /* logical timestamp, for least-recently-used */
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
/* Slots taken back rather than refused. Reported so that "the table never fills"
   is a figure and not a hope. */
static unsigned long g_tex_reclaimed;
/* The table's own clock, advanced on every upload and every binding. The
   allocator has one of its own and they are deliberately separate: this one
   measures the age of a *handle*, which is what the table hands out. */
static unsigned long g_tex_clock;
/* What `texture_lookup` answered. Three outcomes and not two: a **stale** hit --
   the slot named a key the allocator no longer holds -- is the one that would
   have drawn the wrong texture, so conflating it with a plain miss would hide
   exactly the failure this query has to be trusted not to make. A run where
   `stale` is not zero is a run where the two tables disagree. */
static unsigned long g_tex_lookup_hits, g_tex_lookup_misses, g_tex_lookup_stale;

void dkr_glide_backend_lookup_stats(unsigned long *hits, unsigned long *misses,
                                    unsigned long *stale)
{
    if (hits)   { *hits   = g_tex_lookup_hits; }
    if (misses) { *misses = g_tex_lookup_misses; }
    if (stale)  { *stale  = g_tex_lookup_stale; }
}

unsigned long dkr_glide_backend_upload_failure(int kind)
{
    if (kind < 0 || kind >= GL_TEX_FAIL_COUNT) { return 0; }
    return g_tex_failures[kind];
}

unsigned long dkr_glide_backend_slots_reclaimed(void)
{
    return g_tex_reclaimed;
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
    /* State applications served by chaining the two units (E05-S04). Reported
       beside the decoder's `two-layer` triangle count: the two answer different
       halves of "is the second unit doing anything" -- this one says the states
       were programmed, that one says triangles carried coordinates for it, and
       either being zero while the other is not is a defect with an address. */
    unsigned long    two_layer_states;
    /* Glide entry points `GetProcAddress` could not find. See
       `dkr_glide_backend_symbols`. */
    unsigned long    symbols_total;
    unsigned long    symbols_missing;
    const char      *symbol_first_missing[6];
    unsigned int     symbol_first_missing_n;
    /* E05-S03's second pass: drawn, and skipped with the reason. A pass that is
       silently not drawn is indistinguishable from one that is not needed, and
       the whole point of the guards below is that most of them are not needed. */
    unsigned long    pass2_drawn;
    unsigned long    pass2_identity;    /* the constant's alpha is zero */
    unsigned long    pass2_unsupported; /* two cycles, but not the lerp form */
    unsigned long    pass2_blend;       /* drawn over a blended first pass */
    unsigned long    pass2_by_shade;    /* the per-channel form, two passes */
    unsigned long    recipe_multipass;  /* first cycles taken from the table */
    unsigned long    prepass_drawn;     /* first cycles done in two blends */
    unsigned long    prepass_alpha_test;/* refused: a cutout is in force */
    unsigned long    prepass_in_vertex; /* the constant carried in the vertex */
    unsigned long    binds_changed;
    unsigned int     last_bound_address;
} b;

/* --- Translations ----------------------------------------------------------- *
 *
 * Each one is a short, distinct function: that is what lets the witness exercise
 * one mode at a time, and hence attribute an image difference to one precise
 * translation rather than to "the state". */

static void bind_texture(dkr_texture_handle handle);
static void gl_invalidate(void *self);

/* Which second pass a configuration needs, if any. Declared here rather than
   only beside the passes themselves: `gl_set_state` has to know whether one is
   coming before it programs the first pass's constant. */
#define PASS2_NONE        0
#define PASS2_BY_ENV_ALPHA 1   /* factor = the environment's alpha, a scalar */
#define PASS2_BY_SHADE     2   /* factor = the vertex colour, per channel */
static int pass2_wanted(const dkr_render_state *st);

static void apply_combine(dkr_combine_mode m, dkr_texture_handle handle,
                          unsigned int constant, unsigned char alpha_scale)
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
        (void)constant;
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
    case DKR_COMBINE_TEXTURE_CONSTANT:
        /* **Texel modulated by the constant register**, which is what carries
           DKR's multi-pass text: the same glyph drawn several times at identical
           coordinates, each pass differing only by `G_SETPRIMCOLOR`. Without it
           the passes are indistinguishable and the letters stack into a smear.
         *
           **The repacking used to happen here, and that was the wrong place.**
           The RDP writes `0xRRGGBBAA`; `grConstantColorValue` takes
           `0xAARRGGBB`, and passing one for the other shifts every channel by a
           byte -- measured on the machine, the screen's blue went to zero, the
           background from 0x7BDFF7 to 0x7BDF00, distinct colours from 926 to
           227. The shift was correct and local, so `constant_color` went on
           carrying the RDP word while `backend.h` said it carried ARGB.
         *
           `dkr_rdp_to_render_state` now repacks at the source, which is what
           makes that sentence true for **every** consumer -- including the
           catalogue setups, which read the field as documented and got the RDP
           word for it. See the note there for what that cost. */
        /* **The alpha handed to the card is the one the RDP's alpha mux computes,
           not the alpha of the colour register.**
         *
         * `GR_COMBINE_LOCAL_CONSTANT` reads the constant's RGB for the colour and
         * its alpha for the alpha, so putting the mux's factor in the alpha byte
         * makes one register serve both correctly. The byte that was there is the
         * colour register's own alpha, which the RDP's alpha side need never have
         * named -- and for this game's text it names the other register.
         *
         * What that cost: the last of five text passes carried an environment
         * alpha of zero, `FACTOR_LOCAL` multiplied by it, and the pass that paints
         * the character's name contributed nothing. 2416 pixels of the nameplate
         * came out black where the oracle put blue -- the whole of the divergence
         * E09-S02 found on its first real frame. */
        if (gs.constant_color) {
            gs.constant_color(((unsigned int)alpha_scale << 24) |
                              (constant & 0x00FFFFFFu));
        }
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
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

        /* --- Which of those the card's Glide actually exports ---------------- *
         *
         * `dkr_glide_symbol` is `GetProcAddress`, and a name it cannot find comes
         * back **null without a word**. Every user of these pointers then guards
         * itself and returns quietly -- `apply_blend` opens with
         * `if (!gs.blend_function) { return; }`, `apply_combine` with a test on
         * the two combine entries. A missing export therefore does not fail: it
         * draws the frame with whatever state was last programmed, and the image
         * is wrong in a way that reads as a decoder defect, on the one side of
         * this port that cannot be examined from the host.
         *
         * Counted here and printed by the tool, as everything else in this file
         * is. Eighteen pointers and one line of output, against two evenings
         * spent on a shadow and a caption that both come out opaque on the card
         * -- which is what a blender that was never programmed would do. Whether
         * that is the cause is the next measurement; that it could not be
         * *asked* was the defect. */
        {
            static const char *const names[] = {
                "grColorCombine", "grAlphaCombine", "grAlphaBlendFunction",
                "grConstantColorValue", "grClipWindow", "grDepthBufferMode",
                "grDepthBufferFunction", "grDepthMask", "grCullMode",
                "grAlphaTestFunction", "grAlphaTestReferenceValue",
                "grFogMode", "grFogColorValue", "grTexMinAddress",
                "grTexMaxAddress", "grTexTextureMemRequired",
                "grTexDownloadMipMap", "grTexSource", "grTexCombine",
                "grTexFilterMode", "grTexClampMode"
            };
            const void *const fns[] = {
                (const void *)gs.color_combine, (const void *)gs.alpha_combine,
                (const void *)gs.blend_function, (const void *)gs.constant_color,
                (const void *)gs.clip_window, (const void *)gs.depth_mode,
                (const void *)gs.depth_function, (const void *)gs.depth_mask,
                (const void *)gs.cull_mode, (const void *)gs.alpha_test_function,
                (const void *)gs.alpha_test_reference, (const void *)gs.fog_mode,
                (const void *)gs.fog_color, (const void *)gs.tex_min,
                (const void *)gs.tex_max, (const void *)gs.tex_required,
                (const void *)gs.tex_download, (const void *)gs.tex_source,
                (const void *)gs.tex_combine, (const void *)gs.tex_filter,
                (const void *)gs.tex_clamp
            };
            unsigned int i;
            b.symbols_total = (unsigned long)(sizeof(fns) / sizeof(fns[0]));
            b.symbols_missing = 0;
            b.symbol_first_missing_n = 0;
            for (i = 0; i < b.symbols_total; i++) {
                if (fns[i] == 0) {
                    b.symbols_missing++;
                    if (b.symbol_first_missing_n <
                        sizeof(b.symbol_first_missing) /
                        sizeof(b.symbol_first_missing[0])) {
                        b.symbol_first_missing[b.symbol_first_missing_n++] =
                            names[i];
                    }
                }
            }
        }
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
     * skip the tidying up.
     *
     * --- And the mask was only half of it -----------------------------------
     *
     * Measured on 24 August 2026, by a question whose answer was known before
     * the run. `DKR_FLATTEN_W=1` puts every triangle at `oow = 1`, the nearest
     * depth there is; `DKR_PAINT_WHITE=1` makes every one of them opaque white.
     * Against a buffer cleared to `GR_WDEPTHVALUE_FARTHEST` every triangle
     * passes, the first writer keeps each pixel, and the frame the sky quad
     * alone covers is **307,200 painted pixels**. That is arithmetic, not a
     * hope.
     *
     * The six dumped lists came back 39,008 — 0 — 959 — 0 — 0 — 0. Nothing
     * passes, and it decays to nothing as the first lists stamp the buffer at
     * the nearest value and no later triangle can be *strictly* nearer.
     *
     * So the clear does not reach the depth buffer. The mask is open; what is
     * shut is the unit itself — `apply_depth` leaves `grDepthBufferMode` at
     * `GR_DEPTHBUFFER_DISABLE` whenever the last thing drawn asked for no depth,
     * which is every list, the fade rectangle being the last thing drawn. A
     * disabled depth unit has no aux write for `grBufferClear` to perform,
     * whatever the mask says.
     *
     * That the card behaves so is a reading of the measurement and not of any
     * documentation to hand: what is measured is that opening the mask alone
     * does not clear, and that opening the mode as well does. The mode is put
     * back by the same `has_state` invalidation that already served the mask. */
    if (gs.depth_mask) {
        if (gs.depth_mode) {
            gs.depth_mode(g_depth_use_w ? GR_DEPTHBUFFER_WBUFFER
                                        : GR_DEPTHBUFFER_ZBUFFER);
        }
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

    /* --- The table decides, when it has an entry ---------------------------- *
     *
     * `apply_combine`'s four modes are a shorthand over twenty-nine catalogued
     * configurations, and E05-S03 generated the table precisely so that the
     * shorthand would stop being the last word. `recipe` is the index the
     * decoder matched; -1 means the configuration is not in the table, and then
     * the shorthand is all there is.
     *
     * The texture still has to be bound -- the recipe says how to combine a
     * texel, not where it lives. */
    /* --- And only when the entry is certified faithful ---------------------- *
     *
     * "Exact" and "closer to the game's image than the fallback" are two
     * different properties, and the table only ever certified the first. A
     * configuration classified `approximate`, `multipass` or `two texels`
     * carries a setup the table itself says does not compute it -- a second
     * fallback, preferred over the announced one on the strength of being in the
     * table, with nothing measured either way.
     *
     * `combiner_probe.c` measures both now, same quad, same inputs, same reading
     * point, on the card. Out of 29 configurations the announced fallback renders
     * **seven** closer, and every one of the seven is non-exact; **not one exact
     * entry is beaten by it** -- their deviations run 0 to 8 against the
     * fallback's 0 to 156 on the same rows.
     *
     * The rule therefore follows the certification and not the table membership:
     * a setup that reproduces the configuration is applied, one that is only its
     * best available imitation gives way to the fallback E05-S03 specified.
     *
     * What it cost to learn: the one-cycle entries became reachable on 25 August
     * 2026 and `G_CC_BLENDT_ENV_ALPHA_A_TxP` -- approximate, deviation 148
     * against the fallback's 66 -- took over surfaces the fallback had been
     * rendering. Wizpig came out a black silhouette. The six non-exact entries
     * whose setup happens to measure closer than the fallback lose it here too,
     * and that is deliberate: their advantage was measured on one synthetic quad
     * with one set of inputs, which is not a property of the configuration. */
    if (state->recipe > 0 && state->recipe <= dkr_cc_table_count()) {
        const dkr_cc_entry *e = dkr_cc_table_at(state->recipe - 1);
        /* --- Two texels, one pass (E05-S04) ---------------------------------- *
         *
         * The table says this configuration reads `TEXEL1`, and the decoder has
         * a second layer bound. Chained, TMU 1 samples and its result feeds TMU
         * 0, which is one pass and no extra fill -- the whole reason this is
         * worth doing on a card whose limit is fill.
         *
         * `DECAL` on TMU 0's stage: the blend between the two layers is the
         * colour combiner's business, and it has already been programmed from
         * the table's setup below. Doing it twice would apply the factor twice.
         *
         * Not gated on `DKR_CC_EXACT`: the certification of 28 August is about
         * whether a *single-texture* setup reproduces the configuration, and a
         * two-texel entry is classified `DKR_CC_TWO_TEXELS` precisely because it
         * cannot be. Chaining is what makes it reproducible, so the gate that
         * sends the others to the fallback would send this one there for the one
         * reason the second unit answers. */
        if (e != 0 && e->category == DKR_CC_TWO_TEXELS &&
            state->texture1 != 0 && dkr_glide_backend_tmu_count() >= 2) {
            dkr_glide_backend_chain(state->texture, state->texture1,
                                    GR_TEXTURECOMBINE_DECAL, 0);
            dkr_glide_backend_set_recipe(&e->setup, state->constant_color);
            b.two_layer_states++;
        } else if (e != 0 && (e->category == DKR_CC_EXACT ||
                              e->category == DKR_CC_MULTIPASS)) {
            /* --- `MULTIPASS` joins `EXACT` here, and that is the point --------- *
             *
             * The gate used to be `EXACT` alone, on the reasoning that a setup
             * which only imitates a configuration should give way to the
             * fallback -- written after an *approximate* entry took over surfaces
             * and rendered Wizpig as a black silhouette.
             *
             * `MULTIPASS` is not that case. Such an entry is classified so
             * because a *second* pass is needed, and its setup describes the
             * **first cycle exactly**. Sending it to `apply_combine`'s four modes
             * threw that exactness away for nothing.
             *
             * What it cost, measured on 8 September 2026 at one pixel of the
             * character the card renders black:
             *
             *   RDP cycle 1   (TEXEL0 - PRIM) * SHADE_ALPHA + PRIM   = the texel
             *   the shorthand  texel x shade                          = black
             *
             * The shorthand used the shade's **colour** where the RDP uses its
             * **alpha**, and the vertex colour is near zero there. Two days were
             * spent on the second cycle of that configuration; the first was
             * what was wrong. */
            if (e->setup.uses_texture) { bind_texture(state->texture); }
            /* --- The first half of an exact pair ------------------------------ *
             *
             * When a second pass by the environment's alpha is coming, this pass
             * is not the configuration's first cycle but `C (1 - e)` - see the
             * derivation over `pass2_draw`. The `(1 - e)` is folded into the
             * constant's **colour** here, where the CPU knows `e` and the card
             * has no factor that would deliver it; the alpha byte is left alone,
             * since it still carries `p`.
             *
             * `FUNCTION_LOCAL` configurations ignore the constant's colour
             * entirely, so scaling it costs them nothing. */
            {
                unsigned int c1 = state->constant_color;
                if (pass2_wanted(state) == PASS2_BY_ENV_ALPHA) {
                    const unsigned int inv =
                        255u - ((state->env_color >> 24) & 0xFFu);
                    c1 = (c1 & 0xFF000000u)
                       | (((((c1 >> 16) & 0xFFu) * inv) / 255u) << 16)
                       | (((((c1 >>  8) & 0xFFu) * inv) / 255u) <<  8)
                       |  ((( c1         & 0xFFu) * inv) / 255u);
                }
                dkr_glide_backend_set_recipe(&e->setup, c1);
            }
            if (e->category == DKR_CC_MULTIPASS) { b.recipe_multipass++; }
        } else {
            apply_combine(state->combine, state->texture, state->constant_color,
                          state->alpha_scale);
        }
    } else {
        apply_combine(state->combine, state->texture, state->constant_color,
                          state->alpha_scale);
    }
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

/* --- E05-S03's second pass ---------------------------------------------------- *
 *
 * **Why one exists at all.** `DKR_CC_MULTIPASS` said "several passes get there"
 * and named a classification, not an implementation: everything that was not
 * `DKR_CC_EXACT` fell through to `apply_combine`'s four single-pass modes, which
 * compute the RDP's *first* cycle and drop the second. Measured on 4 September
 * 2026, that is why the card drew a character as a black silhouette while the
 * oracle drew him in a red cap.
 *
 * **Why it is affordable.** The multipass share of the fill is 90 to 100 % on
 * this game's scenes, which is what made a second pass look prohibitive on a
 * fill-limited Voodoo 2. It is the wrong figure: the dominant second cycle is a
 * lerp toward the environment colour **by that colour's alpha**, and an alpha of
 * zero makes it the identity. The fill where the second cycle actually changes a
 * pixel is 0 % of the copyright screen, 0.08 % of the intro, 1.4 % of the race
 * and 6.3 % of the hub. See `docs/research/win95-multipass-cost.md`.
 *
 * **What is drawn.** The same triangles, the environment colour flat, at an alpha
 * of `texel_alpha x env_alpha`, blended over what the first pass left. The colour
 * is taken from the constant register and the alpha keeps the texture's, which is
 * what makes the second pass cover exactly what the first covered: a cut-out
 * texel has alpha zero and contributes nothing, here as there.
 *
 * **Three guards, each counted.** A second pass drawn where it is not wanted is
 * worse than none, because it costs fill *and* is wrong.
 */
/* The second cycles this reproduces. Both are the same shape --
   `(ENVIRONMENT - COMBINED) * factor + COMBINED`, a lerp from the first cycle's
   result toward the environment colour -- and they differ by the factor, which
   is what decides how many passes it takes.

   Recognised by the mux fields and not by the entry's name: names are the
   generator's, fields are the hardware's. */
/* The three names are defined beside `gl_set_state`'s forward declaration, which
   needs them earlier in the file. */

static int pass2_kind(const dkr_cc_entry *e)
{
    if (e == 0 || e->cycle != DKR_CYCLE_2) { return PASS2_NONE; }
    if (e->rgb[1].a != (unsigned char)DKR_CC_ENVIRONMENT ||
        e->rgb[1].b != (unsigned char)DKR_CC_COMBINED ||
        e->rgb[1].d != (unsigned char)DKR_CC_COMBINED) {
        return PASS2_NONE;
    }
    if (e->rgb[1].c == (unsigned char)DKR_CC_ENV_ALPHA) {
        return PASS2_BY_ENV_ALPHA;
    }
    if (e->rgb[1].c == (unsigned char)DKR_CC_SHADE) { return PASS2_BY_SHADE; }
    return PASS2_NONE;
}

/* **One switch that turns off everything this backend draws beyond one pass.**
 *
 * The extra passes exist because one Glide stage cannot hold two RDP cycles. They
 * are also, by construction, the newest and least-checked thing in the pixel
 * path, and when a defect appears on a screen nobody has a capture of, the first
 * question worth an experiment is whether the port drew it or whether the port
 * drew it *twice*.
 *
 * `DKR_NO_MULTIPASS=1` answers that in one run. It does not make the picture
 * right -- the configurations that need two cycles come out with one, which is
 * the state the port was in before E05-S04 -- but it is the state whose defects
 * are already described. A defect that survives the switch is not the passes';
 * a defect that vanishes with it is.
 *
 * This is the same instrument as `DKR_NO_ALPHA_TEST`, and it exists for the same
 * reason: a hypothesis with a shape deserves a switch, not an argument. */
static int g_extra_passes = 1;

void dkr_glide_backend_extra_passes(int on) { g_extra_passes = on ? 1 : 0; }

static int pass2_wanted(const dkr_render_state *st)
{
    const dkr_cc_entry *e;
    int kind;

    if (!g_extra_passes) { return 0; }
    if (st->recipe <= 0 || st->recipe > dkr_cc_table_count()) { return 0; }
    e = dkr_cc_table_at(st->recipe - 1);
    /* A one-cycle configuration has nothing to compose and is not a refusal.
       Counting it as one made the first measurement read "unsupported=165" for a
       scene whose problem was elsewhere entirely. */
    if (e == 0 || e->cycle != DKR_CYCLE_2) { return 0; }
    /* **Only the entries the catalogue classifies as needing several passes.**
     *
     * An `EXACT` entry is one a single Glide setup reproduces, second cycle
     * included, so a pass on top of it would apply that cycle twice. A
     * `TWO_TEXELS` entry is served by chaining the units in E05-S04, which is
     * also a complete answer. Neither is a case for this.
     *
     * No entry in today's table is both `EXACT` and the lerp shape, so this
     * changes nothing now -- which is exactly why it is written down rather than
     * left to the table's current contents. The table is generated. */
    if (e->category != DKR_CC_MULTIPASS) { return 0; }
    kind = pass2_kind(e);
    if (kind == PASS2_NONE) { b.pass2_unsupported++; return 0; }
    /* The environment's alpha is the lerp factor for the scalar form. Zero means
       the second cycle is the identity, and this is the guard that makes the
       whole thing cheap -- 585 batches of 900 on the hub, and all 510 on the
       race. The per-channel form has no such constant to test: its factor is the
       vertex colour, which varies across the triangle. */
    if (kind == PASS2_BY_ENV_ALPHA &&
        ((st->env_color >> 24) & 0xFFu) == 0u) { b.pass2_identity++; return 0; }
    /* --- Over a blended first pass it is approximate, and drawn anyway -------- *
     *
     * The RDP computes cycle 2 and *then* blends with the frame buffer; this
     * draws cycle 2 as a blend against a frame buffer that already holds cycle 1.
     * Over an opaque first pass the two are equal. Over an alpha-blended one they
     * are not, and the gap is `dst * (1 - a) * k * a` -- bounded by a quarter of
     * the lerp factor, and zero where the surface is opaque or invisible.
     *
     * **Refusing it was measured and was worse.** On 6 September 2026 the first
     * version refused, and drew 2 second passes out of 900 batches on the hub:
     * the surfaces whose second cycle actually does something there are the
     * alpha-blended ones, so the guard removed exactly the cases the pass exists
     * for. An approximation that announces itself beats a correct rule that
     * applies to nothing.
     *
     * It announces itself here, by being counted separately. */
    if (st->blend != DKR_BLEND_OPAQUE) { b.pass2_blend++; }
    return kind;
}

/* --- The first cycle, when one Glide stage cannot hold it -------------------- *
 *
 * `(TEXEL0 - PRIMITIVE) * SHADE_ALPHA + PRIMITIVE` is a lerp between the constant
 * register and the texel, by the **vertex alpha**. Glide's combiner has the right
 * shape for it -- `(other - local) * factor + local` -- and cannot supply the
 * factor: a factor comes from the local or from the other, and `PRIMITIVE` has to
 * be the local while `SHADE_ALPHA` is neither. The catalogue's generated setup
 * runs into the same wall and names the local's factor instead.
 *
 * So the shorthand rendered it as `texel x shade`, using the shade's *colour*
 * where the RDP uses its *alpha*. Measured at (417,161) of the hub on 8 September
 * 2026: the shade colour is near zero and its alpha is one, so the RDP's answer is
 * the texel -- a red cap -- and the card's was black. That single configuration
 * was the largest divergence in the corpus, wrong on **every** pixel it painted,
 * and two days had been spent on its *second* cycle before the recipe map said
 * where to look.
 *
 * It decomposes into two passes that use only what the blender has:
 *
 *     A:  colour = PRIMITIVE, blend ONE / ZERO
 *     B:  colour = TEXEL0,    blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA,
 *                             the source alpha being the iterated alpha
 *
 * A lays the constant down; B blends the texel over it by the vertex alpha. The
 * result is exactly the RDP's first cycle, and the second-cycle pass then
 * composes on top of it as it does over any other first pass.
 *
 * **A pre-pass and not a post-pass**: it *replaces* the ordinary draw rather than
 * following it.
 */
/* The first cycles this reproduces in two blends. Both are lerps between the
   texel and a constant register; they differ by which one is the base and by
   where the factor comes from, and that decides what each pass draws. */
#define PREPASS_NONE           0
#define PREPASS_PRIM_TO_TEXEL  1  /* (TEXEL0 - PRIM) * SHADE_ALPHA + PRIM */
#define PREPASS_TEXEL_ALONE    2  /* (ENV - TEXEL0) * ENV_ALPHA + TEXEL0 */

static int prepass_shape(const dkr_cc_entry *e)
{
    if (e == 0 || e->category == DKR_CC_EXACT) { return PREPASS_NONE; }
    if (e->rgb[0].a == (unsigned char)DKR_CC_TEXEL0 &&
        e->rgb[0].b == (unsigned char)DKR_CC_PRIMITIVE &&
        e->rgb[0].c == (unsigned char)DKR_CC_SHADE_ALPHA &&
        e->rgb[0].d == (unsigned char)DKR_CC_PRIMITIVE) {
        return PREPASS_PRIM_TO_TEXEL;
    }
    /* --- `(ENVIRONMENT - TEXEL0) * ENV_ALPHA + TEXEL0`: tried, measured, rejected *
     *
     * That shape draws this game's copyright text, and the card renders 784 of
     * its 1,208 pixels near-black where the oracle puts white. It decomposes the
     * same way as everything else here -- the texel, then the constant over it by
     * the constant's alpha -- and the decomposition was written and measured on
     * 9 September 2026:
     *
     *     copyright screen    801 divergent  ->  3,828
     *     the race            124 divergent  ->  2,313
     *
     * **Four to eighteen times worse**, so it is not here. The reason is the
     * first of the two passes: it lays the texel down *opaque*, and these states
     * blend. Over an alpha-blended background the pair computes
     * `(T*a + dst(1-a))(1-k) + E*k` where the RDP computes
     * `(T(1-k) + E*k)*a + dst(1-a)`, and the difference is the whole background
     * showing through the wrong amount.
     *
     * It could be gated on an opaque first pass, where the two do agree. That was
     * not measured, so it is not written: a guard that makes a change apply to
     * nothing is indistinguishable from the change being absent, and this one has
     * no evidence yet that anything is left for it to apply to.
     *
     * **And the next attempt is here, by another route entirely.** It is not a
     * decomposition at all: the constant is carried in the *vertex* colour, so
     * that one Glide stage can express the whole cycle. See
     * `prepass_draw_env_in_vertex`. */
    if (e->rgb[0].a == (unsigned char)DKR_CC_ENVIRONMENT &&
        e->rgb[0].b == (unsigned char)DKR_CC_TEXEL0 &&
        e->rgb[0].c == (unsigned char)DKR_CC_ENV_ALPHA &&
        e->rgb[0].d == (unsigned char)DKR_CC_TEXEL0) {
        return PREPASS_TEXEL_ALONE;
    }
    return PREPASS_NONE;
}

static int prepass_wanted(const dkr_render_state *st)
{
    const dkr_cc_entry *e;
    int shape;

    if (!g_extra_passes) { return 0; }
    if (st->recipe <= 0 || st->recipe > dkr_cc_table_count()) { return 0; }
    e = dkr_cc_table_at(st->recipe - 1);
    shape = prepass_shape(e);
    if (shape == PREPASS_NONE) { return 0; }
    /* **Not while a cutout is in force.** Pass B has to put the *iterated* alpha
       in the alpha combiner, because that alpha is its blend factor -- and the
       alpha test reads the same output. A state that cuts holes by alpha would
       have them cut by the vertex alpha instead of the texel's, which is a
       different shape entirely. Refused and counted; the ordinary draw then
       applies, wrong in the old way rather than wrong in a new one.
       Measured: the states this shape appears in on the hub carry no alpha
       test, so the refusal costs nothing there and guards the case it cannot
       serve. */
    if (shape == PREPASS_PRIM_TO_TEXEL && st->alpha_test) {
        b.prepass_alpha_test++;
        return 0;
    }
    return shape;
}

static void pass2_geometry(const dkr_render_vertex *vertices, int count)
{
    int i;
    for (i = 0; i + 2 < count * 3; i += 3) {
        dkr_glide_draw_raw(&vertices[i], &vertices[i + 1], &vertices[i + 2]);
    }
}

/* --- The per-channel form, in two more passes -------------------------------- *
 *
 * `(ENV - COMBINED) * SHADE + COMBINED` is a lerp by the **vertex colour**, one
 * factor per channel. Glide's frame-buffer blender takes scalar alpha factors in
 * the place a lerp would want one, so a single pass cannot do it -- but the
 * identity
 *
 *     out = dst * (1 - shade) + ENV * shade
 *
 * splits into two blends that use only factors the blender has:
 *
 *     A:  src = shade,      src factor ZERO, dst factor ONE_MINUS_SRC_COLOR
 *     B:  src = ENV * shade, src factor ONE,  dst factor ONE
 *
 * A multiplies what is there by `1 - shade`; B adds the tint. Two extra passes
 * over 6,420 pixels on the hub -- two per cent of the frame -- which is why the
 * arithmetic is worth doing rather than approximating.
 *
 * **Why it matters more than its size.** That configuration is wrong on *every*
 * pixel it paints, and those pixels are a character the card renders as a black
 * silhouette. Fill says what a fix costs; this is what one buys.
 */
/* --- `(ENVIRONMENT - TEXEL0) * ENV_ALPHA + TEXEL0`, in one pass ---------------- *
 *
 * Glide's `BLEND` function is `(other - local) * factor + local`, and the RDP's
 * form rearranges onto it exactly:
 *
 *     (ENV - T) * k + T  =  (T - ENV) * (1 - k) + ENV
 *
 * so `other` = the texture, `local` = ENV, `factor` = `1 - k`. The obstacle the
 * catalogue records is that `k` is a **constant register's alpha**, and no factor
 * delivers one. The way out `gen_combiner_table.py` names is to carry the
 * environment in the **vertex** instead, where a factor reading the *local's*
 * alpha fetches `k` from the iterated alpha.
 *
 * That is what this draws, and it is free here because the configuration ignores
 * the vertex colour: its first cycle names TEXEL0 and ENVIRONMENT and nothing
 * else, so overwriting the shade costs nothing the RDP was using.
 *
 * ## What is measured, and what the measurement refuted
 *
 * The image, on the machine, twice: the copyright screen goes from **801**
 * divergent pixels to **17**, the race is unchanged at 124, `COMPARE.EXE` stays
 * clean. That much is solid and is why this is here.
 *
 * **The explanation above is not.** `combine_enum_probe.c` was extended to sweep
 * the factors with `other` driven from the **texture**, which is this
 * configuration and which the earlier sweeps did not cover. Factor `0x0B`, with a
 * vertex alpha of 200, reads as roughly 0.97 -- indistinguishable from the texel
 * colour as a factor, and nowhere near the 0.216 that `1 - local_alpha` would
 * give. So this pass is **not** fetching `k` from the iterated alpha; it is
 * drawing very nearly the texel, which is the RDP's first cycle at `k = 0`.
 *
 * And one measurement stands against that, unexplained rather than explained
 * away. The two settings were run back to back on one boot, with this pass's own
 * counter finally printed:
 *
 *     BLEND_OTHER / 0x0B     texel-alone drawn=20    17 divergent pixels
 *     SCALE_OTHER / ONE      texel-alone drawn=20   801 divergent pixels
 *
 * **Both fire, twenty times each**, so "the alternative never ran" is excluded --
 * and it was the likeliest explanation until the counter existed. The two differ
 * in nothing but the colour combine.
 *
 * If `0x0B` were the `ONE` the sweep reads it as, `BLEND_OTHER` with it would be
 * `other`, which is exactly what `SCALE_OTHER / ONE` computes, and the two images
 * would be the same. They are not. **So the sweep's reading does not transfer to
 * this configuration**, and neither does the 0.97 above: what this setting
 * actually computes on a font atlas is not known.
 *
 * A fourth sweep, with the texel's alpha at zero, eliminates the other obvious
 * candidate: `0x0B` reads the same 0.97 whether the texel's alpha is 0 or 255, so
 * "a font atlas has varying alpha and that is the difference" is out as well. The
 * same sweep does identify `TEXTURE_ALPHA` at 4 and `ONE_MINUS_TEXTURE_ALPHA` at
 * 12, which the generator had been assuming without measuring.
 *
 * What is known is the image, twice, and that every other setting tried is worse.
 *
 * **And what `0x0B` does is now measured — it is what it is named, under one
 * condition nobody had varied.** `constant_alpha_probe.c`, 15 September 2026:
 *
 *     blender off       the factor reads one, whatever any alpha is set to
 *     blender on        the factor is `1 - alpha`, to the level
 *
 * With `GR_BLEND_ONE / ONE` over black, a texel of 41 against a local of 255:
 *
 *     alpha    0 ->  41   (the texel: factor one)
 *     alpha  128 -> 148   (the lerp, halfway)
 *     alpha  255 -> 247   (the local: factor zero)
 *
 * and the arithmetic of `(T - L) x (1 - alpha) + L` matches every row. The first
 * sweeps read a flat 247 because they ran with blending **off**, which is how a
 * combiner is read directly - and that is exactly the case where this factor
 * degenerates to one.
 *
 * **Which alpha, though, is the one that decides it.** Holding the vertex alpha
 * at 255 and making the alpha unit deliver something else moves the colour with
 * the unit's output and not with the vertex:
 *
 *     the alpha unit delivers  0  51 102 153 204 255
 *     the colour comes back   41  82 123 165 206 247   (predicted 41..255)
 *
 * So the factor reads the **alpha combiner's output**. The vertex route works,
 * and this pass does compute the RDP's lerp - with `k` equal to whatever the
 * alpha unit hands over, which here is the texel's alpha.
 *
 * That also settles the copyright screen's 801 against 17, and it is not a
 * three-per-cent difference after all: with the blender on, `BLEND_OTHER / 0x0B`
 * puts the **environment** on a glyph whose texel alpha is 255 - which is what
 * the RDP does at `k = 1` - while `SCALE_OTHER / ONE` puts the texel there. The
 * text is white; the texel is not.
 *
 * **A two-pass decomposition would be exact and cannot be used here.** It needs
 * an opaque first pass to compose against, and this configuration's fill is 0 %
 * opaque on both scenes it appears in -- measured, and the reason that route was
 * tried, reverted, and is not tried again.
 */
#define PREPASS_BATCH 256

static int g_texel_factor_one;

void dkr_glide_backend_texel_factor_one(int on) { g_texel_factor_one = on; }

static void prepass_draw_texel_alone(const dkr_render_vertex *vertices,
                                     int count)
{
    static dkr_render_vertex tinted[PREPASS_BATCH * 3];
    const float er = (float)((b.current.env_color >> 16) & 0xFFu);
    const float eg = (float)((b.current.env_color >>  8) & 0xFFu);
    const float eb = (float)( b.current.env_color        & 0xFFu);
    const float ea = (float)((b.current.env_color >> 24) & 0xFFu);
    int done;

    if (!gs.color_combine || !gs.alpha_combine || !gs.blend_function) { return; }

    /* --- The switch that exists to settle a contradiction --------------------- *
     *
     * Replacing the pair below with `SCALE_OTHER / FACTOR_ONE` -- which the factor
     * sweep says computes almost the same thing, 1.00 against 0.97 -- was measured
     * once and put the copyright screen back from 17 divergent pixels to 801.
     * That was a whole rebuild ago, with the counter for this pass not yet
     * printed, so "the alternative is worse" and "the alternative never ran" were
     * indistinguishable.
     *
     * `dkr_glide_backend_texel_factor_one` selects between them at run time, so
     * that one boot can measure both with the pass counter visible. A
     * contradiction between two settings of the same renderer is not something to
     * leave in a comment. */
    if (g_texel_factor_one) {
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    } else {
        gs.color_combine(GR_COMBINE_FUNCTION_BLEND_OTHER,
                         GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    }
    /* --- The alpha stays the texel's, and that is measured, not chosen ------- *
     *
     * The iterated alpha now carries the lerp factor and is no longer the
     * vertex's own, so leaving the alpha combiner to read it would put that
     * factor into the alpha test.
     *
     * **What is missing here, and what putting it in cost.** This pass replaces
     * the ordinary draw, and the ordinary draw applies `alpha_scale` - the RDP's
     * alpha mux, `TEXEL0_ALPHA x PRIMITIVE_ALPHA` for this configuration. Taking
     * the texel's alpha alone hands the blender 255 where the mux says 102, and
     * the attract sequence's caption comes out opaque: 12,141 pixels of
     * `CAP0800` at a gap of 32 or more, 63 % of them in one band, the largest
     * disagreement E09-S02 has outside the dither floor.
     *
     * Supplying it was written and measured on 14 September 2026 - the constant
     * register is free here, and `constant_alpha_probe.c` had just shown the card
     * delivers `texel x constant_alpha` through `FACTOR_LOCAL / LOCAL_CONSTANT`.
     * The caption stopped being opaque and the scene got **worse**:
     *
     *     CAP0800, gap >= 32     12,141  ->  18,695
     *     of which newly wrong                7,657, all in the caption's band
     *
     * **And the reason is now measured, not guessed.** The colour combiner's
     * factor above is `1 - alpha`, and the alpha it reads is *this unit's
     * output* - the note before this function has the sweep. So the value
     * programmed here is two things at once: the blend factor the frame buffer
     * will use, and `k` in the lerp the colour unit computes. Scaling it to
     * carry `alpha_scale` scales the lerp by the same amount, the texel bleeds
     * into what should be flat environment colour, and 7,657 pixels of the
     * caption's band go wrong to buy 1,103 back.
     *
     * **One register, two meanings**: that is the whole of why this
     * configuration is hard on this card, and it is sharper than the
     * "approximate" the catalogue records. Carrying both needs either a second
     * pass - measured four to eighteen times worse on an alpha-blended
     * background, 9 September - or the scale folded into the texture's own
     * alpha, which nothing here does yet. The scale is therefore left out on
     * purpose, and the number it costs is written here so that the next attempt
     * starts with it. */
    gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    apply_blend(b.current.blend);
    apply_depth(b.current.depth);

    /* Copied in chunks rather than all at once: a batch is unbounded and this
       buffer is not, and a silent truncation would drop geometry. */
    for (done = 0; done < count; ) {
        const int n = (count - done > PREPASS_BATCH) ? PREPASS_BATCH
                                                     : (count - done);
        int i;
        for (i = 0; i < n * 3; i++) {
            tinted[i] = vertices[(done * 3) + i];
            tinted[i].r = er; tinted[i].g = eg; tinted[i].b = eb;
            tinted[i].a = ea;
        }
        for (i = 0; i + 2 < n * 3; i += 3) {
            dkr_glide_draw_raw(&tinted[i], &tinted[i + 1], &tinted[i + 2]);
        }
        done += n;
    }
    b.prepass_in_vertex++;
}

static void prepass_draw(const dkr_render_vertex *vertices, int count)
{
    if (!gs.color_combine || !gs.alpha_combine || !gs.blend_function) { return; }

    /* A: the constant, opaque. `constant_color` carries the register the colour
       mux named, which for this shape is `PRIMITIVE`. */
    if (gs.constant_color) { gs.constant_color(b.current.constant_color); }
    gs.color_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0);
    gs.alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0);
    gs.blend_function(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    /* Depth as the state asks: this pass stands in for the ordinary draw, so it
       is the one that may write. */
    apply_depth(b.current.depth);
    pass2_geometry(vertices, count);

    /* B: the texel over it, by the vertex alpha. */
    gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    gs.alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    gs.blend_function(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                      GR_BLEND_ONE, GR_BLEND_ZERO);
    if (gs.depth_function && b.current.depth != DKR_DEPTH_DISABLED) {
        gs.depth_function(GR_CMP_LEQUAL);
    }
    if (gs.depth_mask) { gs.depth_mask(0); }
    pass2_geometry(vertices, count);

    b.prepass_drawn++;
}

static void pass2_draw_by_shade(const dkr_render_vertex *vertices, int count)
{
    if (!gs.color_combine || !gs.alpha_combine || !gs.blend_function) { return; }

    /* **The alpha combiner is left exactly as the first pass set it**, and the
       first version's mistake was to touch it.
     *
       The blend factors below use no alpha, so the only thing the alpha still
       does here is feed the alpha *test* -- which is programmed from the state and
       cuts the same holes it cut for the first pass. Reprogramming it to "the
       texel's alpha" was an attempt to preserve that and did the opposite: where
       the configuration binds no texture, `OTHER_TEXTURE` is not the texel's alpha
       but whatever the unit holds, and an alpha test at `GEQUAL 1` then rejects
       the whole pass. Measured on 8 September 2026: the second pass drawn on 162
       batches and the character still black to the pixel.
     */
    if (gs.depth_function && b.current.depth != DKR_DEPTH_DISABLED) {
        gs.depth_function(GR_CMP_LEQUAL);
    }
    if (gs.depth_mask) { gs.depth_mask(0); }

    /* A: dst *= 1 - shade. The source is the iterated colour and contributes
       nothing of itself -- `ZERO` -- it is there to *be* the factor. */
    gs.color_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
    gs.blend_function(GR_BLEND_ZERO, GR_BLEND_ONE_MINUS_SRC_COLOR,
                      GR_BLEND_ONE, GR_BLEND_ZERO);
    pass2_geometry(vertices, count);

    /* B: dst += ENV * shade.
     *
       The product is written `other x local` with the constant as **local** and
       the iterated colour as **other**, rather than the other way round. It is
       the same product, and it uses only `GR_COMBINE_LOCAL_CONSTANT` and
       `GR_COMBINE_OTHER_ITERATED`, both of which this file already programs and
       has seen work. `GR_COMBINE_OTHER_CONSTANT` would do as well and has never
       been exercised on the card; there is no reason to spend an unverified
       enumeration value on a choice that is free. */
    if (gs.constant_color) { gs.constant_color(b.current.env_color); }
    gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                     GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_ITERATED, 0);
    gs.blend_function(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
    pass2_geometry(vertices, count);

    b.pass2_drawn++;
    b.pass2_by_shade++;
}

static void pass2_draw(const dkr_render_vertex *vertices, int count)
{
    int i;

    /* --- The constant carries `p x e`, and that is what makes the pair exact --- *
     *
     * The RDP computes, for this class of configuration,
     *
     *     out = [ C + (ENV - C) e ] a + dst (1 - a)
     *         = C a (1 - e)  +  ENV a e  +  dst (1 - a)
     *
     * with `C` the first cycle's colour, `a = t p` its alpha, `e` the
     * environment's alpha. The second line is two frame-buffer blends, and
     * neither needs a negative source:
     *
     *     pass 1   src = C (1 - e), alpha = a        SRC_ALPHA / ONE_MINUS_SRC_ALPHA
     *     pass 2   src = ENV,       alpha = a e      SRC_ALPHA / ONE
     *
     * Pass 1's `(1 - e)` is folded into the constant by `gl_set_state`, where the
     * CPU knows `e`; here the constant's alpha becomes `p x e`, so that the alpha
     * unit's `texel x constant` delivers `t p e` and the blender's `SRC_ALPHA /
     * ONE` **adds** `ENV (t p e)` to what is already there.
     *
     * What it replaces: `ENV (t e)` laid over the finished frame buffer with
     * `ONE_MINUS_SRC_ALPHA`, which darkened everything behind it by `1 - t e` and
     * cost the hub's sea foam 1,540 pixels - the corpus's second worst blob. */
    if (gs.constant_color) {
        const unsigned int env_alpha = (b.current.env_color >> 24) & 0xFFu;
        const unsigned int pe = ((unsigned int)b.current.alpha_scale * env_alpha)
                                / 255u;
        gs.constant_color((pe << 24) | (b.current.env_color & 0x00FFFFFFu));
    }
    /* Colour: the constant alone. `FUNCTION_LOCAL` outputs `local` and ignores
       `other` entirely, so `other` is given the value this file already uses and
       has seen work rather than `GR_COMBINE_OTHER_NONE`, whose numeric value
       would be written here from memory. This file has a record of what that
       costs: the texture-combine enumeration was found to be shifted by one from
       what had been written the same way. An unused argument is not worth an
       unverified constant. */
    if (gs.color_combine) {
        gs.color_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0);
    }
    /* Alpha: the texel's times the constant's. The constant's is the lerp factor;
       the texel's is what keeps the pass inside the first pass's coverage.
     *
       **Unless there is no texture**, in which case `OTHER_TEXTURE` is not the
       texel's alpha but whatever the unit happens to hold, and the alpha becomes
       the constant's alone -- which is the right answer for an untextured surface
       and the only one available. */
    if (gs.alpha_combine) {
        if (b.current.texture != 0) {
            gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                             GR_COMBINE_FACTOR_LOCAL,
                             GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE,
                             0);
        } else {
            gs.alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                             GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE,
                             0);
        }
    }
    /* **`ONE` on the destination, not `ONE_MINUS_SRC_ALPHA`.** This pass adds a
       term; it does not composite over what is there. The old factor is what made
       the pair wrong: it scaled the first pass's own result down again. */
    if (gs.blend_function) {
        gs.blend_function(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE,
                          GR_BLEND_ONE, GR_BLEND_ZERO);
    }
    /* `LEQUAL` and no write: the second pass sits at exactly the depth the first
       one left, and `LESS` -- which is what everything else uses -- would reject
       every pixel of it. Writing again would be harmless and is skipped because
       it is a write. */
    if (gs.depth_function && b.current.depth != DKR_DEPTH_DISABLED) {
        gs.depth_function(GR_CMP_LEQUAL);
    }
    if (gs.depth_mask) { gs.depth_mask(0); }

    for (i = 0; i + 2 < count * 3; i += 3) {
        dkr_glide_draw_raw(&vertices[i], &vertices[i + 1], &vertices[i + 2]);
    }
    b.pass2_drawn++;

    /* --- And the first pass's programming is put back, at once ---------------- *
     *
     * Clearing `has_state` so that the *next* `set_state` reprograms is not
     * enough, and the gap is the dangerous kind. The decoder calls `set_state`
     * before each batch today; a batch drawn without one -- and nothing in the
     * interface promises there will be one -- would render with the second pass's
     * combiner still loaded, which is a flat constant colour over the geometry.
     * A silent flat-colour object is exactly the class of defect this file keeps
     * a record of.
     *
     * So the state is re-applied here, through the same path that applied it in
     * the first place. Restoring five registers by hand is how one of them gets
     * forgotten. */
    {
        const dkr_render_state saved = b.current;
        b.has_state = 0;
        gl_set_state(0, &saved);
    }
}

static void gl_draw_triangles(void *self, const dkr_render_vertex *vertices,
                              int count)
{
    const unsigned long prepass_before = b.prepass_drawn;
    int i;
    (void)self;
    if (!vertices || count <= 0) { return; }
    /* `dkr_render_vertex` has `GrVertex`'s layout, field for field —
       `backend_layout_check.c` checks it at compile time. The hand-off therefore
       needs no conversion and no copy, which was the whole point of this
       layer. */
    /* The pre-pass **replaces** the ordinary draw: it is the first cycle, done in
       two blends because one stage cannot hold it. Drawing both would lay the
       constant over the result and waste the fill doing it. */
    {
        /* Asked **once**: the guard counts its refusals, and calling it twice
           would count them twice. A counter that inflates with the shape of the
           code around it is worse than no counter. */
        const int shape = b.has_state ? prepass_wanted(&b.current) : PREPASS_NONE;
        if (shape == PREPASS_PRIM_TO_TEXEL) {
            prepass_draw(vertices, count);
        } else if (shape == PREPASS_TEXEL_ALONE) {
            prepass_draw_texel_alone(vertices, count);
        } else {
            for (i = 0; i + 2 < count * 3; i += 3) {
                dkr_glide_draw_raw(&vertices[i], &vertices[i + 1],
                                   &vertices[i + 2]);
            }
        }
    }
    b.triangles += (unsigned long)count;
    if (b.has_state) {
        const int kind = pass2_wanted(&b.current);
        if (kind == PASS2_BY_ENV_ALPHA) {
            pass2_draw(vertices, count);
        } else if (kind == PASS2_BY_SHADE) {
            pass2_draw_by_shade(vertices, count);
            /* The same restoration, and for the same reason: see `pass2_draw`. */
            {
                const dkr_render_state saved = b.current;
                b.has_state = 0;
                gl_set_state(0, &saved);
            }
        } else if (b.prepass_drawn != prepass_before) {
            /* A pre-pass with no second cycle to follow still left the registers
               where it put them. Same restoration, same reason. */
            const dkr_render_state saved = b.current;
            b.has_state = 0;
            gl_set_state(0, &saved);
        }
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
    /* --- And the depth **state** has to say so too --------------------------- *
     *
     * The three fields above were the whole of "depthless", and they are only
     * half of it: they place the rectangle at the nearest depth, they do not
     * stop it being written there. With the depth mask open -- which it is for
     * seventy-nine per cent of what DKR draws -- a full-screen clear stamps the
     * entire buffer at `w = 1`, and every triangle of the frame that follows is
     * farther and fails `GR_CMP_LESS`.
     *
     * That is the whole of the black 3D frames. Measured on 21 August 2026:
     * 237 of 254 triangles have their centroid on screen, 17 of them cover more
     * than ten thousand pixels each, the combiner is forced to shade so no
     * texture is involved, and 1,687 pixels come back painted. Forcing the
     * triangles to carry the rectangle's own `oow` of 1 made the frame
     * **entirely** black, which is the same defect turned up to the point of
     * being unmistakable: at equal depth a strict `LESS` rejects everything
     * after the first writer, and the first writer is the clear. */
    /* **Through `gl_set_state`, not around it.**
     *
     * The first version programmed `grDepthMode` and `grDepthMask` here and set
     * `b.has_state = 0`. That reaches the card, and it leaves the block the
     * backend believes is loaded out of step with the one that is -- while the
     * *decoder* still thinks its own state current, so `apply_state` returns
     * early and never pushes again.
     *
     * The measurement that named it: giving every triangle the same state block
     * a second time, immediately before drawing it, filled the screen. The block
     * was not different -- 218 triangles opaque, shade, depth test-and-write,
     * measured per frame -- it was simply *pushed*. What was wrong was a cache
     * claiming to describe a card that something else had reprogrammed.
     *
     * So the rectangle takes the current block, turns depth off in it, and goes
     * through the same door as everything else; `b.current` stays truthful, and
     * the block is put back afterwards. */
    {
        const dkr_render_state saved = b.current;
        const int had = b.has_state;
        dkr_render_state st = saved;
        st.depth = DKR_DEPTH_DISABLED;
        gl_set_state(self, &st);
        gl_draw_triangles(self, v, 2);
        if (had) { gl_set_state(self, &saved); }
        return;
    }
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
                         ? GR_TEXFMT_INTENSITY_8
                     : (desc->format == DKR_TEXFMT_ALPHA_INTENSITY88)
                         ? GR_TEXFMT_ALPHA_INTENSITY_88
                     : (desc->format == DKR_TEXFMT_ARGB4444)
                         ? GR_TEXFMT_ARGB_4444
                         : GR_TEXFMT_ARGB_1555;
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
    /* --- The two caches deadlocked each other ------------------------------- *
     *
     * A slot was cleared only when a later allocation's range **overlapped** it.
     * That is a rule with a hole in it, and the hole closes on itself: once all
     * 512 slots are live the upload is refused **before** `dkr_tmu_acquire` is
     * ever called, so no allocation happens, so nothing is evicted, so no range
     * overlaps, so no slot is ever cleared again. The table is full for good and
     * every texture from then on is a surface drawn without one.
     *
     * Measured on 25 August 2026 over 840 lists: `refusal-detail slots=12685`
     * against `tmu-memory=0`. Nothing was short of room on the card.
     *
     * The first attempt was to reclaim slots whose key the allocator no longer
     * held. It reclaimed **zero**, and the zero is the proof: none had been
     * evicted, because the refusal is what stops eviction happening. A cache
     * that refuses before consulting the one below it cannot be repaired by
     * consulting the one below it.
     *
     * So the table evicts on its own terms, least-recently-used, by a clock the
     * bindings advance. The allocator keeps the texture resident; if the key
     * comes back it is a hit and costs no download. A slot the current state
     * still names is the most recently bound, so it is the last candidate — and
     * if it were taken anyway, `bind_texture` refuses a dead handle and counts
     * it, which is a visible degradation rather than a wrong texture. */
    if (slot < 0) {
        int oldest = -1;
        for (i = 0; i < GLIDE_MAX_TEXTURES; i++) {
            if (!g_tex[i].live) { continue; }
            if (oldest < 0 || g_tex[i].last_used < g_tex[oldest].last_used) {
                oldest = i;
            }
        }
        if (oldest >= 0) {
            g_tex[oldest].live = 0;
            g_tex_reclaimed++;
            slot = oldest;
        }
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
    /* --- An upload invalidates the binding, and the block cannot say so ------ *
     *
     * `gl_set_state` short-circuits when the render-state block is byte for byte
     * the one the card holds, and the block names a texture by **handle**. The
     * handle survives a re-upload; the TMU address does not. So a texture
     * re-uploaded to a different address under the same handle leaves
     * `grTexSource` pointing at the old one, and the surface samples whatever
     * has since been written there.
     *
     * The port re-uploads almost everything -- 1,067 uploads against 15 reuses
     * in a run -- so addresses churn constantly while handles repeat, which is
     * the worst case for this. Invalidating here costs one full state
     * programming per upload and removes a class of "a piece of scenery wearing
     * another's pattern" that the descriptor table was already fixed once for.
     *
     * Same discipline as `gl_fill_rect`: whatever changes the card behind the
     * cache's back must tell the cache. */
    b.has_state = 0;
    g_tex[slot].bytes   = bytes;
    g_tex[slot].info    = info;
    g_tex[slot].info.data = 0;   /* the pixels do not belong to us */
    g_tex[slot].last_used = ++g_tex_clock;
    g_tex[slot].live    = 1;
    return (dkr_texture_handle)(slot + 1);
}

/* --- Residency, asked before converting rather than after -------------------- *
 *
 * The contract and the measurement that asks for it are in `backend.h`. What is
 * decided here is what counts as a hit, and it is deliberately strict: the
 * descriptor slot must be live, carry this key, sit on this unit, **and** the
 * allocator below must still hold the key. Two tables, two chances to be stale,
 * and a wrong answer here does not fail — it draws a surface with another
 * texture's pattern, in a place that depends on the upload order. That symptom
 * has cost this port two separate investigations already.
 *
 * A hit refreshes the recency in both tables. `bind_texture` will advance the
 * descriptor clock again when the draw comes, which is harmless; the allocator's
 * is advanced only here, and `dkr_tmu_touch` exists for it. */
static dkr_texture_handle gl_texture_lookup(void *self, unsigned long long key,
                                            int tmu)
{
    int i;
    (void)self;
    if (g_tmu_count == 0) { return 0; }
    if (tmu < 0 || tmu >= g_tmu_count) { tmu = 0; }
    for (i = 0; i < GLIDE_MAX_TEXTURES; i++) {
        if (!g_tex[i].live) { continue; }
        if (g_tex[i].key != key) { continue; }
        if (g_tex[i].tmu != (unsigned char)tmu) { continue; }
        /* The allocator is the sole judge of what resides where -- the same rule
           the descriptor table already follows on upload. If it no longer holds
           the key, the slot is stale and says so by being cleared, rather than
           by handing back an address the TMU has reassigned. */
        if (dkr_tmu_touch(&g_tmu[tmu], key) == DKR_TMU_NONE) {
            g_tex[i].live = 0;
            g_tex_lookup_stale++;
            return 0;
        }
        g_tex[i].last_used = ++g_tex_clock;
        g_tex_lookup_hits++;
        return (dkr_texture_handle)(i + 1);
    }
    g_tex_lookup_misses++;
    return 0;
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
    /* The binding is what makes a handle recent. Without this the table's
       least-recently-used would mean "least recently *uploaded*", and the
       texture drawn on every triangle of the frame would be the first one
       thrown out. */
    tx->last_used = ++g_tex_clock;
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
    /* Zeroed first -- this factory already did, and the software and null ones
       did not. See `software.c` for what that cost on 3 September 2026. */
    memset(out, 0, sizeof(*out));
    out->self            = &b;
    out->name            = "3dfx Glide";
    out->open            = gl_open;
    out->close           = gl_close;
    out->begin_frame     = gl_begin_frame;
    out->present         = gl_present;
    out->set_state       = gl_set_state;
    out->set_scissor     = gl_set_scissor;
    out->invalidate      = gl_invalidate;
    out->draw_triangles  = gl_draw_triangles;
    out->fill_rect       = gl_fill_rect;
    out->texture_upload  = gl_texture_upload;
    out->texture_release = gl_texture_release;
    out->texture_lookup  = gl_texture_lookup;
}

unsigned long dkr_glide_backend_triangle_count(void)
{
    return b.triangles;
}

void dkr_glide_backend_symbols(unsigned long *total, unsigned long *missing,
                               const char *const **names, unsigned int *n)
{
    if (total)   { *total = b.symbols_total; }
    if (missing) { *missing = b.symbols_missing; }
    if (names)   { *names = b.symbol_first_missing; }
    if (n)       { *n = b.symbol_first_missing_n; }
}

unsigned long dkr_glide_backend_two_layer_states(void)
{
    return b.two_layer_states;
}

void dkr_glide_backend_pass2_stats(unsigned long *drawn, unsigned long *identity,
                                   unsigned long *unsupported,
                                   unsigned long *blend,
                                   unsigned long *by_shade)
{
    if (drawn)       { *drawn       = b.pass2_drawn; }
    if (identity)    { *identity    = b.pass2_identity; }
    if (unsupported) { *unsupported = b.pass2_unsupported; }
    if (blend)       { *blend       = b.pass2_blend; }
    if (by_shade)    { *by_shade    = b.pass2_by_shade; }
}

void dkr_glide_backend_prepass_stats(unsigned long *drawn,
                                     unsigned long *refused_alpha_test,
                                     unsigned long *texel_alone)
{
    if (drawn)              { *drawn              = b.prepass_drawn; }
    if (refused_alpha_test) { *refused_alpha_test = b.prepass_alpha_test; }
    /* Its own counter, and it was missing. Without it, a run where this pass
       never fired and a run where it fired and did the wrong thing produce the
       same log -- which is how one contradiction spent a day in a comment. */
    if (texel_alone)        { *texel_alone        = b.prepass_in_vertex; }
}

void dkr_glide_backend_bind_stats(unsigned long *binds,
                                  unsigned long *dead,
                                  unsigned long *changed)
{
    if (binds)   { *binds   = b.binds; }
    if (dead)    { *dead    = b.binds_dead; }
    if (changed) { *changed = b.binds_changed; }
}

/* Forget what the card is believed to hold, without changing what it is asked
   to hold.
 *
 * The whole remaining question in one call. `DKR_FORCE_STATE` pushes a block
 * that *differs*, so `gl_set_state` reprograms in full instead of
 * short-circuiting, and the painted surface goes from 1,700 pixels to 120,000 --
 * but that changes the values too, so it cannot say which of the two matters.
 * Invalidating the cache and pushing **the same block** separates them: same
 * values, full reprogramming. */
static void gl_invalidate(void *self)
{
    (void)self;
    b.has_state = 0;
}

/* Whether the entry points the drawing path needs are resolved at all. The
   triangle count above has existed all along and was never read; this says
   whether a zero would mean "nothing drawn" or "nothing could be drawn". */
int dkr_glide_backend_can_draw(void)
{
    return gs.color_combine != 0 && gs.alpha_combine != 0;
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
