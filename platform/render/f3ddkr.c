/* E04-S02 — implementation. The command map lives in
 * `docs/research/f3ddkr-commands.md`, the contract in `f3ddkr.h`. */
#include "f3ddkr.h"
#include "rdp_state.h"
#include "combiner.h"
#include "texture.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Opcodes, taken from `f3ddkr_rt64.cpp`. */
#define OP_MATRIX        0x01
#define OP_TEXOFFSET     0x02
#define OP_MOVEMEM       0x03
#define OP_VERTEX        0x04
#define OP_TRIANGLE      0x05
#define OP_DLBRANCH      0x06
#define OP_DLCOUNTED     0x07
#define OP_ENDDL         0xB8
#define OP_MOVEWORD      0xBC
#define OP_DMAOFFSETS    0xBF
#define OP_LOADBLOCK     0xF3
#define OP_FILLRECT      0xF6
#define OP_SETFILLCOLOR  0xF7
#define OP_SETTEXIMAGE   0xFD
#define OP_SETCOLORIMAGE 0xFF
#define OP_SETOTHERMODE_L 0xB9
#define OP_SETOTHERMODE_H 0xBA
#define OP_SETCOMBINE     0xFC
#define MOVEMEM_VIEWPORT  0x80
#define VIEWPORT_BYTES    16u
#define OP_SETTILE        0xF5
#define OP_SETTILESIZE    0xF2
#define OP_RDPSETOTHERMODE 0xEF

#define MOVEWORD_BILLBOARD   0x02
#define MOVEWORD_MVPMATRIX   0x0A
#define MOVEWORD_PRESENT     0xFE
#define PRESENT_MAGIC        0x444B5200u
#define PRESENT_META_MASK    0xFFu

#define RDRAM_MASK           0x00FFFFFFu
#define MAX_VERTICES         32u
#define MAX_NESTED           32u
/* An ordinary list runs to its ENDDL; a counted list stops at its count. Zero
   cannot mean both. */
#define NO_COUNT          0xFFFFFFFFu
#define VERTEX_STRIDE        10u
#define TRIANGLE_STRIDE      16u
#define MATRIX_BYTES         64u

/* The logging volume is bounded. A corrupt display list would otherwise produce
   thousands of lines per frame, which drowns the diagnosis instead of lighting
   it up — and costs dearly on a 1998 machine. */
#define MAX_LOGGED_REJECTS   64

/* --- Recognised commands whose effect is not wired up yet ------------------- *
 *
 * The decoder implemented only seven opcodes and **stopped the list** on
 * everything else. That was the right choice as long as it only read fabricated
 * display lists: after a genuinely unknown opcode the stream is desynchronised,
 * and carrying on would invent commands.
 *
 * Faced with the game's lists, that choice made the decoder useless: measured on
 * the machine, **600 lists, 3580 commands, zero triangles** — each stopped at
 * its first `0xE9` or `0xB6`, that is, an RDP synchronisation and a geometry
 * mode clear. The geometry was always *after*.
 *
 * The two families below are those of the F3D microcode and of the RDP, all
 * eight-byte commands, hence all skippable without ambiguity:
 *
 *     0xB0..0xBF   F3D immediates — RDPHALF, TRI2, geometry modes, other modes,
 *                  texture, POPMTX, CULLDL
 *     0xE4..0xFF   RDP — synchronisations, scissor, tiles, colours, combiner
 *
 * The low bound was first set at 0xB6, by reading the opcode table rather than
 * by measurement. The machine answered `0xB4` — `G_RDPHALF_1` — once per list,
 * six hundred times. The family does start at 0xB0, and the discrepancy came
 * from the consulted table listing only the part of the command set that has a
 * geometric effect.
 *
 * Enumerating them rather than accepting everything keeps desynchronisation
 * detection: an opcode outside these ranges still stops the list. That is the
 * property we would have lost by simply replacing the rejection with a `break`,
 * and it is worth keeping — it is what allowed us to see that the memory layout
 * was right, since *no* address rejection appeared. */
static int opcode_effect_deferred(unsigned int opcode)
{
    return (opcode >= 0xB0u && opcode <= 0xBFu) ||
           (opcode >= 0xE4u && opcode <= 0xFFu);
}

const char *dkr_f3d_reject_text(dkr_f3d_reject r)
{
    switch (r) {
    case DKR_F3D_REJECT_ADDRESS: return "address outside RDRAM";
    case DKR_F3D_REJECT_COUNT:   return "invalid count";
    case DKR_F3D_REJECT_INDEX:   return "vertex index outside cache";
    case DKR_F3D_REJECT_DEPTH:   return "nesting too deep";
    default:                     return "unknown opcode";
    }
}

/* --- Bounded reads --------------------------------------------------------- *
 *
 * Every read goes through here. That is what makes the validation discipline
 * checkable: there is only one place to re-read to be sure that nothing leaves
 * RDRAM.
 */
static int in_range(const dkr_f3d_context *c, unsigned int addr, unsigned int len)
{
    /* In 64 bits so that the sum does not wrap: `addr + len` in 32 bits can
       become small again and let an obviously out-of-bounds range through. */
    const unsigned long long end = (unsigned long long)addr + (unsigned long long)len;
    return c->rdram && end <= (unsigned long long)c->rdram_size;
}

/* --- Two memory layouts for the same RDRAM ---------------------------------- *
 *
 * The tests build an RDRAM in plain big-endian, like the console. The game, for
 * its part, supplies librecomp's snapshot, which stores the same memory
 * **XOR-3 interleaved**: the byte at guest address `a` sits at `a ^ 3`. This is
 * visible in N64Recomp's macros:
 *
 *     MEM_BU(o, r)  ->  *(uint8_t *)(rdram + ((r + o) ^ 3) - ...)
 *     MEM_HU(o, r)  ->  *(uint16_t *)(rdram + ((r + o) ^ 2) - ...)
 *     MEM_W (o, r)  ->  *(int32_t  *)(rdram + ((r + o))     - ...)
 *
 * The 32-bit word has **no** XOR: the interleaving and the host's little-endian
 * cancel out exactly, so that a native read returns the correct guest value.
 * That is counter-intuitive, and reversing it — swapping the bytes by hand "to
 * fix the endianness" — gives absurd addresses that one then blames on the
 * decoder.
 *
 * The flag is zero by default, so the tests do not change behaviour: it is the
 * game that declares the layout it supplies. */
static unsigned char read_u8(const dkr_f3d_context *c, unsigned int a)
{
    return c->rdram[c->rdram_native ? (a ^ 3u) : a];
}

static short read_s16(const dkr_f3d_context *c, unsigned int a)
{
    return (short)(((unsigned)read_u8(c, a) << 8) | read_u8(c, a + 1u));
}

static unsigned int read_u32(const dkr_f3d_context *c, unsigned int a)
{
    /* The fast path is not a luxury: it is the decoder's most frequent read —
       two per command — and the target is a Pentium II. It only holds on an
       aligned address, which display lists are; the general path stays correct
       for everything else. */
    if (c->rdram_native && (a & 3u) == 0u) {
        return *(const unsigned int *)(const void *)(c->rdram + a);
    }
    return ((unsigned)read_u8(c, a)      << 24) | ((unsigned)read_u8(c, a + 1u) << 16) |
           ((unsigned)read_u8(c, a + 2u) <<  8) |  (unsigned)read_u8(c, a + 3u);
}

static void trace(dkr_f3d_context *c, const char *fmt, ...)
{
    char line[192];
    va_list ap;
    if (!c->trace) {
        return;
    }
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    c->trace(c->trace_user, line);
}

