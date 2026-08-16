/* E04-S08 — implementation. The why lives in `software.h`. */
#include "software.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXTURES 256

typedef struct {
    dkr_texture_format format;
    int                width, height;
    unsigned          *texels;      /* always converted to ARGB: see below */
    unsigned long long key;
    int                used;
} sw_texture;

typedef struct {
    int               width, height;
    unsigned         *color;
    float            *depth;
    int               scissor_x0, scissor_y0, scissor_x1, scissor_y1;
    dkr_render_state  state;
    sw_texture        textures[MAX_TEXTURES];
    int               open;
} sw_backend;

static sw_backend g_sw;

/* --- Utilities ------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* --- Textures -------------------------------------------------------------- *
 *
 * Every format is converted to 32-bit ARGB on upload. That is an oracle's
 * choice: sampling then has a single path, hence a single opportunity to go
 * wrong. The Glide backend, for its part, will keep the card's native format —
 * that is its business, and the gap between the two is precisely what the
 * comparison must reveal.
 */
static unsigned texel_from_rgba5551(unsigned short p)
{
    /* Replicating the high bits is the right extension: 0x1F must give 0xFF and
       not 0xF8, otherwise white is not white and every colour comparison
       drifts. */
    const unsigned r = (unsigned)((p >> 11) & 0x1F);
    const unsigned g = (unsigned)((p >>  6) & 0x1F);
    const unsigned b = (unsigned)((p >>  1) & 0x1F);
    const unsigned a = (unsigned)(p & 0x1U) ? 0xFFu : 0x00u;
    const unsigned r8 = (r << 3) | (r >> 2);
    const unsigned g8 = (g << 3) | (g >> 2);
    const unsigned b8 = (b << 3) | (b >> 2);
    return (a << 24) | (r8 << 16) | (g8 << 8) | b8;
}

static unsigned sample_texel(const sw_texture *t, int x, int y)
{
    if (!t->texels || t->width <= 0 || t->height <= 0) {
        return 0xFFFFFFFFu;
    }
    x = imax(0, imin(x, t->width  - 1));
    y = imax(0, imin(y, t->height - 1));
    return t->texels[(size_t)y * (size_t)t->width + (size_t)x];
}

static int wrap_coord(int v, int size, dkr_wrap_mode mode)
{
    if (size <= 0) {
        return 0;
    }
    switch (mode) {
    case DKR_WRAP_CLAMP:
        return imax(0, imin(v, size - 1));
    case DKR_WRAP_MIRROR: {
        /* Mirroring folds over a period of 2*size; writing it this way avoids
           the negative-coordinate case, where a C modulo behaves other than one
           expects. */
        int period = size * 2;
        int m = v % period;
        if (m < 0) { m += period; }
        return (m < size) ? m : (period - 1 - m);
    }
    case DKR_WRAP_REPEAT:
    default: {
        int m = v % size;
        return (m < 0) ? m + size : m;
    }
    }
}

static unsigned sample_texture(const sw_texture *t, float s, float tc,
                               const dkr_render_state *st)
{
    if (!t || !t->texels) {
        return 0xFFFFFFFFu;
    }
    if (st->filter == DKR_FILTER_BILINEAR) {
        /* The half-texel is what puts the sample at the centre of the texel.
           Without it the image is offset by half a texel — invisible on a test
           pattern, perfectly visible in an image comparison. */
        const float fx = s * (float)t->width  - 0.5f;
        const float fy = tc * (float)t->height - 0.5f;
        const int   x0 = (int)((fx >= 0.0f) ? fx : fx - 1.0f);
        const int   y0 = (int)((fy >= 0.0f) ? fy : fy - 1.0f);
        const float ax = fx - (float)x0;
        const float ay = fy - (float)y0;
        unsigned c[4];
        unsigned out = 0;
        int ch;
        c[0] = sample_texel(t, wrap_coord(x0,     t->width,  st->wrap_s),
                               wrap_coord(y0,     t->height, st->wrap_t));
        c[1] = sample_texel(t, wrap_coord(x0 + 1, t->width,  st->wrap_s),
                               wrap_coord(y0,     t->height, st->wrap_t));
        c[2] = sample_texel(t, wrap_coord(x0,     t->width,  st->wrap_s),
                               wrap_coord(y0 + 1, t->height, st->wrap_t));
        c[3] = sample_texel(t, wrap_coord(x0 + 1, t->width,  st->wrap_s),
                               wrap_coord(y0 + 1, t->height, st->wrap_t));
        for (ch = 0; ch < 4; ch++) {
            const int shift = ch * 8;
            const float v =
                (float)((c[0] >> shift) & 0xFF) * (1.0f - ax) * (1.0f - ay) +
                (float)((c[1] >> shift) & 0xFF) * ax          * (1.0f - ay) +
                (float)((c[2] >> shift) & 0xFF) * (1.0f - ax) * ay +
                (float)((c[3] >> shift) & 0xFF) * ax          * ay;
            out |= ((unsigned)(v + 0.5f) & 0xFFu) << shift;
        }
        return out;
    }
    {
        const int x = wrap_coord((int)(s  * (float)t->width),  t->width,  st->wrap_s);
        const int y = wrap_coord((int)(tc * (float)t->height), t->height, st->wrap_t);
        return sample_texel(t, x, y);
    }
}

