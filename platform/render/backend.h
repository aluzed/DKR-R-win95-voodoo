/* E04-S01 — the interface the F3DDKR decoder drives.
 *
 * Two implementations consume it: Glide (E05) and the reference software
 * rasteriser (E04-S08), which serves as the comparison oracle. It therefore
 * contains no RT64, SDL2 or ImGui type at all.
 *
 * ## What it is, and why it sits low
 *
 * `ultramodern::renderer::RendererContext` already exists, but receives a raw
 * RSP task and leaves everything to the implementation. That is how the current
 * decoder became welded to RT64: `f3ddkr_rt64.cpp` manipulates `RT64::State`,
 * `RT64::DisplayList`, and registers RT64 workloads.
 *
 * This one sits one notch lower. The decoder transforms, lights and clips
 * (E04-S03, E04-S05); the backend receives primitives **already projected into
 * screen coordinates**. This is not a choice of elegance: no 3dfx card
 * transforms, and an interface promising vertices in object space would force
 * the Glide backend to redo on the CPU what the decoder has just done.
 *
 * ## The rule that guided every decision
 *
 * The interface **hugs the card** instead of abstracting it. An over-generic
 * interface is paid for twice: when writing the Glide backend, which then has to
 * emulate what the card does not do, and at run time, in per-primitive overhead
 * on a 400 MHz CPU.
 *
 * Every element therefore carries one of these two annotations:
 *
 *     NATIVE     Glide can do it, and the call is named
 *     TO EMULATE Glide cannot, and the workaround is described
 *
 * ## What was found in `f3ddkr_rt64.cpp`, and what decides the shape
 *
 * The decoder's handlers are: `Matrix`, `Vertex`, `Triangle`, `FillRect`,
 * `SetTextureImage`, `LoadBlock`, `TextureOffset`, `MoveWord`, plus display-list
 * flow control — which does not concern rendering.
 *
 * Three observations matter:
 *
 * 1. **The DKR vertex carries no texture coordinates.** Its ten bytes are
 *    `x, y, z` as signed 16-bit integers and `r, g, b, a` as bytes. The `s, t`
 *    coordinates arrive **per corner, when the triangle is emitted** —
 *    `rsp.modifyVertex(vertices[corner], G_MWO_POINT_ST, texcoord)` modifies the
 *    cached vertex just before drawing.
 *
 *    Direct consequence: **an indexed-vertex interface would be wrong here.**
 *    The same cached vertex is drawn with different `s, t` depending on the
 *    triangle; an indexed backend would therefore have to keep a mutable cache
 *    and rewrite it for every triangle. Since `grDrawTriangle` takes three
 *    complete vertices anyway, the interface takes them too, and the expansion
 *    happens on the decoder side — where the information is already in hand.
 *
 * 2. **Culling is per triangle**, decided by a bit in the header (0x40) and by
 *    the sign of the viewport's x scale.
 *
 * 3. **The decoder needs only one drawing primitive**, the triangle, plus the
 *    filled rectangle from `FillRect`. There is no line, no point, no fan.
 */
#ifndef DKR_RENDER_BACKEND_H
#define DKR_RENDER_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- The vertex ------------------------------------------------------------ *
 *
 * **The layout is that of Glide 2.x's `GrVertex`, field for field, and that is
 * deliberate.** The Glide backend passes the vertex address straight to
 * `grDrawTriangle`: no conversion, no copy. On a Pentium II, a per-vertex format
 * conversion is a real cost, and DKR's rendering emits tens of thousands of them
 * per frame.
 *
 * The order is not intuitive — `ooz` and `a` sit between the colours and `oow` —
 * and departing from it produces **no error at all**: Glide reads the floats at
 * the wrong offsets and renders permuted colours. Measured in E09-S01, where a
 * red vertex came out green. Do not reorder these fields.
 *
 * The E04-S08 software rasteriser reads the same fields by name; the layout
 * costs it nothing.
 */