static void reject(dkr_f3d_context *c, dkr_f3d_reject why, const char *detail)
{
    c->state.rejects[why]++;
    if (c->state.rejects[why] <= MAX_LOGGED_REJECTS) {
        trace(c, "REJECT %s: %s", dkr_f3d_reject_text(why), detail);
    }
}

/* --- From the game's buffer to the screen ----------------------------------
 *
 * The game reasons inside its colour buffer — 320 pixels wide for DKR — and the
 * card displays at 640x480. The factor is **read** from `SETCOLORIMAGE` rather
 * than assumed, and it serves in two places: filled rectangles and the viewport.
 * Letting them diverge would give a 2D interface and a 3D geometry at two
 * different scales, which is visible but not understandable. */
static float screen_scale(const dkr_f3d_context *c)
{
    if (c->screen_width == 0u || c->state.color_image_width == 0u) {
        return 1.0f;
    }
    return (float)c->screen_width / (float)c->state.color_image_width;
}

/* Declared here because triangle drawing precedes it in this file: the state
   translation lives with the rest of the 2D path, further down. */
static void apply_state(dkr_f3d_context *c);

/* --- The commands ---------------------------------------------------------- */

static void cmd_dma_offsets(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    c->state.matrix_offset = w0 & RDRAM_MASK;
    c->state.vertex_offset = w1 & RDRAM_MASK;
    trace(c, "DMAOffsets matrices=0x%06X vertices=0x%06X",
          c->state.matrix_offset, c->state.vertex_offset);
}

static void cmd_matrix(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    unsigned int index, address;

    /* The low field must be 64 — the size of a matrix. This is not a defensive
       check but the way the microcode tells its variants apart: anything else,
       and the command is not a load. */
    if ((w0 & 0xFFFFu) != MATRIX_BYTES) {
        return;
    }
    index = (w0 >> 16) & 0x0Fu;
    if (index == 0) {
        index = (w0 >> 22) & 0x03u;
    }
    if (index > 2u) { index = 2u; }
    c->state.selected_matrix = index;

    address = (c->state.matrix_offset + w1) & RDRAM_MASK;
    if (!in_range(c, address, MATRIX_BYTES)) {
        char d[64];
        sprintf(d, "matrix at 0x%06X", address);
        reject(c, DKR_F3D_REJECT_ADDRESS, d);
        return;
    }
    {
        dkr_matrix loaded;
        /* `dkr_matrix_from_fixed` reads a byte sequence in plain big-endian.
           Under librecomp's layout it must therefore be flattened first — 64
           bytes, once per matrix command, which weighs nothing next to the
           sixteen multiplications that follow. Passing the raw pointer would
           read matrices whose bytes are permuted four by four: the scenery would
           not crash, it would simply be wrong, and one would look for the error
           in the transformation. */
        unsigned char flat[MATRIX_BYTES];
        const unsigned char *source = c->rdram + address;
        if (c->rdram_native) {
            unsigned int i;
            for (i = 0; i < MATRIX_BYTES; i++) { flat[i] = read_u8(c, address + i); }
            source = flat;
        }
        if (dkr_matrix_from_fixed(source, &loaded)) {
            dkr_transform_set_matrix(&c->transform, (int)index, &loaded);
        }
    }
    dkr_transform_select(&c->transform, (int)index);
    trace(c, "Matrix slot=%u address=0x%06X", index, address);
}

static void cmd_vertex(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned int count       = ((w0 >> 19) & 0x1Fu) + 1u;
    const unsigned int destination = (w0 >> 9) & 0x1Fu;
    const unsigned int source      = (c->state.vertex_offset + w1) & RDRAM_MASK;
    unsigned int i;

    /* The three conditions are distinct and all necessary: a count that is too
       large, a destination that is too far, or a batch that overflows the cache
       through the sum of the two. The third is the one that gets forgotten. */
    if (count > MAX_VERTICES || destination >= MAX_VERTICES ||
        count > MAX_VERTICES - destination) {
        char d[80];
        sprintf(d, "%u vertices at index %u", count, destination);
        reject(c, DKR_F3D_REJECT_COUNT, d);
        return;
    }
    if (!in_range(c, source, count * VERTEX_STRIDE)) {
        char d[64];
        sprintf(d, "vertices at 0x%06X", source);
        reject(c, DKR_F3D_REJECT_ADDRESS, d);
        return;
    }
    for (i = 0; i < count; i++) {
        const unsigned int a = source + i * VERTEX_STRIDE;
        dkr_source_vertex sv;
        /* The DKR vertex: x, y, z as signed 16-bit then r, g, b, a as bytes.
           **No texture coordinates** — they arrive with the triangle. */
        sv.x = read_s16(c, a + 0);
        sv.y = read_s16(c, a + 2);
        sv.z = read_s16(c, a + 4);
        sv.r = read_u8(c, a + 6);
        sv.g = read_u8(c, a + 7);
        sv.b = read_u8(c, a + 8);
        sv.a = read_u8(c, a + 9);
        /* Transformed **here** and not at the triangle: a vertex served by
           three triangles would otherwise be transformed three times, and this
           is the port's heaviest stage. The texture coordinates stay at zero —
           they will be laid down at the triangle. */
        dkr_transform_to_clip(&c->transform, &sv, 0.0f, 0.0f,
                              &c->cache[destination + i]);
        c->cache_valid[destination + i] = 1;
    }
    c->state.vertices += count;
    trace(c, "Vertex %u vertices to %u from 0x%06X", count, destination, source);
}