/* --- The combiner ---------------------------------------------------------- *
 *
 * Computed in floating point, with no hardware constraint. That is what gives
 * this backend its value as an oracle: it shows what the image should be, and
 * E05-S03 will measure Glide's gap against that reference.
 */
static void combine(const dkr_render_state *st, unsigned texel,
                    float sr, float sg, float sb, float sa,
                    float *r, float *g, float *b, float *a)
{
    const float tr = (float)((texel >> 16) & 0xFF);
    const float tg = (float)((texel >>  8) & 0xFF);
    const float tb = (float)( texel        & 0xFF);
    const float ta = (float)((texel >> 24) & 0xFF);

    switch (st->combine) {
    case DKR_COMBINE_TEXTURE:
        *r = tr; *g = tg; *b = tb; *a = ta;
        break;
    case DKR_COMBINE_TEXTURE_SHADE:
        *r = tr * sr / 255.0f; *g = tg * sg / 255.0f; *b = tb * sb / 255.0f;
        *a = sa;
        break;
    case DKR_COMBINE_TEXTURE_SHADE_ALPHA:
        *r = tr * sr / 255.0f; *g = tg * sg / 255.0f; *b = tb * sb / 255.0f;
        *a = ta * sa / 255.0f;
        break;
    case DKR_COMBINE_SHADE:
    default:
        *r = sr; *g = sg; *b = sb; *a = sa;
        break;
    }
}

/* --- Writing one pixel ----------------------------------------------------- */

static void put_pixel(int x, int y, float z, float r, float g, float b, float a)
{
    const dkr_render_state *st = &g_sw.state;
    const size_t index = (size_t)y * (size_t)g_sw.width + (size_t)x;
    unsigned dst;
    float dr, dg, db;

    if (x < g_sw.scissor_x0 || x >= g_sw.scissor_x1 ||
        y < g_sw.scissor_y0 || y >= g_sw.scissor_y1) {
        return;
    }
    if (st->alpha_test && a < (float)st->alpha_reference) {
        return;
    }
    if (st->depth != DKR_DEPTH_DISABLED) {
        if (z >= g_sw.depth[index]) {
            return;
        }
        if (st->depth == DKR_DEPTH_TEST_AND_WRITE) {
            g_sw.depth[index] = z;
        }
    }

    dst = g_sw.color[index];
    dr = (float)((dst >> 16) & 0xFF);
    dg = (float)((dst >>  8) & 0xFF);
    db = (float)( dst        & 0xFF);

    switch (st->blend) {
    case DKR_BLEND_ALPHA: {
        const float k = clampf(a / 255.0f, 0.0f, 1.0f);
        r = r * k + dr * (1.0f - k);
        g = g * k + dg * (1.0f - k);
        b = b * k + db * (1.0f - k);
        break;
    }
    case DKR_BLEND_ADDITIVE:
        r += dr; g += dg; b += db;
        break;
    case DKR_BLEND_OPAQUE:
    default:
        break;
    }

    g_sw.color[index] =
        0xFF000000u |
        ((unsigned)clampf(r, 0.0f, 255.0f) << 16) |
        ((unsigned)clampf(g, 0.0f, 255.0f) <<  8) |
        ( unsigned)clampf(b, 0.0f, 255.0f);
}