typedef struct {
    float x, y;            /* screen coordinates, in pixels, origin top left */
    float z;               /* ignored by Glide; the software rasteriser uses it */
    float r, g, b;         /* 0..255, not 0..1 — that is Glide's scale */
    float ooz;             /* 65535/z, the depth-buffer value */
    float a;               /* 0..255 */
    float oow;             /* 1/w, perspective correction */
    float tmu[3][4];       /* per texture unit: sow, tow, oow, reserved */
} dkr_render_vertex;

/* Indices into `tmu[n]`, named so that the call sites read. */
#define DKR_TMU_SOW 0      /* s/w, in the 256-texel space — see below */
#define DKR_TMU_TOW 1      /* t/w, likewise */
#define DKR_TMU_OOW 2      /* 1/w */

/* **Texture coordinates live in a 256-texel space, always.**
 *
 * This is neither [0,1] nor the texture's real width, and it is measured, not
 * chosen: `glide_texture_probe.c` tried the scales 64, 128, 255, 256 and 512 on
 * a 64x64 checkerboard whose four corners carry distinct colours. Only 255 and
 * 256 put the four colours in the four corners. Glide therefore normalises over
 * 256 **whatever the texture's size**.
 *
 * The contract keeps that convention rather than [0,1] for a cost reason: the
 * factor is a constant, independent of the bound texture. The Glide backend thus
 * receives the vertices as they are — that is the whole point of having mirrored
 * `GrVertex` field for field — and it is the reference rasteriser, which has no
 * speed constraint, that divides to get back to [0,1].
 *
 * The reverse would have forced a copy of every vertex before every triangle, on
 * a machine where transformation already costs 0.682 us per vertex.
 *
 * The trap this closes: the two backends were not speaking the same language —
 * the rasteriser sampled in [0,1], the card in 256 — and the E09-S02 comparison
 * did not notice, for want of a texture in the scene.
 *
 * **256 spans the larger side, not each side.** Glide addresses a texture by its
 * LOD — the larger dimension — and an aspect ratio; the smaller side therefore
 * spans only 256/ratio. On a 16x64 texture, `s` runs 0..64 and `t` runs 0..256.
 *
 * The probe above could not see this: a 64x64 checkerboard has ratio 1, where
 * "divide by the larger side" and "divide by its own side" are the same
 * division. Its answer is right and incomplete, which is the more dangerous of
 * the two. What made the omission visible is the game itself — the menu's
 * glyphs, a 16x64 atlas, drew four copies of every letter across each rectangle
 * on 20 August 2026. **The repeat count equalled the aspect ratio**, and that is
 * what named the cause. */
#define DKR_TEXCOORD_SCALE 256.0f

/* --- The render state ------------------------------------------------------ *
 *
 * A **block of values**, not a series of calls. The backend compares against the
 * current block and emits only the differences: that is what makes state
 * tracking cheap, and it is the only tenable approach when every Glide state
 * change costs a function call through a DLL.
 *
 * The block is comparable with `memcmp` — hence no implicit padding and no
 * pointers other than the texture handle.
 */

/* The combiner. **TO EMULATE, partially.**
 *
 * The RDP's combiner takes two stages with four inputs; Glide 2.x's is fixed and
 * offers only a set of modes. The cases DKR actually uses are collected in
 * E04-S06 and translated in E05-S03; those with no equivalent will need a second
 * pass. This enumeration therefore lists only **intentions**, not RDP modes: it
 * is up to the translator to produce them, and up to the backend to honour them.
 *
 * Glide calls: `grColorCombine`, `grAlphaCombine`, `grTexCombine`. */
typedef enum {
    DKR_COMBINE_SHADE = 0,        /* vertex colour alone */
    DKR_COMBINE_TEXTURE,          /* texel alone */
    DKR_COMBINE_TEXTURE_SHADE,    /* texel modulated by the vertex colour */
    DKR_COMBINE_TEXTURE_SHADE_ALPHA, /* likewise, texel alpha kept */
    DKR_COMBINE_TEXTURE_CONSTANT, /* texel modulated by `constant_color` */
    DKR_COMBINE_COUNT
} dkr_combine_mode;

