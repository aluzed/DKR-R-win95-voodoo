/* E04-S05 — implementation. The contract lives in `clip.h`. */
#include "clip.h"

#include <string.h>

/* Interpolates **every** attribute between two vertices.
 *
 * Written once and used for every vertex produced: that is what prevents an
 * attribute being forgotten in a special case. The omission would only show on
 * clipped triangles — hence rarely, hence late. */
static void lerp_vertex(const dkr_clip_vertex *a, const dkr_clip_vertex *b,
                        float k, dkr_clip_vertex *out)
{
    out->x = a->x + (b->x - a->x) * k;
    out->y = a->y + (b->y - a->y) * k;
    out->z = a->z + (b->z - a->z) * k;
    out->w = a->w + (b->w - a->w) * k;
    out->r = a->r + (b->r - a->r) * k;
    out->g = a->g + (b->g - a->g) * k;
    out->b = a->b + (b->b - a->b) * k;
    out->a = a->a + (b->a - a->a) * k;
    out->s = a->s + (b->s - a->s) * k;
    out->t = a->t + (b->t - a->t) * k;
}

/* The signed distance from a vertex to a plane, in homogeneous space.
 *
 * Five planes: the near plane, and the four sides of the guard band. Writing
 * them as linear functions of (x, y, w) lets one loop handle them all — hence
 * only one clipper to read. */
static float plane_distance(const dkr_clip_vertex *v, int plane)
{
    const float g = DKR_CLIP_GUARD;
    switch (plane) {
    case 0:  return v->w - DKR_CLIP_NEAR_EPSILON;   /* near plane */
    case 1:  return v->x + g * v->w;                /* left guard */
    case 2:  return g * v->w - v->x;                /* right guard */
    case 3:  return v->y + g * v->w;                /* top guard */
    default: return g * v->w - v->y;                /* bottom guard */
    }
}

#define CLIP_PLANES 5
/* Five planes can take a triangle up to eight vertices: each adds at most one.
   The bound is reachable, and exceeding it would smash the stack. */
#define CLIP_MAX_VERTICES 8

int dkr_clip_near(const dkr_clip_vertex in[3], dkr_clip_vertex out[6])
{
    dkr_clip_vertex poly[CLIP_MAX_VERTICES];
    dkr_clip_vertex work[CLIP_MAX_VERTICES];
    int n = 3, plane, i, triangles;

    if (!in || !out) {
        return 0;
    }

    /* **Short circuit.** Almost every triangle is entirely inside the band, and
       leaves here without a single edge being computed. That is what makes
       five-plane clipping affordable where full clipping would not be. */
    {
        int all_inside = 1;
        for (plane = 0; plane < CLIP_PLANES && all_inside; plane++) {
            for (i = 0; i < 3; i++) {
                if (plane_distance(&in[i], plane) < 0.0f) {
                    all_inside = 0;
                    break;
                }
            }
        }
        if (all_inside) {
            out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
            return 1;
        }
    }

    poly[0] = in[0]; poly[1] = in[1]; poly[2] = in[2];

    /* Sutherland-Hodgman, one plane after another: for each edge we keep the
       vertex if it is on the right side, and add the intersection if the edge
       crosses. */
    for (plane = 0; plane < CLIP_PLANES; plane++) {
        int m = 0;
        for (i = 0; i < n; i++) {
            const dkr_clip_vertex *cur  = &poly[i];
            const dkr_clip_vertex *next = &poly[(i + 1) % n];
            const float dc = plane_distance(cur,  plane);
            const float dn = plane_distance(next, plane);

            if (dc >= 0.0f && m < CLIP_MAX_VERTICES) {
                work[m++] = *cur;
            }
            if ((dc >= 0.0f) != (dn >= 0.0f) && m < CLIP_MAX_VERTICES) {
                /* The denominator cannot vanish: the two vertices are on
                   opposite sides, so their distances differ. */
                lerp_vertex(cur, next, dc / (dc - dn), &work[m++]);
            }
        }
        n = m;
        if (n < 3) {
            return 0;                 /* rejected entirely */
        }
        for (i = 0; i < n; i++) {
            poly[i] = work[i];
        }
    }

    /* The polygon is retriangulated as a fan. Keeping only the first triangle
       would make the rest of the surface disappear — a hole, on clipped
       triangles alone, hence rare and baffling.

       `out` holds six, that is two triangles: that is this function's contract,
       and a richer polygon is truncated rather than allowed to overflow. The
       case only arises on triangles crossing several planes at once, where the
       lost surface is outside the guard band. */
    triangles = n - 2;
    if (triangles > 2) {
        triangles = 2;
    }
    for (i = 0; i < triangles; i++) {
        out[i * 3 + 0] = poly[0];
        out[i * 3 + 1] = poly[i + 1];
        out[i * 3 + 2] = poly[i + 2];
    }
    return triangles;
}

