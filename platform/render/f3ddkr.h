/* E04-S02 — F3DDKR display-list decoder, independent of RT64.
 *
 * `F3DDKRRT64Bridge` already knows how to decode Rare's microcode, and that work
 * is valuable: it is validated by a port that runs. This module takes its logic
 * — which belongs to the microcode and has nothing to do with RT64 — and rests
 * it on the E04-S01 rendering interface.
 *
 * The command map lives in `docs/research/f3ddkr-commands.md`.
 *
 * ## What had to survive the extraction
 *
 * **Range validation.** The original decoder checks every range before using it
 * and rejects invalid data with a bounded error, rather than letting host memory
 * be addressed. An extraction that lost that discipline would trade a safe
 * decoder for a decoder that is quick to write.
 *
 * It protects against two different things: a modified ROM, and a bug in the
 * port. The second is the more likely.
 *
 * ## What this module does not do
 *
 * It does not transform vertices (E04-S03), does not clip (E04-S05), does not
 * decode textures (E04-S07). It reads the display list, validates, keeps the
 * microcode's state, and calls the rendering interface.
 *
 * It does **emit**, however, now that E04-S03 and E04-S05 exist: every triangle
 * goes through transformation, near-plane clipping, projection and back-face
 * culling before reaching the backend. That is the complete chain, and the only
 * assembly that proves the five modules fit together.
 */
#ifndef DKR_RENDER_F3DDKR_H
#define DKR_RENDER_F3DDKR_H

#include "backend.h"
#include "clip.h"
#include "rdp_state.h"
#include "texture.h"
#include "transform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- The reasons for rejection --------------------------------------------- *
 *
 * Told apart because they are not diagnosed the same way: an address outside
 * RDRAM suggests a wrong DMA base, a vertex index outside the cache suggests a
 * corrupt display list or a missed command. */
typedef enum {
    DKR_F3D_REJECT_ADDRESS = 0,   /* range outside the 8 MiB of RDRAM */
    DKR_F3D_REJECT_COUNT,         /* count of zero or beyond the limit */
    DKR_F3D_REJECT_INDEX,         /* vertex index outside the 32-entry cache */
    DKR_F3D_REJECT_DEPTH,         /* nested display-list stack full */
    DKR_F3D_REJECT_OPCODE,        /* unknown opcode */
    DKR_F3D_REJECT_COUNT_MAX
} dkr_f3d_reject;

const char *dkr_f3d_reject_text(dkr_f3d_reject r);