static void cmd_triangle(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned int count  = ((w0 >> 20) & 0x0Fu) + 1u;
    const unsigned int source = w1 & RDRAM_MASK;
    unsigned int i;

    if (count == 0u) {
        reject(c, DKR_F3D_REJECT_COUNT, "zero triangles");
        return;
    }
    if (!in_range(c, source, count * TRIANGLE_STRIDE)) {
        char d[64];
        sprintf(d, "triangles at 0x%06X", source);
        reject(c, DKR_F3D_REJECT_ADDRESS, d);
        return;
    }
    /* **The whole batch is validated before the first one is drawn.**
     *
     * Validating as we go would let the valid triangles be drawn before the
     * batch is rejected, which makes the defect content-dependent — hence hard
     * to reproduce. The original decoder works this way and this extraction
     * keeps it. */
    for (i = 0; i < count; i++) {
        const unsigned int a = source + i * TRIANGLE_STRIDE;
        if (read_u8(c, a + 1) >= MAX_VERTICES ||
            read_u8(c, a + 2) >= MAX_VERTICES ||
            read_u8(c, a + 3) >= MAX_VERTICES) {
            char d[80];
            sprintf(d, "batch of %u at 0x%06X, triangle %u", count, source, i);
            reject(c, DKR_F3D_REJECT_INDEX, d);
            return;
        }
    }
    c->state.triangles += count;
    trace(c, "Triangle %u from 0x%06X", count, source);

    /* --- Emission, and this is where the chain closes ----------------------- *
     *
     * Every triangle goes through: texture coordinates laid down per corner,
     * near-plane clipping, projection, culling, off-screen rejection. */
    for (i = 0; i < count; i++) {
        const unsigned int a = source + i * TRIANGLE_STRIDE;
        const unsigned char flags = read_u8(c, a + 0);
        const unsigned char idx[3] = { read_u8(c, a + 1), read_u8(c, a + 2),
                                       read_u8(c, a + 3) };
        dkr_clip_vertex   tri[3], clipped[6];
        dkr_render_vertex out[6];
        int pieces, k, corner, emitted_here = 0;
        dkr_cull_mode cull;

        for (corner = 0; corner < 3; corner++) {
            if (!c->cache_valid[idx[corner]]) {
                /* A valid index pointing at a slot that was never loaded: the
                   display list uses a vertex it has not defined. This is not a
                   wrong address, hence not a range rejection — but drawing an
                   uninitialised vertex would give random geometry, which is
                   worse than a missing triangle. */
                reject(c, DKR_F3D_REJECT_INDEX, "vertex not loaded");
                emitted_here = -1;
                break;
            }
            tri[corner] = c->cache[idx[corner]];
            /* The corner's s, t, as signed 16-bit. This is where they come in
               — the vertex did not carry them. */
            /* --- The normalisation that was missing ---------------------- *
             *
             * The microcode gives s and t in **10.5 fixed point**: thirty-two
             * steps per texel. `dkr_clip_project`, for its part, expects [0,1] —
             * its comment says so — and then applies Glide's scale of 256.
             * Between the two, the division by 32 and by the texture's width was
             * missing.
             *
             * The order of magnitude of the error says why nothing was
             * sampling: for a 32-texel texture, a right-hand corner is 1024 raw,
             * hence 262,144 after Glide's scale instead of 256. That is not an
             * offset texture, it is a texture outside everything.
             *
             * **The padded width, not the real one**: the texture only occupies
             * the top-left corner of what was uploaded, since power-of-two
             * padding enlarged it. Normalising over the real width would stretch
             * the pattern by a factor of up to two. */
            {
                const float sb = (float)read_s16(c, a + 4 + corner * 4);
                const float tb = (float)read_s16(c, a + 6 + corner * 4);
                tri[corner].s = sb * c->tex_scale_s;
                tri[corner].t = tb * c->tex_scale_t;
                /* The measurement that can refute the interpretation above: if
                   10.5 is the right format and the width the right one, the
                   extremes must stay in the neighbourhood of [0,1]. Thousands
                   would say the scale is wrong, and saying it in figures rather
                   than on screen is the whole point. */
                if (tri[corner].s < c->state.s_min) { c->state.s_min = tri[corner].s; }
                if (tri[corner].s > c->state.s_max) { c->state.s_max = tri[corner].s; }
                if (tri[corner].t < c->state.t_min) { c->state.t_min = tri[corner].t; }
                if (tri[corner].t > c->state.t_max) { c->state.t_max = tri[corner].t; }
            }
        }
        if (emitted_here < 0) {
            continue;
        }

        /* --- Is the triangle degenerate in texture space? -------------------- *
         *
         * `s_min`/`s_max` above are extremes over the **whole run**: they can be
         * wide while every individual triangle samples a single point, and the
         * two look identical in a log. The frame brought back on 17 August 2026
         * is one colour under `DKR_FORCE_COMBINE=texel`, with thousands of
         * distinct textures bound and none skipped, so "every triangle samples
         * one texel of its own texture" is exactly the reading those extremes
         * cannot rule out.
         *
         * Counted per triangle, before clipping introduces interpolated corners
         * of its own. */
        if (tri[0].s == tri[1].s && tri[1].s == tri[2].s &&
            tri[0].t == tri[1].t && tri[1].t == tri[2].t) {
            c->state.tri_st_degenerate++;
        } else {
            c->state.tri_st_varying++;
        }

        pieces = dkr_clip_near(tri, clipped);
        if (pieces == 0) {
            c->state.clipped_away++;
            continue;
        }
        if (pieces == 2) {
            c->state.clip_split++;
        }

        /* Bit 0x40 disables back-face culling; otherwise the direction comes
           from the sign of the viewport's x scale. */
        cull = dkr_cull_mode_for_viewport(c->transform.viewport_scale_x,
                                          (flags & 0x40u) == 0);

        for (k = 0; k < pieces; k++) {
            dkr_render_vertex *v = &out[k * 3];
            dkr_clip_project(&c->transform, &clipped[k * 3 + 0], &v[0]);
            dkr_clip_project(&c->transform, &clipped[k * 3 + 1], &v[1]);
            dkr_clip_project(&c->transform, &clipped[k * 3 + 2], &v[2]);
            if (!dkr_cull_accept(v, cull)) {
                c->state.culled++;
                continue;
            }
            if (dkr_clip_reject_offscreen(v, 640, 480, DKR_CLIP_DEFAULT_MARGIN)) {
                c->state.clipped_away++;
                continue;
            }
            apply_state(c);
            /* **What the emitted triangles are made of.**
             *
             * The screen stays white while textures upload and the coordinates
             * sit in the right order of magnitude. Three causes remain possible
             * and a single figure — "emitted" — conflates them: a combiner that
             * reads no texel, a texture that is not bound, or silent sampling.
             * The first two are counted here, and that is three integers
             * against another hypothesis picked at random. */
            if (c->render_state.combine < DKR_COMBINE_COUNT) {
                c->state.emitted_per_combine[c->render_state.combine]++;
            }
            if (c->render_state.texture != 0) {
                c->state.emitted_textured++;
            }
            /* **The size of the triangles on screen.**
             *
             * 490 triangles per frame are emitted, and the screen shows only
             * one, enormous. Both cannot be true at once without something else
             * being wrong, and "emitted" does not say which. A distribution
             * dominated by triangles of more than ten thousand pixels would
             * accuse the projection or the matrices; a normal distribution would
             * say instead that the geometry is right and that sampling is what
             * is missing.
             *
             * Area through the cross product, in absolute value and without
             * dividing: we are not after the exact area but the order of
             * magnitude, and a square root per triangle would be paid for. */
            {
                const float ax = v[1].x - v[0].x, ay = v[1].y - v[0].y;
                const float bx = v[2].x - v[0].x, by = v[2].y - v[0].y;
                float area = (ax * by - ay * bx) * 0.5f;
                if (area < 0.0f) { area = -area; }
                if (area < 1.0f)         { c->state.area[0]++; }
                else if (area < 100.0f)  { c->state.area[1]++; }
                else if (area < 10000.0f){ c->state.area[2]++; }
                else                     { c->state.area[3]++; }
                /* **The largest, kept as a number rather than a bucket.**
                 *
                 * The top bucket runs from ten thousand pixels to the whole
                 * screen, and those two answer opposite questions: seventeen
                 * modest quads tiling the view mean a scene that renders flat,
                 * one quad of 307,200 pixels means a screen legitimately showing
                 * a single surface. The histogram cannot separate them, and that
                 * is the third aggregate today to hide the thing it was built to
                 * show. */
                if (area > c->state.area_max) {
                    c->state.area_max = (unsigned long)area;
                }
                {
                    int q;
                    for (q = 0; q < 3; q++) {
                        const long px = (long)v[q].x;
                        const long py = (long)v[q].y;
                        if (px < c->state.proj_x_min) { c->state.proj_x_min = px; }
                        if (px > c->state.proj_x_max) { c->state.proj_x_max = px; }
                        if (py < c->state.proj_y_min) { c->state.proj_y_min = py; }
                        if (py > c->state.proj_y_max) { c->state.proj_y_max = py; }
                    }
                }
            }
            /* **The depth mode at draw time.**
             *
             * The area distribution is normal — 45% of triangles under a hundred
             * pixels — so the geometry is not degenerate. But the screen is
             * covered by a single large polygon, which is exactly what a missing
             * depth sort produces: the 18% of triangles over ten thousand pixels
             * cover everything drawn before. Counting the modes says whether the
             * test is active, rather than assuming it from the code that
             * translates it. */
            if (c->render_state.depth < 4) {
                c->state.emitted_per_depth[c->render_state.depth]++;
            }
            /* **The range of depths handed over.**
             *
             * Glide in W-buffer mode consumes `oow` directly. The screen has
             * been black since the test was switched on, and two very different
             * causes give exactly that symptom: an inverted comparison direction
             * — already recorded in `win95-glide-states.md` — or degenerate
             * depths. Opening the write mask during the clear changed nothing,
             * so the first is pushed one notch aside.
             *
             * We therefore measure the test's input. `oow` values that are all
             * equal, negative, or outside the range Glide encodes would explain
             * the black without any convention being at fault. */
            /* **Blending and the alpha test, counted like depth.**
             *
             * The `G_RDPSETOTHERMODE` fix did not only rewrite the cycle mode:
             * the low half also carries the blender and the alpha comparison.
             * Three causes can blacken the screen and I checked only one — that
             * is exactly the mistake that cost a useless fix on the texture
             * refusals. We separate them before fixing any one of them. */
            if (c->render_state.blend < 8) {
                c->state.emitted_per_blend[c->render_state.blend]++;
            }
            if (c->render_state.alpha_test) {
                c->state.emitted_alpha_test++;
                if (c->render_state.alpha_reference > c->state.alpha_ref_max) {
                    c->state.alpha_ref_max = c->render_state.alpha_reference;
                }
            }
            {
                int q;
                for (q = 0; q < 3; q++) {
                    const float o = v[q].oow;
                    if (o < c->state.oow_min) { c->state.oow_min = o; }
                    if (o > c->state.oow_max) { c->state.oow_max = o; }
                    /* **The vertex colour.**
                     *
                     * The chosen combiner is `texel * shade`, the texture is
                     * bound, blending is opaque for one hundred and seventy-five
                     * thousand triangles — and the screen is black. One of those
                     * three inputs is zero. The vertex colour is the one we can
                     * read without reading the card back, hence the one to start
                     * with. A zero shade multiplies the texel by zero and gives
                     * exactly black, whatever the texels. */
                    if (v[q].r > c->state.shade_max) { c->state.shade_max = v[q].r; }
                    if (v[q].g > c->state.shade_max) { c->state.shade_max = v[q].g; }
                    if (v[q].b > c->state.shade_max) { c->state.shade_max = v[q].b; }
                    if (v[q].a > c->state.alpha_max) { c->state.alpha_max = v[q].a; }
                }
            }
            if (c->backend && c->backend->draw_triangles) {
                c->backend->draw_triangles(c->backend->self, v, 1);
            }
            c->state.emitted++;
        }
    }
}