/* Blending. **NATIVE** — `grAlphaBlendFunction`. */
typedef enum {
    DKR_BLEND_OPAQUE = 0,         /* no blending */
    DKR_BLEND_ALPHA,              /* src.a, 1-src.a */
    DKR_BLEND_ADDITIVE,           /* one, one */
    DKR_BLEND_COUNT
} dkr_blend_mode;

/* The depth test. **NATIVE** — `grDepthBufferFunction`, `grDepthMask`.
 *
 * One reservation measured by ADR 0002: the depth buffer takes up the same
 * frame-buffer memory as the colour buffers, and that is what ruled out triple
 * buffering. Disabling it frees bandwidth, not memory. */
typedef enum {
    DKR_DEPTH_DISABLED = 0,
    DKR_DEPTH_TEST_ONLY,          /* tests without writing */
    DKR_DEPTH_TEST_AND_WRITE,
    DKR_DEPTH_COUNT
} dkr_depth_mode;

/* Filtering and wrapping. **NATIVE** — `grTexFilterMode`, `grTexClampMode`. */
typedef enum { DKR_FILTER_POINT = 0, DKR_FILTER_BILINEAR } dkr_filter_mode;
typedef enum { DKR_WRAP_REPEAT = 0, DKR_WRAP_CLAMP, DKR_WRAP_MIRROR } dkr_wrap_mode;

/* Culling. **NATIVE** — `grCullMode`. Per triangle in the decoder, but carried
 * by the state: Glide has no per-primitive culling, and the decoder already
 * groups its triangles by winding. */
typedef enum { DKR_CULL_NONE = 0, DKR_CULL_FRONT, DKR_CULL_BACK } dkr_cull_mode;

/* Texture handle returned by the backend. Zero means "no texture". */
typedef unsigned int dkr_texture_handle;