/* --- The decoder's state --------------------------------------------------- */
typedef struct {
    /* Addressing bases from `DMAOffsets` — the central mechanism of Rare's
       microcode. A wrong base does not crash: it produces entirely absurd
       geometry, which is far harder to diagnose. */
    unsigned int matrix_offset;
    unsigned int vertex_offset;

    unsigned int selected_matrix;    /* 0..2 */
    unsigned char billboard;
    /* Addressing base for texture loading, **and not a pair of s and t
       offsets** — found in the neighbouring port, see `f3ddkr.c`. */
    unsigned int texture_offset;
    unsigned int texture_shift;
    unsigned int texture_count;

    /* Counts, for trace mode and for the tests. */
    unsigned long commands;
    unsigned long triangles;
    unsigned long vertices;
    unsigned long rejects[DKR_F3D_REJECT_COUNT_MAX];

    /* What the chain actually handed to the backend, as opposed to what the
       display list asked for. The gap between `triangles` and `emitted` is the
       number discarded — by clipping, culling or off-screen rejection — and it
       is a figure one wants to see: an empty screen with a high `triangles` and
       a zero `emitted` points straight at this stage. */
    unsigned long emitted;
    unsigned long culled;
    unsigned long clipped_away;
    unsigned long clip_split;      /* triangles that became two */

    /* Commands recognised but whose effect is not wired up yet — geometry
       modes, RDP state, textures. Counted separately from `commands` because
       this figure answers a different question: not "is the sequence right" but
       **how much of the image is still ignored**. It is the measurement that
       will be missed most when the scenery comes out wrong rather than
       absent. */
    unsigned long deferred;

    /* --- The 2D state, the one the startup sequence exercises ---------------- *
     *
     * Measured before being written: across the 47,000 commands of startup, the
     * only draw order emitted is `FILLRECT`. These three fields are therefore
     * what the first pixel this port displays depends on. */
    unsigned int  fill_color_raw;     /* the SETFILLCOLOR word, as it is */
    unsigned int  fill_color_argb;    /* and its conversion, for the backend */
    unsigned int  color_image_width;  /* the buffer's width, read and not assumed */
    /* And which buffer. DKR points the RDP at more than one, and a fill aimed
       at the depth buffer painted the visible frame for want of this. Both are
       kept **unmasked**: the two buffers share their low twenty-four bits, so
       masking to RDRAM makes them indistinguishable. */
    unsigned int  color_image_address;
    unsigned int  depth_image_address;   /* G_SETZIMG, 0xFE */
    unsigned long fills_to_depth;        /* skipped, and counted rather than mute */
    /* Rectangles that went out through the combiner because the cycle was not
       FILL -- DKR's screen fades. Counted apart from `rects`: they are drawn by
       a different path and a single total would hide which one is at work. */
    unsigned long blend_rects;
    unsigned long scissors;              /* G_SETSCISSOR, now honoured */
    unsigned long rects;              /* rectangles actually handed to the backend */

    /* --- The RDP state, and what it costs in fidelity ----------------------- */
    unsigned long states_applied;    /* translations actually handed to the backend */
    /* **Approximate** translations. `rdp_state.h` insists: an approximation that
       does not announce itself is worse than a failure, because it produces a
       plausible, wrong image. This counter is that safety net. */
    unsigned long states_approximate;
    /* Fills that happened outside `FILL` mode. The RDP only fills in FILL mode;
       any other value accuses the partial write of the mode word, hence the
       shift — and says so in figures rather than on screen. */
    unsigned long fills_wrong_cycle;
    unsigned char current_cycle;
    /* Viewports installed by the game. Zero means we are still drawing with the
       default, hence at an invented scale. */
    unsigned long viewports;

    /* --- Textures ----------------------------------------------------------- */
    dkr_texture_stats textures;           /* converted, refused, out of bounds */
    unsigned long     textures_loaded;    /* handed to the backend */
    unsigned long     textures_reused;    /* served from the cache */
    unsigned long     textures_refused;   /* texture memory full */
    /* Padded up to the next power of two, which the Voodoo requires and the N64
       does not. */
    unsigned long     textures_padded;
    /* Refused for a ratio beyond 8:1, which padding cannot fix without
       multiplying memory by eight. */
    unsigned long     textures_bad_aspect;
    /* The extremes of the normalised coordinates. They must stay in the
       neighbourhood of [0,1]; thousands would say the scale is wrong. The
       measurement exists to be able to contradict the 10.5 format
       interpretation, not to confirm it. */
    float             s_min, s_max, t_min, t_max;
    /* Corners whose s and t both land inside [0,1], against those that do not.
       The extremes above give a range and not a distribution, and the two
       readings call for opposite work. */
    unsigned long     st_inside, st_outside;
    /* Triangles emitted, broken down by combiner mode and by whether a texture
       was bound. "Emitted" on its own conflates three distinct causes of a white
       surface; these two counters separate two of them. */
    unsigned long     emitted_per_combine[DKR_COMBINE_COUNT];
    unsigned long     emitted_textured;
    /* Triangles emitted by order of magnitude of screen area: under one pixel,
       under a hundred, under ten thousand, beyond. A distribution dominated by
       the last bucket accuses the projection or the matrices; a normal
       distribution says the geometry is right. */
    unsigned long     area[4];
    /* Triangles emitted by depth mode. A missing sort produces exactly the
       observed image: the last large polygon covers everything. */
    unsigned long     emitted_per_depth[4];
    /* The range of depths handed to the card. Glide in W-buffer mode consumes
       `oow` as it is; degenerate values give a black screen with no comparison
       convention being at fault. */
    float             oow_min, oow_max;
    /* Blending and the alpha test. Three causes can blacken a screen — depth,
       blending, alpha threshold — and conflating them makes one fix the wrong
       one. */
    unsigned long     emitted_per_blend[8];
    unsigned long     emitted_alpha_test;
    unsigned          alpha_ref_max;
    /* The maximum colour and alpha reached by an emitted vertex. A zero shade
       multiplies the texel by zero: that is black, whatever the texels. */
    float             shade_max, alpha_max;
    /* Textures entirely black after conversion, against those that carry
       something. The texel is the last combiner input we had not looked at. */
    unsigned long     textures_black, textures_with_content;
    /* Textures whose every texel is the same value, against those that carry an
       image. `textures_black` only ever asked whether everything was zero, so a
       uniformly dark grey texture counted as content — which is exactly the
       state the flat 3D frames are in. The first uniform one is kept whole:
       its texel, its padded size and its format say whether the colour on
       screen is that texel and where the conversion lost the image. The texel
       carries bit 16 as a "recorded" marker, so that a legitimate value of zero
       is not mistaken for an empty slot. */
    unsigned long     textures_uniform, textures_varied;
    unsigned int      uniform_sample_texel;
    short             uniform_sample_w, uniform_sample_h;
    unsigned int      uniform_sample_format;
    /* The combiner configurations, catalogued or not. `rdp_state.h` insists: a
       missing case is invisible at decode time, it shows on screen as an
       unexpected colour, possibly in a single level. We keep the keys rather
       than merely their count — a count says some are missing, not which. */
    /* Triangles whose three corners carry the **same** s,t: they sample one
       texel whatever the texture, which a run-wide extreme cannot distinguish
       from a healthy spread. See the note at the counting site. */
    /* The largest emitted triangle, in pixels. The top area bucket spans from
       ten thousand to the whole screen; those are different findings. */
    /* Extremes of the **projected** screen coordinates, in pixels, over the
       list. The area buckets say a triangle is huge; these say by how much and
       in which direction, which is what separates "a background quad extends
       past the viewport", which is normal, from "the projection scale is
       wrong", which is not. Stored as longs: the values reach the guard band
       and beyond, so a short would wrap and read as healthy. */
    /* The normalised device coordinates, before any clipping. On-screen
       geometry belongs in [-1, 1]; this is what the guard band was hiding. */
    /* Vertices landing inside a generous [-1.5, 1.5] box against those outside.
       The extremes alone cannot separate "all the geometry is oversized" from
       "a few vertices are wild", and those are different defects. */
    /* Textured rectangles: decoded, and handed to the backend. Two numbers,
       because their being one was what hid `G_TEXRECT` being skipped for two
       days -- `texrect` came from the raw opcode histogram, which rises whether
       or not the command draws anything, and `fillrect` had a `handed-over`
       beside it while this had none. */
    /* The first few rectangles, verbatim. The glyphs overlap on screen and the
       two candidate causes are a pixel apart: `lrx` inclusive, as `cmd_fill_rect`
       takes it, or exclusive. Adjacent letters abutting with no gap says the
       `+1` is one too many; a gap says it is right and the fault is elsewhere.
       Sixteen: six showed the letters of KRUNCH matching BigFont exactly, and
       the doubling therefore lies beyond them - thirty rectangles for a
       six-letter name. */
    short              rect_sample[16][4];
    unsigned long      rect_sample_n;
    /* The render state the first textured rectangle was drawn with: combine,
       blend, alpha test, alpha reference. The game's own glyph rectangles
       overlap by two or three pixels - measured - so the padding around each
       letter must be masked by transparency. If these say opaque with no alpha
       test, the boxes paint over each other and that is the crowding on screen. */
    unsigned char      rect_state[4];
    unsigned char      rect_state_seen;
    /* The texture the first textured rectangle sampled, and the span it asked
       for. Three hypotheses about the crowded glyphs have each cost a run -- the
       lower-right +1, the blend state, the intensity alpha -- and none of them
       began by asking which conversion path these textures even take. Format,
       real and padded size, and the s range together say whether a glyph
       samples its own cell or overruns into the next. */
    unsigned int       rect_tex_format;
    short              rect_tex_w, rect_tex_h;
    short              rect_tex_pw, rect_tex_ph;
    int                rect_s0_1000, rect_s1_1000;
    unsigned long      texrects_seen;
    unsigned long      texrects_drawn;
    unsigned long      texrects_no_texture;
    /* Which opcode actually carried the two half-words. `gbi.h` computes
       `G_RDPHALF_1 = 0xB3` from `G_IMMFIRST - 12`; this decoder's own comment
       claims the machine answered `0xB4`. They cannot both be right, so the
       halves are captured by position -- the microcode emits them adjacently by
       construction -- and the opcodes seen are recorded rather than assumed. */
    unsigned char      texrect_half_opcode[2];
    unsigned long      ndc_inside, ndc_outside;
    float              ndc_x_min, ndc_x_max;
    float              ndc_y_min, ndc_y_max;
    long               proj_x_min, proj_x_max;
    long               proj_y_min, proj_y_max;
    unsigned long      area_max;
    /* --- Who painted the flat frame ----------------------------------------- *
     *
     * The 3D frames come back as one colour over the whole screen -- four
     * distinct values, and they are the Voodoo's 4x4 dither of a single grey,
     * not four bands. Two candidates can do that, and every aggregate the state
     * already carries counts both the same way: a full-screen `G_FILLRECT`, or
     * one triangle large enough to cover everything drawn before it.
     *
     * So each is recorded verbatim, once per frame, and one run separates them.
     * Guessing costs ten minutes a run; these two arrays cost sixty bytes. */
    short              fill_sample[8][4];      /* x0,y0,x1,y1 in screen pixels */
    unsigned int       fill_sample_color[8];
    unsigned int       fill_sample_target[8];   /* the colour image it went to */
    unsigned long      fill_sample_after[8];    /* triangles emitted before it */
    unsigned long      fill_sample_n;
    /* The vertices of the largest triangle handed over, with the colour of its
       first vertex and the state it went out under. `area_max` says how big the
       worst one is; this says where it is and what it looks like. */
    short              big_tri[3][2];
    unsigned int       big_tri_color;
    unsigned char      big_tri_state[3];       /* combine, blend, depth */
    /* --- The paint stack of one pixel --------------------------------------- *
     *
     * Every aggregate so far -- areas, fills, the largest triangle, the matrix --
     * answered truthfully and left the question open, because each summarises
     * over the frame and the frame's problem is an **order**. Turning the depth
     * test off made the screen black rather than legible: so the scene is not
     * hidden behind a comparison, it is painted over by whatever comes last.
     *
     * So one pixel is followed instead of the frame. Every triangle covering the
     * centre of the screen is recorded in submission order -- its ordinal, its
     * area, its state and its vertex colour -- and the last sixteen are printed.
     * That is the stack of paint on that pixel, and it names what erases the
     * scene rather than describing what the result looks like.
     *
     * Three cross products per triangle, against two hundred triangles a list. */
    unsigned char      center_state[16][4];    /* combine, blend, depth, textured */
    unsigned int       center_rgb[16];
    unsigned long      center_area[16];
    unsigned long      center_ordinal[16];
    unsigned long      center_hits;
    /* Emitted triangles whose centroid lands inside the viewport, against those
       whose does not. The projected extremes clamp to the guard band and the
       area counts guard-band pixels, so neither can say where the geometry is. */
    unsigned long      tri_on_screen, tri_off_screen;
    unsigned long      tri_st_degenerate;
    unsigned long      tri_st_varying;
    unsigned long      combiners_known;
    unsigned long      combiners_unknown;
    unsigned long long unknown_keys[8];
    unsigned           unknown_keys_n;
    /* The composition of each unknown configuration, without which the key only
       lets one note the gap, not fill it. */
    dkr_combiner       unknown_combiners[8];
    unsigned char      unknown_cycle[8];

    /* How many times each opcode was seen.
     *
     * A thousand bytes to answer a question no amount of reasoning settles:
     * **what is a DKR frame made of?** Without it one decides what to implement
     * from an opcode table, that is, from what the microcode *can* emit rather
     * than what this game *does* emit. The two have already diverged once this
     * session, on the low bound of the F3D family. */
    unsigned long opcodes[256];
} dkr_f3d_state;

