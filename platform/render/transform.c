/* E04-S03 — implementation. The contract lives in `transform.h`. */
#include "transform.h"
#include "clip.h"

#include <string.h>

/* --- Conversion 16.16 ------------------------------------------------------- */

static short read_s16_be(const unsigned char *p)
{
    return (short)(((unsigned)p[0] << 8) | p[1]);
}

static unsigned short read_u16_be(const unsigned char *p)
{
    return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

int dkr_matrix_from_fixed(const unsigned char *data, dkr_matrix *out)
{
    int i, j;
    if (!data || !out) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const int k = i * 4 + j;
            /* The two halves sit 32 bytes apart, and the element recomposes
               into a **signed** 32-bit integer:
               `(integer << 16) | fraction`, divided by 65536.
             *
               Writing `integer + fraction / 65536.0` gives the same result —
               the two's-complement decomposition is exact — but **treating the
               fraction as signed would not**. That is the mistake the test is
               looking for. */
            const short          hi = read_s16_be(data + k * 2);
            const unsigned short lo = read_u16_be(data + 32 + k * 2);
            const int combined = (int)(((unsigned int)(unsigned short)hi << 16) |
                                       (unsigned int)lo);
            out->m[i][j] = (float)combined / 65536.0f;
        }
    }
    return 1;
}

/* --- The stack -------------------------------------------------------------- */

static void identity(dkr_matrix *m)
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m->m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}

void dkr_transform_init(dkr_transform *t)
{
    int i;
    if (!t) { return; }
    memset(t, 0, sizeof(*t));
    for (i = 0; i < DKR_MATRIX_SLOTS; i++) {
        identity(&t->slot[i]);
    }
    identity(&t->projection);
    identity(&t->mvp);
    t->mvp_valid = 1;
    /* A plausible default window: 640x480, origin at the centre. The game will
       replace it; keeping it non-zero stops an oversight from collapsing every
       vertex onto the same point, which looks just like a matrix bug. */
    t->viewport_scale_x = 320.0f;
    t->viewport_scale_y = -240.0f;
    t->viewport_trans_x = 320.0f;
    t->viewport_trans_y = 240.0f;
}

void dkr_transform_set_matrix(dkr_transform *t, int slot, const dkr_matrix *m)
{
    if (!t || !m || slot < 0 || slot >= DKR_MATRIX_SLOTS) { return; }
    t->slot[slot] = *m;
    t->mvp_valid = 0;
}

void dkr_transform_select(dkr_transform *t, int slot)
{
    if (!t) { return; }
    if (slot < 0) { slot = 0; }
    if (slot >= DKR_MATRIX_SLOTS) { slot = DKR_MATRIX_SLOTS - 1; }
    if (t->selected != slot) {
        t->selected = slot;
        t->mvp_valid = 0;
    }
}

void dkr_transform_set_projection(dkr_transform *t, const dkr_matrix *m)
{
    if (!t || !m) { return; }
    t->projection = *m;
    t->mvp_valid = 0;
}

void dkr_transform_set_viewport(dkr_transform *t, float sx, float sy,
                                float tx, float ty)
{
    if (!t) { return; }
    t->viewport_scale_x = sx;
    t->viewport_scale_y = sy;
    t->viewport_trans_x = tx;
    t->viewport_trans_y = ty;
}

static void multiply(const dkr_matrix *a, const dkr_matrix *b, dkr_matrix *out)
{
    int i, j, k;
    dkr_matrix r;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) {
                s += a->m[i][k] * b->m[k][j];
            }
            r.m[i][j] = s;
        }
    }
    *out = r;
}

const dkr_matrix *dkr_transform_mvp(dkr_transform *t)
{
    if (!t) { return NULL; }
    if (!t->mvp_valid) {
        /* The product is only recomputed when something changes. On a Pentium
           II, sixty-four multiplications per vertex rather than per matrix
           would be the kind of expense that decides a port. */
        multiply(&t->slot[t->selected], &t->projection, &t->mvp);
        t->mvp_valid = 1;
    }
    return &t->mvp;
}

/* --- The vertex ------------------------------------------------------------- */

int dkr_transform_vertex(dkr_transform *t, const dkr_source_vertex *in,
                         dkr_render_vertex *out)
{
    const dkr_matrix *m;
    float x, y, z, w, oow;

    if (!t || !in || !out) {
        return 0;
    }
    m = dkr_transform_mvp(t);

    x = (float)in->x;
    y = (float)in->y;
    z = (float)in->z;

    {
        const float cx = x * m->m[0][0] + y * m->m[1][0] + z * m->m[2][0] + m->m[3][0];
        const float cy = x * m->m[0][1] + y * m->m[1][1] + z * m->m[2][1] + m->m[3][1];
        const float cz = x * m->m[0][2] + y * m->m[1][2] + z * m->m[2][2] + m->m[3][2];
        const float cw = x * m->m[0][3] + y * m->m[1][3] + z * m->m[2][3] + m->m[3][3];
        x = cx; y = cy; z = cz; w = cw;
    }

    /* Behind the projection plane: do not divide. Clipping is E04-S05; here we
       simply refuse to produce a vertex whose coordinates would mean nothing —
       and a stray vertex shooting across the screen is far more visible than a
       missing one. */
    if (w <= 0.0f) {
        return 0;
    }
    oow = 1.0f / w;

    out->x = x * oow * t->viewport_scale_x + t->viewport_trans_x;
    out->y = y * oow * t->viewport_scale_y + t->viewport_trans_y;
    out->z = z * oow;

    /* `oow` is what Glide calls `1/w`, and `ooz` the depth-buffer value.
       Writing them here rather than further down avoids the copy that E04-S01
       set out to remove in the first place. */
    out->oow = oow;
    out->ooz = out->z;

    out->r = (float)in->r;
    out->g = (float)in->g;
    out->b = (float)in->b;
    out->a = (float)in->a;

    /* Texture coordinates do not come from the vertex — they arrive per corner
       when the triangle is emitted. We leave them at zero rather than invent
       them: a plausible value would hide a triangle that forgot to set them. */
    memset(out->tmu, 0, sizeof(out->tmu));
    out->tmu[0][DKR_TMU_OOW] = oow;

    return 1;
}

void dkr_transform_to_clip(dkr_transform *t, const dkr_source_vertex *in,
                           float s, float tc, struct dkr_clip_vertex_ *out)
{
    const dkr_matrix *m;
    float x, y, z;

    if (!t || !in || !out) {
        return;
    }
    m = dkr_transform_mvp(t);
    x = (float)in->x;
    y = (float)in->y;
    z = (float)in->z;

    out->x = x * m->m[0][0] + y * m->m[1][0] + z * m->m[2][0] + m->m[3][0];
    out->y = x * m->m[0][1] + y * m->m[1][1] + z * m->m[2][1] + m->m[3][1];
    out->z = x * m->m[0][2] + y * m->m[1][2] + z * m->m[2][2] + m->m[3][2];
    out->w = x * m->m[0][3] + y * m->m[1][3] + z * m->m[2][3] + m->m[3][3];

    out->r = (float)in->r;
    out->g = (float)in->g;
    out->b = (float)in->b;
    out->a = (float)in->a;
    out->s = s;
    out->t = tc;
}