/* --- Rasterisation --------------------------------------------------------- *
 *
 * Edge functions and barycentric coordinates, a bounding box, one loop per
 * pixel. It is the most obvious form, and that is the point.
 *
 * **Perspective correction** is the part that counts, and it is the one a naive
 * rasteriser gets wrong: interpolating `s` and `t` linearly in screen space
 * makes textures ripple across any surface seen at an angle. So we interpolate
 * `s/w`, `t/w` and `1/w`, and divide per pixel — which the vertices already
 * carry, since the interface has `GrVertex`'s layout where `oow` is `1/w`.
 *
 * This is precisely the kind of artefact one would later try to pin on Glide;
 * the oracle therefore has to be right here.
 */
static float edge(const dkr_render_vertex *a, const dkr_render_vertex *b,
                  float px, float py)
{
    return (px - a->x) * (b->y - a->y) - (py - a->y) * (b->x - a->x);
}

static void raster_triangle(const dkr_render_vertex *v0,
                            const dkr_render_vertex *v1,
                            const dkr_render_vertex *v2)
{
    const dkr_render_state *st = &g_sw.state;
    const sw_texture *tex = NULL;
    float area;
    int x, y, x0, y0, x1, y1;

    if (st->texture != 0 && st->texture <= MAX_TEXTURES &&
        g_sw.textures[st->texture - 1].used) {
        tex = &g_sw.textures[st->texture - 1];
    }

    area = edge(v0, v1, v2->x, v2->y);
    if (area == 0.0f) {
        return;                     /* degenerate triangle */
    }
    /* Culling reads off the sign of the area. Glide does it in hardware; here we
       write it, and the two conventions must agree — the origin is at the top
       left on both sides, so a counter-clockwise triangle on screen has a
       negative area. */
    if (st->cull == DKR_CULL_BACK  && area > 0.0f) { return; }
    if (st->cull == DKR_CULL_FRONT && area < 0.0f) { return; }

    x0 = imax(g_sw.scissor_x0, (int)clampf(v0->x < v1->x ? (v0->x < v2->x ? v0->x : v2->x)
                                                         : (v1->x < v2->x ? v1->x : v2->x),
                                           0.0f, (float)g_sw.width));
    x1 = imin(g_sw.scissor_x1, (int)clampf((v0->x > v1->x ? (v0->x > v2->x ? v0->x : v2->x)
                                                          : (v1->x > v2->x ? v1->x : v2->x)) + 1.0f,
                                           0.0f, (float)g_sw.width));
    y0 = imax(g_sw.scissor_y0, (int)clampf(v0->y < v1->y ? (v0->y < v2->y ? v0->y : v2->y)
                                                         : (v1->y < v2->y ? v1->y : v2->y),
                                           0.0f, (float)g_sw.height));
    y1 = imin(g_sw.scissor_y1, (int)clampf((v0->y > v1->y ? (v0->y > v2->y ? v0->y : v2->y)
                                                          : (v1->y > v2->y ? v1->y : v2->y)) + 1.0f,
                                           0.0f, (float)g_sw.height));

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            /* The pixel's centre, not its corner: that is the fill convention
               which avoids gaps between adjacent triangles. */
            const float px = (float)x + 0.5f;
            const float py = (float)y + 0.5f;
            const float w0 = edge(v1, v2, px, py) / area;
            const float w1 = edge(v2, v0, px, py) / area;
            const float w2 = edge(v0, v1, px, py) / area;
            float oow, w, s, t, sr, sg, sb, sa, z, r, g, b, a;
            unsigned texel = 0xFFFFFFFFu;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }

            /* Perspective correction: interpolate 1/w, then divide. */
            oow = w0 * v0->oow + w1 * v1->oow + w2 * v2->oow;
            w   = (oow != 0.0f) ? (1.0f / oow) : 0.0f;

            /* **Colour is iterated linearly, with no perspective correction.**
             *
             * That is counter-intuitive after the preceding paragraph, and it is
             * nonetheless what is needed: neither Glide 2 nor the RDP corrects
             * colour. `GrVertex.r/g/b/a` are iterated in screen space, and only
             * `s/w`, `t/w` and `1/w` go through the division. An oracle whose
             * target is Glide must iterate like Glide, failing which it accuses
             * the hardware of a gap it authored itself.
             *
             * The first version corrected colour, and the error was invisible:
             * on an ordinary surface the two interpolations differ by a few
             * units. It blew up on the first triangle **clipped at the near
             * plane**, where the created vertex carries an enormous `1/w` that,
             * once weighted, imposes its colour on the whole polygon. The
             * comparison against the card showed a flat magenta where the
             * hardware produced a green gradient — see
             * `docs/research/win95-oracle-vs-card.md`. */
            sr = w0 * v0->r + w1 * v1->r + w2 * v2->r;
            sg = w0 * v0->g + w1 * v1->g + w2 * v2->g;
            sb = w0 * v0->b + w1 * v1->b + w2 * v2->b;
            sa = w0 * v0->a + w1 * v1->a + w2 * v2->a;
            /* **Depth is sorted on `1/w`, not on `z`.**
             *
             * A z buffer is perfectly legitimate, and it was the first choice.
             * Comparison against the card invalidated it, for a reason that
             * cannot be guessed.
             *
             * `dkr_clip_project` clamps `z` to [0,1] — it has to, a vertex
             * created by clipping comes out with a depth of the order of
             * -200000. But clamping **at the vertex** distorts the gradient
             * across the whole primitive: the two ends are no longer to the same
             * scale, and the interpolation lies everywhere between them. The
             * defect stays invisible over a whole surface and only appears where
             * a clipped primitive crosses another — a corner of a few thousand
             * pixels, where the oracle and the card each named a different
             * surface as being in front.
             *
             * `oow` does not have that problem: it is `1/w`, it is affine in
             * screen space, it never needs clamping, and it is exactly what the
             * Voodoo stores in its buffer. The oracle must predict Glide, so it
             * sorts like Glide. Large `1/w` = near, hence the inverted sense of
             * the test.
             *
             * `v->z` stays filled in and available: the RDP does sort in z, and
             * the day we want to confront the port with the original rather than
             * with the hardware, that is the value we will need. */
            z  = -oow;

            if (tex) {
                /* Back to normalised coordinates. The vertices carry Glide's
                   256-texel space — the card imposes the contract, and the
                   oracle adapts, because it has no speed constraint. See
                   `DKR_TEXCOORD_SCALE`. */
                s = (w0 * v0->tmu[0][DKR_TMU_SOW] + w1 * v1->tmu[0][DKR_TMU_SOW] +
                     w2 * v2->tmu[0][DKR_TMU_SOW]) * w * (1.0f / DKR_TEXCOORD_SCALE);
                t = (w0 * v0->tmu[0][DKR_TMU_TOW] + w1 * v1->tmu[0][DKR_TMU_TOW] +
                     w2 * v2->tmu[0][DKR_TMU_TOW]) * w * (1.0f / DKR_TEXCOORD_SCALE);
                texel = sample_texture(tex, s, t, st);
            }

            combine(st, texel, sr, sg, sb, sa, &r, &g, &b, &a);

            if (st->fog_enabled) {
                /* Linear fog over depth. Glide uses a table; the gap between
                   the two is measurable, and that is this backend's reason to
                   exist. */
                const float k = clampf(z, 0.0f, 1.0f);
                const float fr = (float)((st->fog_color >> 16) & 0xFF);
                const float fg = (float)((st->fog_color >>  8) & 0xFF);
                const float fb = (float)( st->fog_color        & 0xFF);
                r = r * (1.0f - k) + fr * k;
                g = g * (1.0f - k) + fg * k;
                b = b * (1.0f - k) + fb * k;
            }
            put_pixel(x, y, z, r, g, b, a);
        }
    }
}