static void cmd_move_word(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned char type = (unsigned char)(w0 & 0xFFu);
    if (type == MOVEWORD_PRESENT &&
        (w1 & ~PRESENT_META_MASK) == PRESENT_MAGIC) {
        /* An extension of the port, not of the original microcode: the magic
           word "DKR\0" tells the commands added by the modern engine apart from
           the game's. */
        trace(c, "PresentationGroup mode=%u", w1 & 7u);
    } else if (type == MOVEWORD_BILLBOARD) {
        c->state.billboard = (unsigned char)(w1 & 1u);
        trace(c, "MoveWord billboard=%u", c->state.billboard);
    } else if (type == MOVEWORD_MVPMATRIX) {
        unsigned int m = (w1 >> 6) & 0x03u;
        if (m > 2u) { m = 2u; }
        c->state.selected_matrix = m;
        dkr_transform_select(&c->transform, (int)m);
        trace(c, "MoveWord matrix=%u", m);
    } else {
        trace(c, "MoveWord type=0x%02X value=0x%08X", type, w1);
    }
}


/* --- The RDP state, accumulated then translated ----------------------------- *
 *
 * `SETOTHERMODE_H` and `_L` are **partial writes**: each command replaces one
 * field of the mode word without touching the rest. The encoding is F3D's —
 * shift in bits 8..15, length in bits 0..7, data **already shifted** in `w1`:
 *
 *     0xBA001402 w1=0x00000000   shift 20, length 2  -> cycle type
 *     0xBA001701 w1=0x00800000   shift 23, length 1  -> the bit is already there
 *     0xB900031D w1=0x0F0A4000   shift  3, length 29 -> render mode
 *
 * The three samples come from the machine, not from a header: F3DEX2 inverts the
 * shift, and picking the wrong family would give fields adjacent to the intended
 * ones — a filter instead of a cycle type, for instance, that is, a plausible
 * and wrong image rather than a frank error.
 *
 * The state is only translated at draw time. Doing it on every write would cost
 * a complete translation per command, and there are more than six thousand per
 * frame; doing it at draw time pays for it once per real change. */
static void write_othermode(unsigned int *word, unsigned int w0, unsigned int w1)
{
    const unsigned int sft = (w0 >> 8) & 0xFFu;
    const unsigned int len = w0 & 0xFFu;
    unsigned int mask;
    if (len == 0u || len > 32u || sft >= 32u) {
        return;
    }
    mask = (len >= 32u) ? 0xFFFFFFFFu : (((1u << len) - 1u) << sft);
    *word = (*word & ~mask) | (w1 & mask);
}

/* Translates the accumulated RDP state and hands it to the backend, if anything
   has changed since the last draw. */