typedef struct {
    dkr_combine_mode   combine;
    /* The constant colour the combiner mixes, 0xAARRGGBB, when `combine` names a
       mode that uses one. **NATIVE** — `grConstantColorValue`. It is what tells
       one pass of DKR's multi-pass text from the next; see `rdp_state.h`. */
    unsigned int       constant_color;
    /* **Both constant registers, always, whatever the colour side read.**
     *
     * `constant_color` above carries the one Glide can be given, chosen by which
     * register the colour mux names. That choice is a Glide limitation and not a
     * property of the RDP, which has two and may read both:
     * `G_CC_MODULATEIA_PRIM` in cycle 1 with `G_CC_BLEND_ENV_ALPHA2` in cycle 2
     * does exactly that, and it is what draws this game's glows.
     *
     * A reference rasteriser that evaluates the real combiner needs both, so both
     * are carried. Both are `0xAARRGGBB`, repacked at the source like
     * `constant_color`, so that no consumer has to know the RDP's own order. */
    unsigned int       prim_color;
    unsigned int       env_color;
    /* --- Which entry of the E05-S03 catalogue applies, or none --------------- *
     *
     * `combine` above is a four-mode shorthand; the generated table covers
     * twenty-nine configurations and carries, for each, the Glide setup that
     * renders it. `dkr_cc_lookup` was being called to *count* matches and its
     * answer thrown away, so `approximate` equalled `catalogued` and the setups
     * were never applied -- the gap E05-S03 has carried since it was written.
     *
     * An **index**, not a pointer: the block is compared with `memcmp`, and a
     * pointer would make it depend on the pointer width and on where the table
     * happens to sit.
     *
     * **The index is stored plus one, and zero means none.** It used to be
     * stored as it was, with -1 for "not catalogued", and that made the zeroed
     * block select entry 0 -- a two-texel configuration this port cannot serve.
     * Every witness in `tools/win95/witnesses` builds its state with a `memset`,
     * so every one of them had been running that entry since the recipes were
     * wired on 22 August 2026; `ai88_probe.c` painted a black frame until the
     * field was set by hand, which is how it was found. A default that has to be
     * written down to be safe will be forgotten, so the safe value is now the
     * one `memset` produces. */
    short              recipe;
    short              recipe_pad;    /* keeps the block free of implicit padding */
    dkr_blend_mode     blend;
    dkr_depth_mode     depth;
    dkr_cull_mode      cull;
    dkr_filter_mode    filter;
    dkr_wrap_mode      wrap_s;
    dkr_wrap_mode      wrap_t;

    /* Alpha test. **NATIVE** — `grAlphaTestFunction`,
       `grAlphaTestReferenceValue`. `alpha_reference` only counts if `alpha_test`
       is non-zero. */
    unsigned char      alpha_test;
    unsigned char      alpha_reference;   /* 0..255 */

    /* Fog. **NATIVE** — `grFogMode`, `grFogColorValue`, `grFogTable`.
       One of the few things Glide does better than its contemporaries, and DKR
       uses it constantly. */
    unsigned char      fog_enabled;
    /* **The factor the RDP's alpha mux applies to the texel's alpha**, 0..255,
     * with 255 meaning none. This byte used to be `pad_`, explicit padding
     * keeping the block memcmp-able; it is a field of the same width now, so the
     * layout is unchanged.
     *
     * It exists because the two backends had each decided the question alone and
     * decided differently — the oracle took the texel's alpha untouched, the
     * Glide path multiplied it by the alpha of `constant_color` — and neither had
     * read the RDP's alpha stages, which are a separate mux from the colour ones.
     *
     * The disagreement is not a rounding one. DKR draws a character's name in
     * five passes at the same coordinates, differing only by a constant colour,
     * and the register the *colour* side reads is not always the register the
     * *alpha* side reads. Measured 4 September 2026: the last pass carried a
     * constant whose alpha byte was zero, the Glide path multiplied by it, and
     * 2416 pixels of the nameplate went black.
     *
     * Derived in `dkr_rdp_to_render_state` for the shapes the alpha mux takes in
     * this game; anything else keeps 255, which is the texel's alpha alone.
     *
     * **Read by the fallback path only, and deliberately.** The E05-S03 catalogue
     * programs its alpha combiner from the table's own `ac_*`, generated from the
     * real alpha stages, so it does not need this. It does share the register
     * question — a catalogued entry whose alpha names the register the colour side
     * did not still receives the colour register's alpha — but no measurement has
     * reached that case yet, and a blind substitution there would be wrong for
     * every entry whose alpha shape this byte cannot carry. Left as it is, and
     * written down. */
    unsigned char      alpha_scale;
    unsigned int       fog_color;         /* 0x00RRGGBB */

    dkr_texture_handle texture;
    /* The second layer, for configurations that read two texels (E05-S04). Zero
       when there is only one, which is the common case. */
    dkr_texture_handle texture1;
} dkr_render_state;

/* --- Textures -------------------------------------------------------------- *
 *
 * A handle cache. The decoder supplies an **already decoded** texture in a
 * format the card accepts, plus a key that identifies it; the backend returns a
 * handle and manages its placement in texture memory on its own (E05-S02).
 *
 * The key is the RDRAM address combined with the format and the dimensions,
 * computed by the decoder. The backend does not interpret it: it compares, that
 * is all. That is what lets it answer "I already have it" without decoding
 * again — and N64 decoding (E04-S07) is expensive.
 *
 * **NATIVE** — `grTexDownloadMipMap`, `grTexSource`. Placement inside the TMU,
 * on the other hand, is entirely the backend's job: Glide exposes flat memory
 * and an address, with no allocator. */