/* --- L'interface ----------------------------------------------------------- */

static int sw_open(void *self, int width, int height)
{
    (void)self;
    if (width <= 0 || height <= 0) {
        return 0;
    }
    g_sw.color = (unsigned *)calloc((size_t)width * (size_t)height, sizeof(unsigned));
    g_sw.depth = (float *)   calloc((size_t)width * (size_t)height, sizeof(float));
    if (!g_sw.color || !g_sw.depth) {
        free(g_sw.color); free(g_sw.depth);
        g_sw.color = NULL; g_sw.depth = NULL;
        return 0;
    }
    g_sw.width = width;
    g_sw.height = height;
    g_sw.scissor_x0 = 0; g_sw.scissor_y0 = 0;
    g_sw.scissor_x1 = width; g_sw.scissor_y1 = height;
    g_sw.open = 1;
    return 1;
}

static void sw_close(void *self)
{
    int i;
    (void)self;
    for (i = 0; i < MAX_TEXTURES; i++) {
        free(g_sw.textures[i].texels);
        g_sw.textures[i].texels = NULL;
        g_sw.textures[i].used = 0;
    }
    free(g_sw.color); free(g_sw.depth);
    memset(&g_sw, 0, sizeof(g_sw));
}

static void sw_begin_frame(void *self, unsigned clear_argb)
{
    size_t i, n;
    (void)self;
    if (!g_sw.open) { return; }
    n = (size_t)g_sw.width * (size_t)g_sw.height;
    for (i = 0; i < n; i++) {
        g_sw.color[i] = 0xFF000000u | (clear_argb & 0x00FFFFFFu);
        /* The buffer starts at the far end. The sorted quantity being `-1/w`,
           "far" is a large positive: `1/w` tends to zero at infinity, so `-1/w`
           tends to zero from below, and every real surface has a negative value.
           Zero would do; we take a margin so that a surface exactly at infinity
           is painted all the same. */
        g_sw.depth[i] = 1.0f;
    }
}