static void apply_state(dkr_f3d_context *c)
{
    dkr_rdp_state rdp;
    int exact = 1;

    if (!c->state_dirty) {
        return;
    }
    c->state_dirty = 0;

    memset(&rdp, 0, sizeof(rdp));
    dkr_rdp_decode_othermode(c->mode_h, c->mode_l, &rdp);
    rdp.combiner = c->combiner;

    /* The decoded cycle type checks itself: during a `FILLRECT` it must be
       `FILL`. A misplaced shift would put it elsewhere, and this counter would
       say so without anyone having to look at the screen. */
    c->state.current_cycle = (unsigned char)rdp.cycle;

    /* --- The safety net `rdp_state.h` demands, and that nobody held --------- *
     *
     * "A case that is not catalogued must **announce itself** rather than render
     * wrongly in silence. A missed configuration is invisible at decode time —
     * it shows up on screen, as a surface in an unexpected colour, possibly in a
     * single level."
     *
     * The neighbouring port's inventory counts 33 configurations. The key
     * identifies them exactly; `dkr_cc_lookup` returns NULL for the others. So
     * we count, and we keep the first unknown keys — a count alone would say
     * some are missing, not which, and that is the difference between a figure
     * and a lead.
     *
     * **It is `dkr_cc_lookup` and not `dkr_rdp_combiner_name`**, and that
     * distinction was worth a run on the machine to find. There used to be two
     * catalogues: `CC_TABLE`, generated from the game's own static tables, and
     * an eight-entry `KNOWN` written by hand. This counter asked the hand-written
     * one, which was transcribed in a shorthand where `0` meant zero — whereas
     * the RDP spells zero `8` in a 4-bit `b`, `16` in a 5-bit `c` and `7` in a
     * 3-bit `d`. Every entry of it was therefore unmatchable, and the counter
     * read `catalogued=0` over 24,286 applications while claiming to be a safety
     * net. A hand transcription that goes wrong in silence is exactly what
     * generating the table was meant to avoid. */
    {
        const unsigned long long key = dkr_rdp_combiner_key(&rdp.combiner, rdp.cycle);
        if (dkr_cc_lookup(key) != 0) {
            c->state.combiners_known++;
        } else {
            unsigned i;
            int seen = 0;
            c->state.combiners_unknown++;
            for (i = 0; i < c->state.unknown_keys_n; i++) {
                if (c->state.unknown_keys[i] == key) { seen = 1; break; }
            }
            if (!seen && c->state.unknown_keys_n < 8u) {
                /* **The key is not enough.** It identifies a configuration; it
                   does not say what that configuration computes, so it does not
                   allow it to be added to the table. We keep the composition,
                   which is what is needed to name it against the `G_CC_*`
                   macros. */
                const unsigned i2 = c->state.unknown_keys_n;
                c->state.unknown_keys[i2] = key;
                c->state.unknown_combiners[i2] = rdp.combiner;
                c->state.unknown_cycle[i2] = (unsigned char)rdp.cycle;
                c->state.unknown_keys_n++;
            }
        }
    }

    dkr_rdp_to_render_state(&rdp, &c->render_state, &exact);
    /* A diagnostic switch, not a workaround.
     *
     * Three causes can blacken the screen and two have been ruled out by
     * measurement. The third — depth — cannot be refuted by looking at it: its
     * inputs are sound, its configuration is the one E05-S05 measured, and it
     * blackens all the same. Switching it off for one notch answers, in a single
     * race, a question inspection does not settle, and we keep the switch: it
     * will serve again every time a doubt bears on sorting rather than on what
     * is drawn. */
    if (c->no_depth) {
        c->render_state.depth = DKR_DEPTH_DISABLED;
    }
    /* The same reasoning one step further along the pipeline. `no_depth`
       separates sorting from drawing; this separates the texel from the shade.
       Both are kept: on a target where a run costs four minutes, a switch that
       answers in one race is worth more than the line it occupies. */
    if (c->force_combine) {
        c->render_state.combine =
            (dkr_combine_mode)(c->force_combine - 1u);
    }
    /* --- The texture handle does not survive the translation ---------------- *
     *
     * `dkr_rdp_to_render_state` fills **the whole** block from the RDP state,
     * and the RDP state knows nothing of our handles: the `texture` field the
     * upload had just placed there was therefore overwritten on every
     * application.
     *
     * Measured, and that is what named the cause without a detour: 45,773
     * textures uploaded, 246,707 triangles emitted with a combiner that reads a
     * texel, and **zero triangles emitted with a texture bound**. Three figures
     * which, separated, leave only one explanation; gathered under "emitted",
     * they left none.
     *
     * The handle therefore lives in the context, which is its proper place — it
     * is a decoder resource, not an RDP mode — and it is laid back down after
     * the translation. */
    c->render_state.texture = c->bound_texture;
    if (!exact) {
        /* **An approximate translation that does not announce itself is worse
           than a failure**: it produces a plausible, wrong image. The counter is
           the safety net `rdp_state.h` explicitly demands. */
        c->state.states_approximate++;
    }
    c->state.states_applied++;

    if (c->backend && c->backend->set_state) {
        c->backend->set_state(c->backend->self, &c->render_state);
    }
}



/* --- Textures --------------------------------------------------------------- *
 *
 * Three commands carry the information, and **none is enough on its own**:
 *
 *     SETTIMG      (0xFD)  format, size, RDRAM address
 *     SETTILE      (0xF5)  the tile's format and size, wrapping
 *     SETTILESIZE  (0xF2)  the dimensions, in 10.2 fixed point
 *
 * Measured on the machine, DKR's sequence:
 *
 *     0xFD100000 w1=0x00252D60   RGBA, 16 bits, address 0x252D60
 *     0xF5100000 w1=0x07080200   tile 7
 *     0xF3000000 w1=0x077FF100   LoadBlock
 *     0xF5101000 w1=0x00080200   tile 0
 *     0xF2000000 w1=0x0007C0FC   lrs=124, lrt=252 -> 32x64 texels
 *
 * We upload at `SETTILESIZE` because it is the last of the three: before it the
 * dimensions are unknown, and uploading on `SETTIMG` would give a texture of an
 * invented size. The order is the microcode's, not a convention we pick.
 *
 * The cache key gathers address, format, size and dimensions. The address alone
 * would not do: DKR reuses its buffers, and two different textures can share an
 * address from one frame to the next. A key that is too short does not crash —
 * it displays the old texture, which gets noticed late. */
/* --- What the Voodoo accepts, and what the N64 sends ------------------------ *
 *
 * The RDP samples any dimensions; the Voodoo requires **powers of two**, a side
 * of at most 256, and a ratio of at most 8:1.
 *
 * Measured on the machine, once the four causes of refusal were separated:
 *
 *     refusal-detail: aspect=21084 size=0 slots=0 tmu-memory=0
 *
 * **All** the refusals came from there, and none from memory — which invalidated
 * the previous fix, made on the assumption that exhaustion was to blame.
 * Separating the causes cost four integers; conflating them had cost a fix.
 *
 * So we pad up to the next power of two and keep the ratio, which the texture
 * coordinates need: the real texture now only occupies the top-left corner.
 *
 * **What padding spoils, and had better be said**: a repeated texture will show
 * its padding at the seams, since wrapping happens over the padded size and not
 * over the real one. DKR uses wrapping eighteen times against clamping four
 * times, so the question will come up. The clean answer is to repeat the pattern
 * into the padding rather than leave it empty; that is what is done here. */
static int next_power_of_two(int n)
{
    int p = 1;
    while (p < n && p < 256) { p <<= 1; }
    return p;
}