typedef enum {
    DKR_TEXFMT_ARGB1555 = 0,      /* the Voodoo's natural format */
    DKR_TEXFMT_RGBA8888,          /* to be converted: the Voodoo 2 does not take it */
    DKR_TEXFMT_INTENSITY8,
    /* Eight bits of intensity and eight of alpha, sixteen a texel — the same
       memory as ARGB1555 and exactly what the N64's `I` and `IA` formats mean.
       Which byte carries which is measured by `AI88.EXE` rather than assumed:
       this port has already had one texel layout wrong from memory. */
    DKR_TEXFMT_ALPHA_INTENSITY88,
    /* Four bits an axis. Sixteen a texel again, so no more memory than
       ARGB1555, and **four alpha bits instead of one** — which is exactly what
       the N64's `IA8` carries, and sixteen times what a threshold leaves of
       `IA16`. Measured by `AI88.EXE` alongside AI88, whose alpha does not
       arrive. */
    DKR_TEXFMT_ARGB4444,
    DKR_TEXFMT_COUNT
} dkr_texture_format;

typedef struct {
    unsigned long long key;       /* opaque to the backend; it compares, it does not interpret */
    dkr_texture_format format;
    int                width, height;
    const void        *pixels;
    size_t             size_bytes;
    /* Which texture unit to place it on. Zero by default.
     *
     * **A texture is not samplable from a TMU where it does not reside**, and
     * the two units have their own memory: they are two spaces, not one.
     * Carrying the target in the descriptor rather than in the signature avoids
     * changing the function table for information only multitexturing uses. */
    int                tmu;
} dkr_texture_desc;

/* --- The interface --------------------------------------------------------- *
 *
 * A table of function pointers rather than a set of symbols: both
 * implementations must coexist in the same binary, the software rasteriser
 * serving as the Glide backend's oracle (E04-S08). Global symbols would forbid
 * that.
 *
 * Any function may be null in a partial implementation; the caller must check.
 * That is what lets an empty implementation compile and link, as E04-S01 asks.
 */
