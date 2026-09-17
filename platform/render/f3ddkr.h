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
/* The geometry-mode bits this decoder reads, from the decompilation's `gbi.h`
   rather than from memory. Only the ones something acts on are named: a define
   nobody reads is a claim nobody checks. */
#define DKR_G_ZBUFFER 0x00000001u
#define DKR_G_SHADE   0x00000004u
#define DKR_G_FOG     0x00010000u
#define DKR_G_CULL_FRONT 0x00001000u
#define DKR_G_CULL_BACK  0x00002000u

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

    /* --- The geometry mode, read at last -------------------------------------- *
     *
     * `G_SETGEOMETRYMODE` (0xB7) and `G_CLEARGEOMETRYMODE` (0xB6) sit inside the
     * skipped `0xB0..0xBF` family, and every capture of the corpus carries a
     * handful of each - four to eight sets, three or four clears. Until now they
     * were counted as `deferred` and dropped, which means this decoder has never
     * known whether the game asks for `G_FOG`, `G_ZBUFFER` or a cull direction.
     *
     * That is what keeps fog switched off. The note beside `OP_SETFOGCOLOR`
     * says it exactly: the colour has been decoded since the deferred-command
     * audit, and "fog is off because the coefficient is wrong, not because the
     * colour was missing". The coefficient is the vertex alpha, and the vertex
     * alpha is a fog coefficient **only where the geometry mode says `G_FOG`** -
     * which nothing here could say. This is that half.
     *
     * The encodings are read from the decompilation's `gbi.h`, not recited:
     * `w0 = opcode << 24`, `w1 = the mode word` for both, the clear naming the
     * bits to remove rather than their complement. The bit values come from the
     * same header.
     *
     * **Recorded before it is used.** This field changes no pixel on its own; the
     * counts and the value are reported so that the question "does this game set
     * G_FOG at all, and where" has an answer before anything is wired to it. */
    /* How many `G_MW_FOG` words the list carried. Non-zero means the game is
       asking for fog and this port is not computing its coefficient. */
    unsigned long fog_words;

    /* --- Draws aimed at a render target nothing reads ------------------------ *
     *
     * `color_image_address` is decoded, stored, and consumed by no backend: the
     * port has no concept of a render target and every draw lands in the one
     * frame buffer, whichever colour image the list selected. The running game
     * switches target constantly - 2,433 `SetColorImage` in one run, 488 of them
     * to a second address - so the gap is structural.
     *
     * What that costs is a different question from how often it happens, and it
     * is this: how many *draws* are aimed somewhere other than the first colour
     * image the list named. Counted, not acted on. */
    unsigned int  first_color_image;
    unsigned long emitted_secondary;
    unsigned long texture_cmds;

    /* --- Two sources for one fact, counted against each other ---------------- *
     *
     * Depth and culling are *derived* - from the blender and from the winding -
     * and the geometry mode *states* them. Both are now available, and nothing
     * had ever confronted them. Wherever they disagree there is a class of error,
     * and a count is the cheapest way to know whether the class is empty. */
    unsigned long geom_batches;
    unsigned long geom_depth_disagrees;
    unsigned long geom_cull_disagrees;

    unsigned int  geometry_mode;
    unsigned long geometry_mode_writes;

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
    /* Served by the backend's residency query, without converting anything.
       Distinct from `textures_reused`, which counts the one-entry cache in
       front of the conversion: that one is per display list, this one is what
       survives between them. Reading one for the other is how
       `uploaded=64358 reused=1413` looked like a two-per-cent cache for days
       while the card was answering 98.9 %. */
    unsigned long     textures_resident;
    /* Triangles painted under each `dkr_cc_category`, and those under no
       catalogue entry at all. The measurement that decides E05-S04: a
       configuration reading two texels can be rare and still cover the screen. */
    unsigned long     emitted_per_cc[4];
    unsigned long     emitted_uncatalogued;
    /* E05-S04's precondition. `tilesize_per_tile` says which tiles the game
       sizes at all; `tile1_distinct` counts the sizings of tile 1 that name a
       texture image other than tile 0's -- that is, a real second texel. Zero
       there would mean the two-texel configurations have nothing to read from,
       and the ticket is not what it says it is. */
    unsigned long     tilesize_per_tile[8];
    unsigned int      tile_image[2];
    unsigned long     tile_image_changes[2];
    unsigned long     tile1_distinct;
    /* Triangles handed over with both layers bound, and tile-1 sizings the port
       could not serve -- refused upload, or a card with one TMU. The pair is what
       says whether the chaining is reaching the card or quietly not. */
    unsigned long     emitted_two_layer;
    unsigned long     tile1_unserved;
    /* Every exit of the tile-1 path, separately. Three sizings of the second
       layer produced one download and no two-layer triangle, and "111 in, 1
       out" does not say which door the other 110 left by. Each door is counted
       so the next run names it instead of narrowing it. */
    unsigned long     tile1_entered;
    unsigned long     tile1_cached;      /* the one-entry cache answered */
    unsigned long     tile1_resident;    /* the card already held it */
    unsigned long     tile1_uploaded;    /* converted and downloaded */
    /* And at the triangle: a two-texel configuration drawn with the second
       layer bound, and without. The second is the number that says the layer is
       not surviving to the draw. */
    unsigned long     two_texel_with_layer;
    unsigned long     two_texel_without;
    /* --- What the conversions actually cost, and how much of it repeats ------ *
     *
     * `textures_reused` counts hits on a cache of **one entry**: the tile whose
     * key matches the last one converted. Everything else is converted again,
     * texel by texel, and on a 400 MHz Pentium II that is the decoder's largest
     * per-frame cost after the transform.
     *
     * Two figures size it. `conversion_texels` is the work done; `distinct_keys`
     * is how many different tiles a list actually asks for, which is the work a
     * cache of the right size would leave. If the second is far below the number
     * of conversions, the cache is worth building and its size is known rather
     * than guessed. `distinct_overflow` says when the set could not hold them
     * all, so the figure is never read as complete when it is not. */
    unsigned long     conversion_texels;
    unsigned long long distinct_key_set[64];
    unsigned          distinct_keys;
    unsigned long     distinct_overflow;
    unsigned long     textures_refused;   /* texture memory full */
    /* Padded up to the next power of two, which the Voodoo requires and the N64
       does not. */
    unsigned long     textures_padded;
    /* Refused because the largest side is beyond Glide's 256, which no padding
       reaches. The 8:1 ratio used to be refused here too, on a stated cost of
       "eight times memory" that turned out to be 80 KiB a list; it is padded
       now, and counted below rather than lost. */
    unsigned long     textures_bad_aspect;
    unsigned long     textures_aspect_padded;
    unsigned short    aspect_padded_dims[8][2];
    unsigned          aspect_padded_n;
    unsigned long     aspect_padded_texels;
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
    unsigned long     tiles_decoded;      /* G_SETTILE, render tile only */
    /* **The tile's row stride, against the one the conversion assumes.**
     *
     * `G_SETTILE`'s `w0` carries `line`, the number of 64-bit words between two
     * rows of the tile in texture memory. Nothing decoded it: the handler read
     * `w1` for the wrap modes and let `w0` go by. The conversion, meanwhile,
     * walks RDRAM linearly and therefore assumes the rows are exactly
     * `width * bytes-per-texel` apart.
     *
     * Where the two agree the assumption is free. Where they do not, every row
     * after the first is read at the wrong offset, and the image comes out
     * sheared — which is what a texture "read at the wrong width" looks like and
     * what this port's small font looks like.
     *
     * Counted rather than asserted. `stride_checked` is the denominator: a
     * mismatch count without it cannot be told from a check that never ran.
     *
     * **This counter has not been validated, and what it reports is suspect.**
     * Every disagreement it finds is a factor of exactly two, on 16x16 and 32x32
     * textures, in every scene of the corpus. A real stride error shears the
     * image; sixteen sheared textures in one frame would be visible and are not.
     * The likeliest reading is that the check compares a conversion against a
     * `G_SETTILE` that belongs to another tile or another moment — the decoder
     * keeps one render tile and the two are not ordered with respect to each
     * other. Read it as "something here does not line up", never as a defect
     * count, until a texture of known layout has been put through it. */
    /* **Tiles whose upper-left corner is not the image's.**
     *
     * `uls`/`ult` place the tile inside the texture image; this decoder converts
     * from the image origin and the s,t it hands the rasteriser are the ones the
     * microcode gave, unshifted. Where `uls` is zero the two conventions agree
     * and nothing is owed. Where it is not, the texels come from one place and
     * the coordinates address another.
     *
     * `f3ddkr.c` has recorded `uls` and `ult` since they were first decoded,
     * with a comment saying that whether it matters should be measured before
     * anything is written to act on it. This is that measurement. */
    unsigned long     tile_origin_nonzero;
    unsigned short    tile_origin_first[8][4];  /* uls, ult, width, height */
    unsigned int      tile_origin_first_n;
    /* **The size the tile declares against the size the image declares.**
     *
     * `dkr_texture_convert` is called with `timg_size`, from
     * `G_SETTEXTUREIMAGE`. But `gDPLoadTextureBlock` re-declares the image in
     * whatever unit suits the *transfer* and leaves the real texel size on the
     * tile, in `G_SETTILE`'s `siz`. Where they differ, every texel is read at
     * the wrong width. */
    /* **Tiles narrower than the image they come from.**
     *
     * `G_SETTEXTUREIMAGE` carries `width - 1` in the low twelve bits of `w0`,
     * and nothing decoded it: the handler took `fmt` and `siz` and let the width
     * go by. `dkr_texture_convert` therefore reads the texels linearly, which
     * assumes the image is exactly as wide as the tile.
     *
     * Where it is not, every row after the first starts at the wrong offset. */
    /* **The row stride `G_LOADBLOCK` declares, against the one assumed.**
     *
     * `gsDPLoadBlock(tile, uls, ult, lrs, dxt)` puts `dxt` in the low twelve
     * bits of `w1`. It is the RDP's row-advance increment, and it names the
     * row length: `dxt = 2048 / words-per-row`, so the row is `2048 / dxt`
     * 64-bit words. Nothing decoded it.
     *
     * `dkr_texture_convert` reads the texels linearly, which assumes the rows
     * are `tile width x bytes-per-texel` apart. Where `dxt` says otherwise, the
     * tile is a window into a wider image and every row after the first is read
     * from the wrong place. */
    unsigned long     dxt_disagrees;
    unsigned long     dxt_disagrees_texels;
    unsigned short    dxt_first[8][4];   /* dxt row bytes, assumed row bytes, w, h */
    unsigned int      dxt_first_n;
    /* Conversions that applied the RDP's odd-row swap, because their block
       was loaded with `dxt == 0`. See `f3ddkr.c`. */
    unsigned long     odd_row_swapped;
    /* Conversions that read the texels at the *tile's* format and size rather
       than the texture image's, because the two disagree. See `f3ddkr.c`. */
    unsigned long     tile_texel_size_used;
    unsigned long     image_wider;
    unsigned long     image_wider_texels;
    unsigned short    image_wider_first[8][4];  /* image w, tile w, tile h, siz */
    unsigned int      image_wider_first_n;
    unsigned long     size_mismatch;
    unsigned short    size_first[24][6];  /* tile siz, timg siz, w, h, tile fmt, timg fmt */
    unsigned int      size_first_n;
    unsigned long     stride_checked;
    unsigned long     stride_mismatch;
    unsigned long     stride_mismatch_texels;
    /* The first four disagreements, whole: tile line in bytes, the bytes a row
       of `width` texels needs, and the dimensions. Four because one example can
       be a special case and a histogram cannot be read. */
    unsigned short    stride_first[8][4];   /* line_bytes, row_bytes, w, h */
    unsigned int      stride_first_n;
    /* The texture-offset indirection: shifts actually applied to an image
       address, and the times the table was abandoned for a shift that does not
       land on a block boundary. Both counted, because a base that is decoded and
       read by nobody is exactly what this replaces. */
    unsigned long     texture_shifts_applied;
    unsigned long     texture_offset_dropped;
    unsigned long     textures_uniform, textures_varied;
    unsigned long     textures_mostly_black;   /* three quarters of texels RGB=0 */
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
    /* What each of those triangles asked its texture for: the texture's padded
       size and format, and the three corners in texels. */
    short              center_tex_w[16], center_tex_h[16];
    unsigned char      center_tex_fmt[16];
    short              center_s[16][3], center_t[16][3];
    unsigned short     center_texel0[16];          /* the texel at (0,0) */
    unsigned int       center_dark[16], center_texels[16];
    unsigned int       center_mean[16];   /* mean luminance, 0..31 */
    short              center_tile_uls[16], center_tile_ult[16];
    unsigned long      center_hits;
    /* Emitted triangles whose centroid lands inside the viewport, against those
       whose does not. The projected extremes clamp to the guard band and the
       area counts guard-band pixels, so neither can say where the geometry is. */
    unsigned long      tri_on_screen, tri_off_screen;
    /* The sum, over the list, of each emitted triangle's bounding box clipped to
       the viewport, halved. An over-estimate of the surface handed to the card,
       and the figure to read against the pixels that come back. */
    unsigned long      on_screen_area;
    /* Triangles emitted with fog on. Glide takes its fog factor from the vertex
       alpha and the fog colour was never decoded, so this counter and the black
       screen are the same fact. */
    unsigned long      emitted_fogged;
    unsigned long      tri_st_degenerate;
    unsigned long      tri_st_varying;
    unsigned long      combiners_known;
    unsigned long      combiners_unknown;
    /* State applications whose render mode carries the RDP's cutout bits. The
       character-select screen shows palms and bushes inside opaque black
       rectangles, and `alpha-test=0` on every frame measured so far — so either
       DKR asks for its cutout some other way, or it asks for none. These two
       counters tell those apart before anything is changed. */
    unsigned long      states_cvg_x_alpha;
    unsigned long      states_alpha_cvg_sel;
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
    /* `DKR_NO_TEXCACHE=1`: convert every texture even when the card already
       holds it. Not a workaround -- it is what makes the frame-dump probe exact
       again, since the residency path has no converted texels to describe and
       deliberately reports `dark=0/0` rather than the previous texture's. A
       diagnostic that costs speed, switched on when the diagnostic is the
       point. */
    unsigned char        no_texture_cache;
    /* Draws both faces of every triangle, whatever the winding says. Zero by
       default.
     *
       A diagnostic and not a mode: back-face culling is where geometry
       legitimately disappears, and it is therefore where geometry that
       disappears *wrongly* hides. The copyright screen of 4 September 2026 culls
       171 of its 293 triangles and draws a blank plate where the `RAREWARE`
       logo belongs, its texture painting exactly zero pixels. Whether the logo
       is being culled or was never asked for is one run apart with this, and
       unanswerable without it. */
    /* Diagnostic: turn the odd-row swap off, to measure what it is worth. */
    unsigned char        no_odd_row_swap;
    /* Diagnostic: read the texels at the texture image's format and size even
       where the render tile declares different ones. That is what the converter
       did before the tile was believed, and it is kept so the change can be
       measured rather than asserted. See `f3ddkr.c`. */
    unsigned char        no_tile_texel_size;
    unsigned char        no_cull;
    /* `DKR_NO_ALPHA_TEST=1`: draw every texel, whatever its alpha.
     *
       A diagnostic, and it exists for one question. The cutout this port applies
       is `CVG_X_ALPHA` translated to a hard alpha test at reference 1, and
       `rdp_state.c` records the approximation that entails: "where the alpha has
       more bits the N64 dithers a partial coverage and a hard threshold cannot".
       A dithered coverage resolved by a threshold looks like a regular speckle
       eaten out of a shape -- which is exactly how this game's best-time digits
       come out on the vehicle-select screen, while the same font elsewhere is
       clean.
     *
       Turning the test off does not fix anything: it answers, in one run, whether
       the speckle is the cutout at all. A hypothesis one cannot switch off is a
       hypothesis one argues about. */
    unsigned char        no_alpha_test;
    /* The second layer's binding and its own coordinate scale, mirroring
       `bound_texture` and `tex_scale_s`. Separate rather than an array of two
       because every other consumer in this file reads the single-texture pair by
       name, and an index would make each of those call sites say `[0]` for no
       gain. */
    dkr_texture_handle   bound_texture1;
    unsigned long long   texture1_key;
    float                tex1_scale_s, tex1_scale_t;
    /* Which layer the sizing in flight is for. Set at the top of
       `cmd_set_tile_size` and read by the paths below it, rather than threaded
       through six call sites that otherwise have no interest in it. */
    unsigned char        tile_target_tmu1;
    /* How many texture units the backend really has, set by the caller from
       `dkr_glide_backend_tmu_count` -- which reads E05-S01's detection *and* the
       test override. Read here rather than called, so that the decoder keeps no
       dependency on the Glide backend and the software oracle can say one. */
    unsigned char        tmu_count;
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
    /* `DKR_FOG=1`. Fog is **off by default**: Glide takes its factor from the
       vertex alpha, which in this port carries opacity and not a fog
       coefficient, and the fog colour is never decoded. See `apply_state`. */
    unsigned char        fog_enabled_override;
    /* The catalogue entry matching the current combiner, or -1. Kept in the
       context rather than in the render state's translation, for the same
       reason as the texture handle: it is a decoder resource. */
    short                catalogue_index;
    /* The render tile's wrap modes, from `G_SETTILE`. Defaults to repeat, which
       is what the translation used to write unconditionally. */
    unsigned char        tile_wrap_s, tile_wrap_t;
    /* `line` from the render tile's `G_SETTILE`, in 64-bit words. See
       `stride_mismatch` in the counters. */
    unsigned short       tile_line;
    /* `siz` from the same `G_SETTILE`. See `tile_line`. */
    unsigned char        tile_size;
    /* `fmt` from the same `G_SETTILE`. */
    unsigned char        tile_format;
    /* Whether a `G_SETTILE` for the render tile has been seen at all. Without
       it `tile_size` and `tile_format` are zeroes that mean nothing, and zero is
       a valid `siz` -- so "never set" has to be its own bit rather than a value
       nobody uses. */
    unsigned char        tile_declared;
    /* `width` from `G_SETTEXTUREIMAGE`, in texels. See `image_wider`. */
    unsigned short       timg_width;
    /* Row bytes implied by the last `G_LOADBLOCK`'s `dxt`, 0 if unknown. */
    unsigned short       block_row_bytes;
    /* `G_SETFOGCOLOR` and `G_SETBLENDCOLOR`, as 0xRRGGBB. Nothing reads the
       blend colour yet; it is decoded so the audit closes. */
    unsigned int         fog_color, blend_color;
    /* `texEnabled` from the current `gSPPolygon` batch. Starts at 1 so that a
       list which draws before its first triangle command behaves as it did. */
    unsigned char        batch_textured;
    unsigned short       tile_uls, tile_ult;   /* the tile's origin in the image */
    /* Where the paint stack is followed. Zero is the centre of the screen. */
    int                  probe_x, probe_y;
    /* Texel (0,0) of the texture currently bound, recorded when it is bound. */
    unsigned short       bound_texel0;
    unsigned int         bound_dark, bound_texels, bound_mean;
    /* `DKR_FORCE_STATE=1`. Every emitted triangle drawn under the canary's own
       state block, to tell a bad state from bad vertices. */
    unsigned char        force_state;
    /* `DKR_NEUTRAL=<mask>`: which fields of the render state to neutralise
       before each triangle. See the note at the emission in `f3ddkr.c`. */
    unsigned char        neutral_mask;
    /* Forces fog off for every draw, the mirror of `fog_enabled_override`.
       The card programs Glide's hardware fog and the software oracle's fog is
       inert, so a fogged draw is the one place the two backends cannot be
       compared - and every corpus figure was taken before the gate that decides
       which draws are fogged reached a built binary. This makes the difference
       measurable in one run instead of argued from counts. */
    unsigned char        fog_disabled;
    /* What the list asked for, kept apart from what is programmed. The gate below
       closes fog before it reaches a backend, and `emitted_fogged` would then
       count zero and stop being a diagnostic - so the request is recorded here
       and the counter reads this instead. */
    unsigned char        fog_asked;

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
    /* The fog coefficient's two constants, from `G_MW_FOG`. Decoded and not yet
       applied - see the long note in `cmd_move_word` for what is measured and
       what is refuted. */
    short                fog_multiplier, fog_offset;
    /* `G_TEXTURE`'s two 0.16 factors, 0xFFFF being one. Recorded, not applied. */
    unsigned short       texture_scale_s, texture_scale_t;

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