static void cmd_set_tile_size(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned int lrs = (w1 >> 12) & 0xFFFu;
    const unsigned int lrt = w1 & 0xFFFu;
    const unsigned int uls = (w0 >> 12) & 0xFFFu;
    const unsigned int ult = w0 & 0xFFFu;
    /* 10.2 fixed point, and both corners are **inclusive** — as with the filled
       rectangle, and for the same RDP convention reason. */
    const int width  = (int)((lrs >> 2) - (uls >> 2)) + 1;
    const int height = (int)((lrt >> 2) - (ult >> 2)) + 1;
    unsigned long long key;

    if (width <= 0 || height <= 0) {
        return;
    }

    key = ((unsigned long long)c->timg_address << 24)
        ^ ((unsigned long long)c->timg_format << 20)
        ^ ((unsigned long long)c->timg_size   << 18)
        ^ ((unsigned long long)width << 9)
        ^ (unsigned long long)height;

    if (key == c->texture_key && c->render_state.texture != 0) {
        /* Already uploaded and still bound: nothing to do. Without this test we
           would reconvert the same texture thousands of times per frame, and on
           a Pentium II that alone would be enough to make the port
           unplayable. */
        c->state.textures_reused++;
        return;
    }

    {
        const int pl = next_power_of_two(width);
        const int ph = next_power_of_two(height);
        /* The card's ratio of at most 8:1. We cannot pad to satisfy it — that
           would amount to multiplying memory by eight — so we refuse, and we
           count it rather than keep quiet about it. */
        const int big   = (pl > ph) ? pl : ph;
        const int small = (pl > ph) ? ph : pl;
        if (big > 256 || (small > 0 && big / small > 8)) {
            c->render_state.texture = 0;
            c->bound_texture = 0;
            c->texture_key = 0;
            c->state.textures_bad_aspect++;
            c->state_dirty = 1;
            return;
        }
        c->tex_width = width;
        c->tex_height = height;
        c->tex_padded_width = pl;
        c->tex_padded_height = ph;
    }

    if (!dkr_texture_convert(c->rdram, c->rdram_size, c->rdram_native,
                             c->timg_address,
                             (dkr_n64_format)c->timg_format,
                             (dkr_n64_size)c->timg_size,
                             width, height, c->texels, &c->state.textures)) {
        /* Refused: we **unbind** rather than draw with the previous one. A stale
           texture on a surface is more confusing than a surface with no texture,
           because it passes for rendering. */
        c->render_state.texture = 0;
        c->bound_texture = 0;
        c->texture_key = 0;
        c->state_dirty = 1;
        return;
    }

    /* The padding, in place and bottom to top so as not to overwrite what is
       being copied. The pattern is **repeated** rather than left empty: that is
       what makes the padding invisible when the texture is wrapped, and it costs
       nothing more than a zero fill. */
    if (c->tex_padded_width != width || c->tex_padded_height != height) {
        int y, x;
        for (y = c->tex_padded_height - 1; y >= 0; y--) {
            const int sy = y % height;
            for (x = c->tex_padded_width - 1; x >= 0; x--) {
                const int sx = x % width;
                c->texels[(size_t)y * (size_t)c->tex_padded_width + (size_t)x] =
                    c->texels[(size_t)sy * (size_t)width + (size_t)sx];
            }
        }
        c->state.textures_padded++;
    }

    /* **The texture's content, after conversion.**
     *
     * The texel is the last of the combiner's three inputs we had not looked at:
     * the vertex colour reaches 255, the chosen combiner does read the texel,
     * the texture is bound. If the texels are zero, so is the product — and that
     * is black, whatever the other two are worth.
     *
     * We count the non-zero texels rather than print any: an entirely black
     * texture is a fact, not a value to read. And we only do it on upload, not
     * at draw time. */
    {
        unsigned int i, n = (unsigned int)c->tex_padded_width *
                            (unsigned int)c->tex_padded_height;
        unsigned int seen = 0;
        for (i = 0; i < n; i++) {
            if ((c->texels[i] & 0xFFFEu) != 0u) { seen++; }
        }
        if (seen == 0u) { c->state.textures_black++; }
        else            { c->state.textures_with_content++; }
    }

    if (c->backend && c->backend->texture_upload) {
        dkr_texture_desc d;
        dkr_texture_handle h;
        memset(&d, 0, sizeof(d));
        d.key = key;
        d.format = DKR_TEXFMT_RGBA5551;
        d.width = c->tex_padded_width;
        d.height = c->tex_padded_height;
        d.pixels = c->texels;
        d.size_bytes = (size_t)c->tex_padded_width *
                       (size_t)c->tex_padded_height * 2u;
        h = c->backend->texture_upload(c->backend->self, &d);
        if (h != 0) {
            c->bound_texture = h;
            /* 1/32 for the microcode's 10.5, 1/width to get into [0,1]. Both
               in a single multiplication per vertex: transformation is already
               the port's heaviest stage. */
            c->tex_scale_s = 1.0f / (32.0f * (float)c->tex_padded_width);
            c->tex_scale_t = 1.0f / (32.0f * (float)c->tex_padded_height);
            c->render_state.texture = h;
            c->texture_key = key;
            c->state_dirty = 1;
            c->state.textures_loaded++;
        } else {
            /* Texture memory full. E05-S02 administers it; here we merely
               refrain from drawing with an invalid handle. */
            c->render_state.texture = 0;
            c->bound_texture = 0;
            c->texture_key = 0;
            c->state.textures_refused++;
        }
    }

    trace(c, "SetTileSize %dx%d %s at 0x%06X", width, height,
          dkr_texture_format_name((dkr_n64_format)c->timg_format,
                                  (dkr_n64_size)c->timg_size),
          c->timg_address);
}

/* --- The viewport: the game's, not the one we assume ------------------------ *
 *
 * Until now the viewport came from `dkr_transform_init`'s default — 640x480,
 * plausible and wrong. The measured symptom: a single triangle covering half the
 * screen, while the geometry and the shading were correct.
 *
 * `MOVEMEM` with index `0x80` carries it, in sixteen bytes. The sample measured
 * on the machine:
 *
 *     opcode 0x03 w0=0x03800010 w1=0x000DD148
 *                    ^^ index   ^^^^ sixteen bytes
 *
 * The structure is `short vscale[4]` then `short vtrans[4]`, in 2.2 fixed point
 * — hence the division by four. The last two components carry depth and are not
 * used here: our depth range is the backend's, established by E05-S05.
 *
 * **The sign in y flips.** The game gives a positive scale; `dkr_transform`'s
 * convention wants it negative, like its own default. Skipping that would flip
 * the image top to bottom — visible, but easy to attribute to the projection
 * rather than to a sign convention. */
static void cmd_viewport(dkr_f3d_context *c, unsigned int address)
{
    const short sx = read_s16(c, address + 0u);
    const short sy = read_s16(c, address + 2u);
    const short tx = read_s16(c, address + 8u);
    const short ty = read_s16(c, address + 10u);
    const float scale = screen_scale(c);

    /* A zero viewport is not a viewport: it would project every vertex onto the
       same point, which looks like a wrong matrix. We then keep the one we had
       rather than install an unusable one. */
    if (sx == 0 || sy == 0) {
        trace(c, "Viewport ignored: zero scale");
        return;
    }

    dkr_transform_set_viewport(&c->transform,
                               ((float)sx / 4.0f) * scale,
                               -((float)sy / 4.0f) * scale,
                               ((float)tx / 4.0f) * scale,
                               ((float)ty / 4.0f) * scale);
    c->state.viewports++;
    trace(c, "Viewport scale=%d,%d translation=%d,%d (x%d/100)",
          sx / 4, sy / 4, tx / 4, ty / 4, (int)(scale * 100.0f));
}

/* --- The filled rectangle --------------------------------------------------- *
 *
 * Measured on the machine before being written: across the 47,000 commands of
 * the startup sequence, **`FILLRECT` is the only draw order emitted** — no
 * vertex, no triangle, no textured rectangle, two fills per frame. This path is
 * therefore not a detail of the 2D: it is everything that puts pixels on screen
 * at this stage of the port.
 *
 * ## The coordinates
 *
 * `gDPFillRectangle` stores the two corners in the two words, in 10.2 fixed
 * point, and **the bottom-right corner is inclusive**:
 *
 *     w0 = opcode<<24 | lrx<<14 | lry<<2
 *     w1 =              ulx<<14 | uly<<2
 *
 * Forgetting the inclusion gives a rectangle one pixel short at the bottom and
 * on the right. On a full-screen clear that leaves one line of the background
 * visible, which one blames on the rasteriser rather than on the convention.
 *
 * ## The scale
 *
 * The coordinates live in the space of the game's colour buffer, not that of the
 * screen. The factor is **read** from `SETCOLORIMAGE`, which carries the width,
 * rather than assuming the N64's usual 320 pixels: DKR switches buffers along
 * the way, and an assumed scale would produce offset scenery on some screens
 * only — the kind of defect that takes hours to connect to its cause.
 *
 * ## The colour
 *
 * In 16-bit fill mode, `SETFILLCOLOR` carries **two RGBA5551 pixels side by
 * side**, because the RDP writes two pixels per cycle. We take the low sixteen
 * bits: the two halves are identical for a plain fill, and a half-wrong colour
 * would be more confusing than a frankly wrong one. */