typedef struct dkr_render_backend {
    const char *name;             /* "glide", "software" — for the logs */

    /* Life cycle. `open` returns zero on failure; the message stays with the
       backend, which alone knows why. */
    int  (*open)(void *self, int width, int height);
    void (*close)(void *self);

    /* Frame cycle. **NATIVE** — `grBufferClear`, `grBufferSwap`.
       `begin_frame` clears; `present` swaps the buffers. */
    void (*begin_frame)(void *self, unsigned clear_argb);
    void (*present)(void *self);

    /* State. The backend compares against the current block and emits only the
       differences. */
    void (*set_state)(void *self, const dkr_render_state *state);

    /* Scissor window. **NATIVE** — `grClipWindow`. The coordinates are in screen
       pixels, bounds included on the left and top, excluded on the right and
       bottom — Glide's convention, kept so that no conversion is needed. */
    void (*set_scissor)(void *self, int x0, int y0, int x1, int y1);
    /* Forget what the backend believes the card holds, without changing what it
       is asked to hold. Optional -- a backend that keeps no cache leaves it
       null. It exists for one measurement: `DKR_FORCE_STATE=3` pushes the same
       block after invalidating, which is the only way to separate "the values
       differ" from "the registers were written" now that pushing a *different*
       block is known to take the painted surface from 1,700 pixels to 120,000. */
    void (*invalidate)(void *self);

    /* Triangles. The vertices are complete and projected; see the remark about
       texture coordinates at the top of the file.
       **NATIVE** — `grDrawTriangle`, one call per triangle.
       `draw_triangles` takes an array of 3n vertices in order to amortise the
       call crossing the interface, not the one crossing Glide. */
    void (*draw_triangles)(void *self, const dkr_render_vertex *vertices,
                           int triangle_count);

    /* Filled rectangle. **TO EMULATE** — Glide has no rectangle primitive. Two
       triangles are enough, and the backend builds them: doing it here rather
       than in the decoder avoids imposing the same expense on the software
       rasteriser, which can fill a rectangle directly. */
    void (*fill_rect)(void *self, int x0, int y0, int x1, int y1,
                      unsigned argb);

    /* Textures. `upload` returns zero on failure — TMU memory full, for
       instance, which E05-S02 will have to handle. */
    dkr_texture_handle (*texture_upload)(void *self, const dkr_texture_desc *desc);
    void               (*texture_release)(void *self, dkr_texture_handle handle);

    /* --- Is this key already on the card? ---------------------------------- *
     *
     * Returns the handle if the texture is resident on `tmu` and usable as it
     * stands, zero otherwise. May be NULL: a backend that keeps nothing answers
     * by not implementing this, and the caller converts as before.
     *
     * **The measurement that asks for it.** The allocator's counters, read on
     * the game for the first time on 26 August 2026, report `hits=63670/64358`
     * over 780 display lists — 98.9 %. `uploaded` and `hits+misses` are the same
     * quantity counted on either side of this interface, so 63,670 times out of
     * 64,358 the decoder converted a texture in order to hand it to an allocator
     * that already had it. The bus moved 1.5 KiB a list; the CPU converted some
     * 78,000 texels a list, and 98.9 % of that work had a destination that did
     * not need it.
     *
     * The decoder cannot answer this itself. Its cache holds one entry and
     * `dkr_f3d_init` clears the whole context for every graphics task, so it
     * cannot remember across display lists — and *across* is where the
     * repetition is. Residency lives here and survives, which is the whole
     * reason the question is asked here.
     *
     * **A hit must advance the recency the eviction policy reads**, both in the
     * backend's descriptor table and in the allocator below it. Skipping that
     * would make "least recently used" mean "least recently *uploaded*", and the
     * texture drawn on every triangle of the frame would be the first thrown
     * out — the trap `gl_texture_upload` already documents against itself. */
    dkr_texture_handle (*texture_lookup)(void *self, unsigned long long key,
                                         int tmu);

    void *self;                   /* the implementation's private state */
} dkr_render_backend;

/* An empty implementation, which accepts everything and draws nothing.
 *
 * It is not a stub of convenience: it serves to establish that the interface
 * compiles and links without a real backend, and it gives the decoder a target
 * while Glide and the software rasteriser are being written. The day a rendering
 * defect appears, comparing it against the other two will say whether the
 * decoder or the backend is at fault. */
void dkr_render_backend_null(dkr_render_backend *out);

/* The real implementations. Declared here rather than each in its own header:
   a caller choosing a backend at run time wants them all visible from a single
   include, and that is how the E09-S02 comparator opens them side by side on the
   same input.

   `glide` is only available on the Win95 target; on the host, only the software
   version is compiled, and that is deliberate — the oracle must run
   everywhere. */
void dkr_render_backend_software(dkr_render_backend *out);
#if defined(DKR_TARGET_WIN95)
void dkr_render_backend_glide(dkr_render_backend *out);
unsigned long dkr_glide_backend_triangle_count(void);

/* What `bind_texture` really did: binds performed, binds **skipped because the
 * slot was no longer live**, and binds that actually moved the TMU's address.
 *
 * The middle figure is the point. A skipped bind leaves the TMU sampling
 * wherever it last pointed, so every draw after it wears one texture -- the
 * exact symptom of the flat frame measured on 17 August 2026, where forcing the
 * combiner to the texel alone gave three shades of one colour over the whole
 * screen. It used to be a silent early return.
 *
 * `changed` separates the two readings that remain: "one texture is bound over
 * and over" from "many are bound and the sampling is wrong regardless". */
int dkr_glide_backend_can_draw(void);
void dkr_glide_backend_bind_stats(unsigned long *binds,
                                  unsigned long *dead,
                                  unsigned long *changed);
/* Why a texture upload failed: 0 aspect ratio refused by the card, 1 zero size,
   2 descriptor table full, 3 TMU memory exhausted. Four causes behind a single
   returned zero; conflating them makes one fix the wrong thing. */