/* --- The context ----------------------------------------------------------- */
typedef struct {
    const unsigned char *rdram;      /* RDRAM snapshot, `rdram_size` bytes */
    unsigned int         rdram_size;
    /* Byte layout inside `rdram`. Zero — the default — describes the console's
       plain big-endian, the one the tests build. One means librecomp's
       **XOR-3 interleaved** layout, that of the snapshot the game hands to the
       graphics thread.
     *
       The flag exists because the two are indistinguishable on inspection: a
       display list read with the wrong convention does not crash, it decodes
       plausible opcodes at absurd addresses. We reject them, we count the
       rejections, and we suspect the decoder. */
    unsigned char        rdram_native;
    dkr_render_backend  *backend;    /* may be NULL: we decode without drawing */
    dkr_f3d_state        state;

    /* The chain. `transform` carries the matrices and the viewport; `cache`
       holds the microcode's 32 vertices, **already transformed into homogeneous
       space**.
     *
       Transforming them on load rather than at the triangle is not a free
       optimisation: a vertex served by three triangles would otherwise be
       transformed three times, and transformation is the port's heaviest stage
       (0.682 us per vertex, measured). Texture coordinates, on the other hand,
       do arrive with the triangle — that is how the microcode works. */
    dkr_transform        transform;
    dkr_clip_vertex      cache[32];
    unsigned char        cache_valid[32];
    /* Where an appended vertex batch starts. `gSPVertexDKR` carries an append
       flag, not a destination: a flag-0 load writes at the beginning of the
       array and stores its count, a flag-1 load writes after it. This is that
       stored count. */
    unsigned int         vertex_base;
    dkr_render_state     render_state;

    /* The RDP's other-mode word, accumulated through partial writes, and the
       combiner. They live in the context and not in the state because they are
       the decoder's working memory, not a measurement. */
    unsigned int         mode_h;
    unsigned int         mode_l;
    /* `G_SETPRIMCOLOR` and `G_SETENVCOLOR`, 0xAARRGGBB. Held here rather than in
       the RDP state because they are set by their own commands and must survive
       every state translation, exactly like the texture handle. */
    unsigned int         prim_color;
    unsigned int         env_color;
    unsigned char        prim_lod_min, prim_lod_frac;
    dkr_combiner         combiner;
    unsigned char        state_dirty;
    /* Forces depth off, to isolate sorting from a rendering defect. Set by the
       caller; zero by default. */
    unsigned char        no_depth;
    /* Forces every draw to one combine mode, to bisect what the image owes to
       what. The frame brought back on 17 August 2026 is a uniform grey over a
       **black** clear, so a quarter of a million triangles are painting and all
       coming out one colour; forcing the mode says in one run per notch which
       input carries it.
     *
       Already measured: `DKR_COMBINE_SHADE` gives a frame of a single colour,
       `#000000`, the clear -- with the vertex colour alone nothing is written at
       all.
     *
       `force_combine` is the mode plus one, so that zero keeps meaning "do not
       force" and `DKR_COMBINE_SHADE`, which is zero, stays reachable. A boolean
       per mode was the first shape and it does not scale past two. */
    unsigned char        force_combine;
    /* `DKR_SCISSOR=1`. The command is decoded and counted either way; this says
       whether the clip window reaches the card. See the note in `f3ddkr.c`. */
    unsigned char        scissor_enabled;
    /* `DKR_FLATTEN_W=1`. Forces every emitted triangle to carry a rectangle's
       depth values -- oow 1, z 0, ooz 0 -- which is the one difference between
       the geometry that does not paint and the rectangles that do. */
    unsigned char        flatten_w;
    /* `DKR_PAINT_WHITE=1`. Every emitted vertex opaque white, so that "does not
       rasterise" and "rasterises black on black" stop looking alike. */
    unsigned char        paint_white;

    /* --- A textured rectangle in flight ------------------------------------ *
     *
     * `G_TEXRECT` carries its texture coordinates in the two commands that
     * follow it, so the rectangle cannot be drawn when its opcode arrives. It
     * waits here until both halves have been seen. `pending` counts how many
     * halves are still owed: 2 after the opcode, 0 when the rectangle is
     * complete and drawn. */
    unsigned int         texrect_pending;
    int                  texrect_ulx, texrect_uly;
    int                  texrect_lrx, texrect_lry;
    unsigned char        texrect_flip;
    float                texrect_s, texrect_t;
    float                texrect_dsdx, texrect_dtdy;

    /* The resolution the backend actually opened. The decoder needs it to carry
       the game's buffer to the screen, and deducing it from the current viewport
       would stop working as soon as it replaces that viewport. */
    unsigned int         screen_width;
    unsigned int         screen_height;

    /* The current texture image, as `SETTIMG` describes it. It is not enough to
       load with: the dimensions come from `SETTILESIZE`, later. */
    unsigned int         timg_address;
    unsigned int         timg_format;
    unsigned int         timg_size;
    /* The currently bound texture, by its key. Zero means none. */
    unsigned long long   texture_key;
    /* The current handle. It lives here and not in `render_state` because
       translating the RDP state rewrites that block in full: the handle would be
       overwritten on every application, which is exactly what was happening. */
    dkr_texture_handle   bound_texture;
    /* The conversion buffer. 256x256 in 5551: 128 KiB, carried by the context
       rather than allocated per texture — a Pentium II cannot afford a malloc
       per texture change, and there are thousands of them per second. */
    unsigned short       texels[256 * 256];
    /* The real dimensions and the padded ones. Their ratio serves the texture
       coordinates: the real texture only occupies the top-left corner of what is
       uploaded. */
    int                  tex_width, tex_height;
    int                  tex_padded_width, tex_padded_height;
    /* The factor that carries the microcode's 10.5 into the projection's [0,1],
       padding width included. */
    float                tex_scale_s, tex_scale_t;

    /* Trace mode. Without this tool, every graphics diagnosis on the target
       machine is made blind — the screen belongs to the 3dfx card and one sees
       nothing but the result. */
    void (*trace)(void *user, const char *line);
    void  *trace_user;
} dkr_f3d_context;

/* Prepares the context. `rdram` and `rdram_size` describe the visible memory;
   anything outside it is rejected. */
void dkr_f3d_init(dkr_f3d_context *ctx, const unsigned char *rdram,
                  unsigned int rdram_size, dkr_render_backend *backend);

/* Runs the display list at `address`. Returns the number of commands decoded.
 *
 * `address` is an RDRAM address, not a pointer: the decoder never dereferences
 * anything coming from the display list without bounding it first. */
unsigned long dkr_f3d_run(dkr_f3d_context *ctx, unsigned int address);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_F3DDKR_H */