static unsigned int colour_from_5551(unsigned int pixel)
{
    const unsigned int r = (pixel >> 11) & 0x1Fu;
    const unsigned int g = (pixel >>  6) & 0x1Fu;
    const unsigned int b = (pixel >>  1) & 0x1Fu;
    /* Replicating the high bits rather than shifting alone: 31 must give 255 and
       not 248, otherwise white is never white. */
    const unsigned int r8 = (r << 3) | (r >> 2);
    const unsigned int g8 = (g << 3) | (g >> 2);
    const unsigned int b8 = (b << 3) | (b >> 2);
    return (r8 << 16) | (g8 << 8) | b8;
}

static void cmd_fill_rect(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    /* 10.2 fixed point: two fraction bits, which we drop. The RDP fills by
       whole pixels in fill mode. */
    const int lrx = (int)((w0 >> 14) & 0x3FFu);
    const int lry = (int)((w0 >>  2) & 0x3FFu);
    const int ulx = (int)((w1 >> 14) & 0x3FFu);
    const int uly = (int)((w1 >>  2) & 0x3FFu);

    const float screen_w = 2.0f * c->transform.viewport_scale_x;
    const float screen_h = -2.0f * c->transform.viewport_scale_y;
    float scale_x = 1.0f, scale_y = 1.0f;
    int x0, y0, x1, y1;

    if (!c->backend || !c->backend->fill_rect) {
        trace(c, "FillRect ignored: no backend");
        return;
    }

    if (c->state.color_image_width > 0u && screen_w > 0.0f) {
        scale_x = screen_scale(c);
        /* The buffer's height is carried by no command — the RDP does not know
           it, it only has the width and the address. So we apply the same factor
           as in x, which is right as long as the buffer has the screen's aspect
           ratio. That is DKR's case (320x240 for 640x480) and it is an
           assumption to be revisited the day it stops holding. */
        scale_y = scale_x;
    }
    (void)screen_h;

    x0 = (int)((float)ulx * scale_x);
    y0 = (int)((float)uly * scale_y);
    /* +1: the bottom-right corner is inclusive on the RDP side, exclusive on the
       backend side. */
    x1 = (int)((float)(lrx + 1) * scale_x);
    y1 = (int)((float)(lry + 1) * scale_y);

    /* A check that costs nothing and fires on its own: the RDP only fills in
       `FILL` mode. A misplaced shift in the mode-word write would show up here,
       in figures, rather than on screen as a surface in an unexpected colour. */
    {
        dkr_rdp_state check;
        memset(&check, 0, sizeof(check));
        dkr_rdp_decode_othermode(c->mode_h, c->mode_l, &check);
        if (check.cycle != DKR_CYCLE_FILL) { c->state.fills_wrong_cycle++; }
    }
    c->backend->fill_rect(c->backend->self, x0, y0, x1, y1,
                          c->state.fill_color_argb);
    c->state.rects++;
    trace(c, "FillRect %d,%d..%d,%d colour=0x%06X",
          x0, y0, x1, y1, c->state.fill_color_argb);
}

/* --- The loop -------------------------------------------------------------- */