void dkr_clip_project(const dkr_transform *t, const dkr_clip_vertex *in,
                      dkr_render_vertex *out)
{
    float oow;

    if (!t || !in || !out) {
        return;
    }
    /* Clipping guarantees `w > epsilon`: the division is safe here, and that is
       the whole point of having clipped first. */
    oow = 1.0f / in->w;

    memset(out, 0, sizeof(*out));
    out->x = in->x * oow * t->viewport_scale_x + t->viewport_trans_x;
    out->y = in->y * oow * t->viewport_scale_y + t->viewport_trans_y;

    /* **Depth is clamped to [0,1], and that is not a tweak.**
     *
     * The depth buffer is defined over that interval: a value outside it means
     * nothing, and it wins the test everywhere. A vertex created by clipping
     * comes out with `w` equal to the near-plane margin, hence an enormous depth
     * — measured: -250000 for a margin of 0.0001 — which passes in front of the
     * whole scene.
     *
     * The symptom is spectacular and misleading: isolated pixels of the clipped
     * triangle punch through a surface that should hide it, in a stippled
     * pattern that suggests a rasterisation defect rather than a depth one. It
     * took projecting a vertex by hand to see it.
     *
     * Real hardware clamps the same way. */
    {
        const float z = in->z * oow;
        out->z = (z < 0.0f) ? 0.0f : (z > 1.0f ? 1.0f : z);
    }
    out->ooz = out->z;
    out->oow = oow;
    out->r = in->r;
    out->g = in->g;
    out->b = in->b;
    out->a = in->a;
    /* Texture coordinates are divided here, not earlier: the rasteriser and
       Glide expect `s/w` and `t/w`.
       The scale of 256 is Glide's convention, measured on the card — see
       `DKR_TEXCOORD_SCALE` in `backend.h`. It is applied here, once per vertex,
       rather than by the backend once per triangle. */
    out->tmu[0][DKR_TMU_SOW] = in->s * DKR_TEXCOORD_SCALE * oow;
    out->tmu[0][DKR_TMU_TOW] = in->t * DKR_TEXCOORD_SCALE * oow;
    out->tmu[0][DKR_TMU_OOW] = oow;
}

/* --- Back faces ------------------------------------------------------------- */

dkr_cull_mode dkr_cull_mode_for_viewport(float viewport_scale_x, int cull_enabled)
{
    if (!cull_enabled) {
        return DKR_CULL_NONE;
    }
    /* The microcode's convention, taken from `f3ddkr_rt64.cpp`: the direction
       depends on the **sign of the x scale**. A mirrored viewport flips the
       apparent winding of triangles, and culling the wrong side would empty the
       screen — a spectacular defect and an easy one to misdiagnose. */
    return (viewport_scale_x > 0.0f) ? DKR_CULL_BACK : DKR_CULL_FRONT;
}

int dkr_cull_accept(const dkr_render_vertex v[3], dkr_cull_mode mode)
{
    float area;
    if (!v || mode == DKR_CULL_NONE) {
        return 1;
    }
    /* The signed area in screen space, origin at the top left. The E04-S08
       rasteriser uses the same convention: the two must agree, otherwise the
       oracle and Glide would not cull the same triangles. */
    area = (v[1].x - v[0].x) * (v[2].y - v[0].y) -
           (v[2].x - v[0].x) * (v[1].y - v[0].y);
    if (area == 0.0f) {
        return 0;                     /* degenerate: nothing to draw */
    }
    return (mode == DKR_CULL_BACK) ? (area < 0.0f) : (area > 0.0f);
}

/* --- Rejection -------------------------------------------------------------- */

int dkr_clip_reject_offscreen(const dkr_render_vertex v[3],
                              int width, int height, float margin)
{
    int i;
    int left = 0, right = 0, above = 0, below = 0;

    if (!v) {
        return 1;
    }
    /* Rejected only if **all three** vertices are on the same side. A triangle
       whose vertices are scattered on either side may well cover the screen, and
       rejecting it would be a far worse error than letting a useless triangle
       through. */
    for (i = 0; i < 3; i++) {
        if (v[i].x < -margin)                    { left++; }
        if (v[i].x > (float)width + margin)      { right++; }
        if (v[i].y < -margin)                    { above++; }
        if (v[i].y > (float)height + margin)     { below++; }
    }
    return left == 3 || right == 3 || above == 3 || below == 3;
}

/* --- Scissor window --------------------------------------------------------- */

int dkr_scissor_for_player(int players, int player, int width, int height,
                           dkr_scissor *out)
{
    if (!out || player < 0 || player >= players || width <= 0 || height <= 0) {
        return 0;
    }
    switch (players) {
    case 1:
        out->x0 = 0; out->y0 = 0; out->x1 = width; out->y1 = height;
        return 1;
    case 2:
        /* Two players: horizontal split, one above the other. */
        out->x0 = 0; out->x1 = width;
        out->y0 = player * (height / 2);
        out->y1 = out->y0 + height / 2;
        return 1;
    case 3:
    case 4:
        /* Three and four players share the same grid of four quadrants; with
           three, the fourth stays empty. Handling them together avoids two
           computations that would drift apart. */
        out->x0 = (player % 2) * (width / 2);
        out->y0 = (player / 2) * (height / 2);
        out->x1 = out->x0 + width / 2;
        out->y1 = out->y0 + height / 2;
        return 1;
    default:
        return 0;
    }
}