static void sw_present(void *self) { (void)self; }

static void sw_set_state(void *self, const dkr_render_state *state)
{
    (void)self;
    if (state) { g_sw.state = *state; }
}

static void sw_set_scissor(void *self, int x0, int y0, int x1, int y1)
{
    (void)self;
    g_sw.scissor_x0 = imax(0, imin(x0, g_sw.width));
    g_sw.scissor_y0 = imax(0, imin(y0, g_sw.height));
    g_sw.scissor_x1 = imax(0, imin(x1, g_sw.width));
    g_sw.scissor_y1 = imax(0, imin(y1, g_sw.height));
}

static void sw_draw_triangles(void *self, const dkr_render_vertex *v, int count)
{
    int i;
    (void)self;
    if (!g_sw.open || !v) { return; }
    for (i = 0; i < count; i++) {
        raster_triangle(&v[i * 3 + 0], &v[i * 3 + 1], &v[i * 3 + 2]);
    }
}

static void sw_fill_rect(void *self, int x0, int y0, int x1, int y1, unsigned argb)
{
    int x, y;
    (void)self;
    if (!g_sw.open) { return; }
    /* Filled directly, without going through two triangles: the Glide backend
       will have to build them for want of a primitive, this one can fill.
       Imposing the detour on the rasteriser would add a source of divergence
       between the two where there is no reason to have one. */
    x0 = imax(x0, g_sw.scissor_x0); x1 = imin(x1, g_sw.scissor_x1);
    y0 = imax(y0, g_sw.scissor_y0); y1 = imin(y1, g_sw.scissor_y1);
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            g_sw.color[(size_t)y * (size_t)g_sw.width + (size_t)x] =
                0xFF000000u | (argb & 0x00FFFFFFu);
        }
    }
}