unsigned long dkr_glide_backend_upload_failure(int kind);

/* Descriptor slots taken back from textures the allocator no longer holds.
   Reported beside the refusals so that "the table never fills" is a figure
   and not a hope: the two together say whether the reclaim is doing the work
   or whether the working set has simply not reached the limit yet. */
unsigned long dkr_glide_backend_slots_reclaimed(void);
/* State applications served by chaining both texture units (E05-S04). */
unsigned long dkr_glide_backend_two_layer_states(void);
/* E05-S03's second pass: how many batches got one, and how many did not with the
   reason — the second cycle would have been the identity, or its shape is not the
   one the pass reproduces. `blend` counts passes that *were* drawn over an
   alpha-blended first pass, where the composition is approximate rather than
   exact; it is a subset of `drawn` and not a refusal. A pass silently not drawn is indistinguishable from one
   not needed, and the guards exist precisely because most are not needed. */
void dkr_glide_backend_pass2_stats(unsigned long *drawn, unsigned long *identity,
                                   unsigned long *unsupported,
                                   unsigned long *blend,
                                   unsigned long *by_shade);
/* The **first** cycle done in two blends, when one combiner stage cannot hold it:
   how many batches took that route, and how many were refused because a cutout was
   in force. See `prepass_wanted` for why an alpha test rules it out. */
void dkr_glide_backend_prepass_stats(unsigned long *drawn,
                                     unsigned long *refused_alpha_test,
                                     unsigned long *texel_alone);
/* Selects `SCALE_OTHER / FACTOR_ONE` instead of `BLEND_OTHER / 0x0B` for the
   configuration that draws this game's copyright text. The factor sweep says the
   two compute almost the same thing; one measurement says they do not produce the
   same image. A switch is how that gets settled rather than argued. */
void dkr_glide_backend_texel_factor_one(int on);
/* What `texture_lookup` answered, cumulatively: resident, not resident, and
   **stale** -- a descriptor naming a key the allocator has since evicted. The
   third is separated from the second on purpose: a stale hit is the one that
   would have drawn another texture's pattern, so a non-zero count is a defect
   report and not a cache statistic. */
void dkr_glide_backend_lookup_stats(unsigned long *hits, unsigned long *misses,
                                    unsigned long *stale);
/* The TMU allocator's state, for E08-S01's diagnostic display. Returns NULL if
   the requested TMU does not exist. */
struct dkr_tmu;
const struct dkr_tmu *dkr_glide_backend_tmu(int index);
/* Applies a setup from the E05-S03 table and binds a texture. Used by the
   measurement harness, and meant for the engine: the four `dkr_combine_mode`
   modes are only a shorthand in front of twenty-nine configurations. */
struct dkr_cc_setup;
void dkr_glide_backend_set_recipe(const struct dkr_cc_setup *r,
                                  unsigned constant_argb);
void dkr_glide_backend_bind(dkr_texture_handle handle);
/* Chains the two texture units (E05-S04). No effect on a single-TMU card, where
   the multipass fallback applies. */
void dkr_glide_backend_chain(dkr_texture_handle tmu0, dkr_texture_handle tmu1,
                             unsigned char function, unsigned char factor);
/* Forces the multipass path even on a two-TMU card. Ticket E05-S04 names the
   risk: a fallback that is easy to write and easy never to exercise, for want of
   single-TMU hardware to hand. */
void dkr_glide_backend_force_single_tmu(int force);
/* The number of **usable** TMUs, which accounts for the forcing. That is what
   the path choice must read, never the hardware detection directly. */
int  dkr_glide_backend_tmu_count(void);
/* Selects the depth buffer: non-zero for W, zero for Z. Exposed so that E05-S05
   compares the two by measurement rather than by reputation. */
void dkr_glide_backend_depth_mode(int use_w);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_BACKEND_H */
