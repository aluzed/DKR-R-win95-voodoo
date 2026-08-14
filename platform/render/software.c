/* E04-S08 — mise en œuvre. Le pourquoi est dans `software.h`. */
#include "software.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXTURES 256

typedef struct {
    dkr_texture_format format;
    int                width, height;
    unsigned          *texels;      /* toujours converti en ARGB : voir plus bas */
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

/* --- Utilitaires ----------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* --- Textures -------------------------------------------------------------- *
 *
 * Tous les formats sont convertis en ARGB 32 bits au chargement. C'est un choix
 * d'oracle : l'échantillonnage n'a alors qu'un seul chemin, donc une seule
 * occasion de se tromper. Le backend Glide, lui, gardera le format natif de la
 * carte — c'est son affaire, et l'écart entre les deux est précisément ce que la
 * comparaison doit révéler.
 */
static unsigned texel_from_rgba5551(unsigned short p)
{
    /* La réplication des bits de poids fort est la bonne extension : 0x1F doit
       donner 0xFF et non 0xF8, sans quoi le blanc n'est pas blanc et toute
       comparaison de couleur dérive. */
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
        /* Le miroir se replie sur une période de 2*size ; l'écrire ainsi évite
           le cas des coordonnées négatives, où un modulo de C se comporte
           autrement qu'on ne l'attend. */
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
        /* Le demi-texel est ce qui place l'échantillon au centre du texel. Sans
           lui l'image est décalée d'un demi-texel — invisible sur une mire,
           parfaitement visible sur une comparaison d'images. */
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

/* --- Le combineur ---------------------------------------------------------- *
 *
 * Calculé en flottants, sans contrainte de matériel. C'est ce qui donne à ce
 * backend sa valeur d'oracle : il montre ce que l'image devrait être, et E05-S03
 * mesurera l'écart de Glide à cette référence.
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

/* --- Écriture d'un pixel --------------------------------------------------- */

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

/* --- Rastérisation --------------------------------------------------------- *
 *
 * Fonctions de bord et coordonnées barycentriques, une boîte englobante, une
 * boucle par pixel. C'est la forme la plus évidente, et c'est le but.
 *
 * La **correction perspective** est le point qui compte, et c'est celui qu'un
 * rastériseur naïf rate : interpoler `s` et `t` linéairement en espace écran
 * fait onduler les textures sur toute surface vue en oblique. On interpole donc
 * `s/w`, `t/w` et `1/w`, et on divise par pixel — ce que les sommets portent
 * déjà, puisque l'interface a la disposition de `GrVertex` où `oow` vaut `1/w`.
 *
 * C'est précisément le genre d'artefact qu'on chercherait plus tard à imputer à
 * Glide ; il faut donc que l'oracle soit juste ici.
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
        return;                     /* triangle dégénéré */
    }
    /* La culling se lit sur le signe de l'aire. Glide la fait dans le matériel ;
       ici on l'écrit, et il faut que les deux conventions coïncident — l'origine
       est en haut à gauche des deux côtés, donc un triangle antihoraire à
       l'écran a une aire négative. */
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
            /* Le centre du pixel, et non son coin : c'est la convention de
               remplissage qui évite les trous entre triangles adjacents. */
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

            /* Correction perspective : interpoler 1/w, puis diviser. */
            oow = w0 * v0->oow + w1 * v1->oow + w2 * v2->oow;
            w   = (oow != 0.0f) ? (1.0f / oow) : 0.0f;

            sr = (w0 * v0->r * v0->oow + w1 * v1->r * v1->oow + w2 * v2->r * v2->oow) * w;
            sg = (w0 * v0->g * v0->oow + w1 * v1->g * v1->oow + w2 * v2->g * v2->oow) * w;
            sb = (w0 * v0->b * v0->oow + w1 * v1->b * v1->oow + w2 * v2->b * v2->oow) * w;
            sa = (w0 * v0->a * v0->oow + w1 * v1->a * v1->oow + w2 * v2->a * v2->oow) * w;
            z  =  w0 * v0->z + w1 * v1->z + w2 * v2->z;

            if (tex) {
                s = (w0 * v0->tmu[0][DKR_TMU_SOW] + w1 * v1->tmu[0][DKR_TMU_SOW] +
                     w2 * v2->tmu[0][DKR_TMU_SOW]) * w;
                t = (w0 * v0->tmu[0][DKR_TMU_TOW] + w1 * v1->tmu[0][DKR_TMU_TOW] +
                     w2 * v2->tmu[0][DKR_TMU_TOW]) * w;
                texel = sample_texture(tex, s, t, st);
            }

            combine(st, texel, sr, sg, sb, sa, &r, &g, &b, &a);

            if (st->fog_enabled) {
                /* Un brouillard linéaire sur la profondeur. Glide emploie une
                   table ; l'écart entre les deux est mesurable, et c'est la
                   raison d'être de ce backend. */
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
        /* La profondeur part au plus loin. `1.0` et non `FLT_MAX` : les valeurs
           interpolées sont dans [0,1], et une borne hors échelle masquerait une
           erreur d'échelle au lieu de la révéler. */
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
    /* Rempli directement, sans passer par deux triangles : le backend Glide
       devra les fabriquer faute de primitive, celui-ci sait remplir. Imposer le
       détour au rastériseur ajouterait une source d'écart entre les deux là où
       il n'y a aucune raison d'en avoir. */
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
    /* Déjà chargée ? La clé est opaque : on compare, on n'interprète pas. */
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

/* --- Ce qui rend l'oracle observable --------------------------------------- */

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
    *(int *)     &header[22] = h;      /* positif : lignes du bas vers le haut */
    header[26] = 1;                    /* plans */
    header[28] = 24;                   /* bits par pixel */
    *(unsigned *)&header[34] = data_size;
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return 0;
    }
    /* BMP range les lignes du bas vers le haut, le tampon du haut vers le bas :
       on parcourt donc à l'envers. Se tromper ici donne une image retournée,
       qu'une comparaison automatique signale comme entièrement fausse — un
       diagnostic bien plus coûteux que le bogue. */
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