unsigned long dkr_f3d_run(dkr_f3d_context *c, unsigned int address)
{
    unsigned int return_stack[MAX_NESTED];
    /* --- What ends a counted list ----------------------------------------- *
     *
     * A counted list has **no** `ENDDL`: its count is what ends it. The decoder
     * ignored that — it pushed the return address, jumped, and waited for an
     * `ENDDL` that would never come. So it fell out of the bottom of the list
     * and carried on into the memory that follows, until it hit randomness.
     *
     * The symptom, measured on the machine: **seventy commands per list,
     * constant, two fills and not one triangle**, and one rejection per frame.
     * DKR's display list uploads a texture through a counted list of seven
     * commands, and all the geometry comes *after* that return. It was lost
     * there, every frame, from the start.
     *
     * `NO_COUNT` tells "until `ENDDL`" from "not one command more". Without that
     * sentinel, zero would mean both, and an ordinary list would end at its
     * first command. */
    unsigned int remaining_stack[MAX_NESTED];
    unsigned int remaining = NO_COUNT;
    unsigned int depth = 0;
    unsigned long executed = 0;
    int running = 1;

    if (!c || !c->rdram) {
        return 0;
    }
    address &= RDRAM_MASK;

    while (running) {
        unsigned int w0, w1, opcode;

        if (!in_range(c, address, 8u)) {
            char d[64];
            sprintf(d, "command at 0x%06X", address);
            reject(c, DKR_F3D_REJECT_ADDRESS, d);
            break;
        }
        w0 = read_u32(c, address);
        w1 = read_u32(c, address + 4u);
        opcode = (w0 >> 24) & 0xFFu;
        address += 8u;
        executed++;
        c->state.commands++;
        c->state.opcodes[opcode]++;
        /* Decremented before executing, so that the value pushed by a nested
           call is the parent's **after** this command. Decrementing it
           afterwards would count it again on return. */
        if (remaining != NO_COUNT && remaining > 0u) { remaining--; }

        switch (opcode) {
        case OP_DMAOFFSETS: cmd_dma_offsets(c, w0, w1); break;
        case OP_MATRIX:     cmd_matrix(c, w0, w1);      break;
        case OP_VERTEX:     cmd_vertex(c, w0, w1);      break;
        case OP_TRIANGLE:   cmd_triangle(c, w0, w1);    break;
        case OP_MOVEWORD:   cmd_move_word(c, w0, w1);   break;

        case OP_TEXOFFSET:
            /* **`w1` is an RDRAM address, not a pair of offsets.**
             *
             * This decoder read `(w1 >> 16)` and `(w1 & 0xFFFF)` as sixteen-bit
             * `s` and `t` offsets. The neighbouring port, which runs, makes
             * something else entirely of it:
             * `data.texture_offset = w1 & 0x00FFFFFF`, an **addressing base for
             * texture loading**, and the command resets the shift and the count
             * to zero.
             *
             * The error would not have shown up straight away. An address base
             * read as two texture offsets produces absurd coordinates on the
             * surfaces concerned — hence a displaced pattern, not an absence —
             * and one would have looked at texture decoding. E05-S07 asked for
             * this behaviour to be measured rather than assumed; that is what
             * revealed it. */
            c->state.texture_offset = w1 & 0x00FFFFFFu;
            c->state.texture_shift  = 0;
            c->state.texture_count  = 0;
            trace(c, "TextureOffset base=0x%06X", c->state.texture_offset);
            break;

        case OP_DLBRANCH: {
            /* Aligned to eight bytes — the size of a command. A misaligned
               target would decode straddling words and produce fanciful
               opcodes. */
            const unsigned int target = w1 & 0x00FFFFF8u;
            const int branch = ((w0 >> 16) & 0x01u) != 0;
            if (!in_range(c, target, 8u)) {
                char d[64];
                sprintf(d, "list at 0x%06X", target);
                reject(c, DKR_F3D_REJECT_ADDRESS, d);
                break;
            }
            if (!branch) {
                if (depth >= MAX_NESTED) {
                    char d[48];
                    sprintf(d, "depth %u", depth);
                    reject(c, DKR_F3D_REJECT_DEPTH, d);
                    break;
                }
                remaining_stack[depth] = remaining;
                return_stack[depth++] = address;
                remaining = NO_COUNT;   /* a called list runs to its ENDDL */
            }
            trace(c, "DisplayList %s to 0x%06X",
                  branch ? "branch" : "call", target);
            address = target;
            break;
        }

        case OP_ENDDL:
            if (depth == 0u) {
                trace(c, "EndDisplayList - end");
                running = 0;
            } else {
                address = return_stack[--depth];
                remaining = remaining_stack[depth];
                trace(c, "EndDisplayList - return to 0x%06X", address);
            }
            break;

        case OP_DLCOUNTED: {
            const unsigned int count  = (w0 >> 16) & 0xFFu;
            const unsigned int target = w1 & RDRAM_MASK;
            if (count == 0u || target == 0u ||
                !in_range(c, target, count * 8u)) {
                char d[72];
                sprintf(d, "%u commands at 0x%06X", count, target);
                reject(c, DKR_F3D_REJECT_COUNT, d);
                break;
            }
            if (depth >= MAX_NESTED) {
                reject(c, DKR_F3D_REJECT_DEPTH, "counted list");
                break;
            }
            remaining_stack[depth] = remaining;
            return_stack[depth++] = address;
            remaining = count;
            trace(c, "CountedDisplayList %u commands at 0x%06X", count, target);
            address = target;
            break;
        }

        case OP_FILLRECT:
            cmd_fill_rect(c, w0, w1);
            break;

        case OP_RDPSETOTHERMODE:
            /* --- The mode word, written whole -------------------------------- *
             *
             * `SETOTHERMODE_H` and `_L` are **partial** writes; this one replaces
             * both halves at once. It was being skipped, and the mode therefore
             * stayed frozen on the last partial setting — in practice that of the
             * full-screen fills, that is, cycle mode FILL.
             *
             * The symptom accused nothing: the game's 32,411 combiner
             * configurations were all recorded in FILL cycle, so none could match
             * the table — the cycle mode is part of the key. One would have
             * concluded that the table was incomplete and enriched it with
             * configurations that would have recognised nothing either.
             *
             * The opcode histogram carried the answer from the start:
             * `EF:1798`, one thousand seven hundred and ninety-eight times per
             * race, in the top eight. It was being skipped just like the
             * synchronisations, for want of having looked at what it did.
             *
             * The high half only holds twenty-four bits — that is what the
             * command carries, the rest of the word not existing on the RDP
             * side. */
            c->mode_h = w0 & 0x00FFFFFFu;
            c->mode_l = w1;
            c->state_dirty = 1;
            trace(c, "SetOtherMode whole h=0x%06X l=0x%08X",
                  c->mode_h, c->mode_l);
            break;

        case OP_SETOTHERMODE_H:
            write_othermode(&c->mode_h, w0, w1);
            c->state_dirty = 1;
            break;

        case OP_SETOTHERMODE_L:
            write_othermode(&c->mode_l, w0, w1);
            c->state_dirty = 1;
            break;

        case OP_SETCOMBINE:
            dkr_rdp_decode_combine(w0, w1, &c->combiner);
            c->state_dirty = 1;
            break;

        case OP_SETFILLCOLOR:
            c->state.fill_color_raw = w1;
            c->state.fill_color_argb = colour_from_5551(w1 & 0xFFFFu);
            trace(c, "SetFillColor raw=0x%08X -> 0x%06X",
                  w1, c->state.fill_color_argb);
            break;

        case OP_SETCOLORIMAGE:
            /* The low twelve bits carry the width minus one. This is where the
               rectangles' scale comes from, rather than from an assumption about
               the N64's 320 pixels. */
            c->state.color_image_width = (w0 & 0xFFFu) + 1u;
            trace(c, "SetColorImage width=%u address=0x%06X",
                  c->state.color_image_width, w1 & RDRAM_MASK);
            break;

        case OP_SETTEXIMAGE:
            c->timg_format  = (w0 >> 21) & 0x07u;
            c->timg_size    = (w0 >> 19) & 0x03u;
            c->timg_address = w1 & RDRAM_MASK;
            trace(c, "SetTextureImage %s at 0x%06X",
                  dkr_texture_format_name((dkr_n64_format)c->timg_format,
                                          (dkr_n64_size)c->timg_size),
                  c->timg_address);
            break;

        case OP_SETTILESIZE:
            cmd_set_tile_size(c, w0, w1);
            break;

        case OP_MOVEMEM: {
            const unsigned int index = (w0 >> 16) & 0xFFu;
            const unsigned int size = w0 & 0xFFFFu;
            const unsigned int source = w1 & RDRAM_MASK;
            if (index == MOVEMEM_VIEWPORT && size >= VIEWPORT_BYTES &&
                in_range(c, source, VIEWPORT_BYTES)) {
                cmd_viewport(c, source);
            } else {
                trace(c, "MoveMem index=0x%02X size=%u at 0x%06X",
                      index, size, source);
            }
            break;
        }

        case OP_LOADBLOCK:
            /* `LOADBLOCK` copies the texture from RDRAM into the RDP's texture
               memory. This port reads straight from RDRAM — the shortcut is
               owned and documented at the top of `texture.h` — so the copy has
               no business here. The command stays decoded so that the sequence
               appears in the trace. */
            trace(c, "LoadBlock w0=0x%08X w1=0x%08X", w0, w1);
            break;

        default: {
            char d[48];
            if (opcode_effect_deferred(opcode)) {
                /* Recognised, skipped. Counted separately from `commands`:
                   this figure says **how much of the image we still ignore**,
                   and it is the measurement that would be missed most when the
                   scenery comes out wrong. */
                c->state.deferred++;
                if (c->state.deferred <= MAX_LOGGED_REJECTS) {
                    trace(c, "deferred 0x%02X w0=0x%08X w1=0x%08X", opcode, w0, w1);
                }
                break;
            }
            sprintf(d, "0x%02X at 0x%06X", opcode, address - 8u);
            reject(c, DKR_F3D_REJECT_OPCODE, d);
            /* We stop: after a genuinely unknown opcode the stream is probably
               desynchronised and carrying on would invent commands. The
               detection survives precisely because the known families are
               enumerated rather than everything accepted. */
            running = 0;
            break;
        }
        }

        /* The count is spent: we return, without waiting for an ENDDL. It is a
           counted list's only terminator, and forgetting it made the decoder
           fall out of the bottom of the list into the memory that follows. */
        if (running && remaining == 0u && depth > 0u) {
            address = return_stack[--depth];
            remaining = remaining_stack[depth];
            trace(c, "CountedDisplayList finished - return to 0x%06X", address);
        }
    }
    return executed;
}

void dkr_f3d_init(dkr_f3d_context *ctx, const unsigned char *rdram,
                  unsigned int rdram_size, dkr_render_backend *backend)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    /* A non-zero scale by default: with no texture bound the coordinates are
       not used, but zero would collapse them all onto a point, which would look
       like a transformation defect rather than an absence. */
    ctx->tex_scale_s = 1.0f / 32.0f;
    ctx->tex_scale_t = 1.0f / 32.0f;
    /* Seeded so that the first vertex replaces them. Zero would be a value the
       geometry legitimately holds, and the extremes would then never report a
       range that stays on one side of the origin. */
    ctx->state.proj_x_min = 1000000L;
    ctx->state.proj_y_min = 1000000L;
    ctx->state.proj_x_max = -1000000L;
    ctx->state.proj_y_max = -1000000L;
    ctx->state.s_min = 1.0e30f;
    ctx->state.t_min = 1.0e30f;
    ctx->state.s_max = -1.0e30f;
    ctx->state.oow_min = 1.0e30f;
    ctx->state.oow_max = -1.0e30f;
    ctx->state.t_max = -1.0e30f;
    ctx->rdram      = rdram;
    ctx->rdram_size = rdram_size;
    ctx->backend    = backend;
    dkr_transform_init(&ctx->transform);
}