static dkr_texture_handle sw_texture_upload(void *self, const dkr_texture_desc *d)
{
    int slot;
    (void)self;
    if (!d || !d->pixels || d->width <= 0 || d->height <= 0) {
        return 0;
    }
    /* Already uploaded? The key is opaque: we compare, we do not interpret. */
    for (slot = 0; slot < MAX_TEXTURES; slot++) {
        if (g_sw.textures[slot].used && g_sw.textures[slot].key == d->key) {
            return (dkr_texture_handle)(slot + 1);
        }
    }
    for (slot = 0; slot < MAX_TEXTURES; slot++) {
        if (!g_sw.textures[slot].used) {
            break;
        }
    }
    if (slot == MAX_TEXTURES) {
        return 0;
    }
    {
        sw_texture *t = &g_sw.textures[slot];
        const size_t count = (size_t)d->width * (size_t)d->height;
        size_t i;
        t->texels = (unsigned *)calloc(count, sizeof(unsigned));
        if (!t->texels) {
            return 0;
        }
        for (i = 0; i < count; i++) {
            switch (d->format) {
            case DKR_TEXFMT_RGBA5551:
                t->texels[i] = texel_from_rgba5551(((const unsigned short *)d->pixels)[i]);
                break;
            case DKR_TEXFMT_INTENSITY8: {
                const unsigned v = ((const unsigned char *)d->pixels)[i];
                t->texels[i] = 0xFF000000u | (v << 16) | (v << 8) | v;
                break;
            }
            case DKR_TEXFMT_RGBA8888:
            default:
                t->texels[i] = ((const unsigned *)d->pixels)[i];
                break;
            }
        }
        t->format = d->format;
        t->width  = d->width;
        t->height = d->height;
        t->key    = d->key;
        t->used   = 1;
        return (dkr_texture_handle)(slot + 1);
    }
}

static void sw_texture_release(void *self, dkr_texture_handle handle)
{
    (void)self;
    if (handle == 0 || handle > MAX_TEXTURES) { return; }
    free(g_sw.textures[handle - 1].texels);
    g_sw.textures[handle - 1].texels = NULL;
    g_sw.textures[handle - 1].used = 0;
}

void dkr_render_backend_software(dkr_render_backend *out)
{
    if (!out) { return; }
    memset(&g_sw, 0, sizeof(g_sw));
    out->name            = "software";
    out->open            = sw_open;
    out->close           = sw_close;
    out->begin_frame     = sw_begin_frame;
    out->present         = sw_present;
    out->set_state       = sw_set_state;
    out->set_scissor     = sw_set_scissor;
    out->draw_triangles  = sw_draw_triangles;
    out->fill_rect       = sw_fill_rect;
    out->texture_upload  = sw_texture_upload;
    out->texture_release = sw_texture_release;
    out->self            = &g_sw;
}

/* --- What makes the oracle observable -------------------------------------- */

const unsigned *dkr_software_framebuffer(int *width, int *height)
{
    if (width)  { *width  = g_sw.width; }
    if (height) { *height = g_sw.height; }
    return g_sw.color;
}

const float *dkr_software_depthbuffer(int *width, int *height)
{
    if (width)  { *width  = g_sw.width; }
    if (height) { *height = g_sw.height; }
    return g_sw.depth;
}

int dkr_software_write_bmp(const char *path)
{
    FILE *f;
    const int w = g_sw.width, h = g_sw.height;
    const int row_padding = (4 - (w * 3) % 4) % 4;
    const unsigned data_size = (unsigned)((w * 3 + row_padding) * h);
    unsigned char header[54];
    int x, y, i;

    if (!g_sw.color || w <= 0 || h <= 0 || !path) {
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    *(unsigned *)&header[2]  = 54u + data_size;
    *(unsigned *)&header[10] = 54u;
    *(unsigned *)&header[14] = 40u;
    *(int *)     &header[18] = w;
    *(int *)     &header[22] = h;      /* positive: rows bottom to top */
    header[26] = 1;                    /* planes */
    header[28] = 24;                   /* bits per pixel */
    *(unsigned *)&header[34] = data_size;
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return 0;
    }
    /* BMP stores rows bottom to top, the buffer top to bottom: we therefore
       walk it backwards. Getting this wrong gives a flipped image, which an
       automatic comparison reports as entirely wrong — a far more expensive
       diagnosis than the bug. */
    for (y = h - 1; y >= 0; y--) {
        for (x = 0; x < w; x++) {
            const unsigned c = g_sw.color[(size_t)y * (size_t)w + (size_t)x];
            unsigned char bgr[3];
            bgr[0] = (unsigned char)( c        & 0xFF);
            bgr[1] = (unsigned char)((c >>  8) & 0xFF);
            bgr[2] = (unsigned char)((c >> 16) & 0xFF);
            if (fwrite(bgr, 1, 3, f) != 3) { fclose(f); return 0; }
        }
        for (i = 0; i < row_padding; i++) {
            if (fputc(0, f) == EOF) { fclose(f); return 0; }
        }
    }
    return fclose(f) == 0;
}
