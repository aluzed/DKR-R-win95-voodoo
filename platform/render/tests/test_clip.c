/* E04-S05 — the clipping test.
 *
 * Clipping is a classic source of subtle errors: a badly clipped triangle
 * produces a shard of geometry crossing the screen, very visible and hard to
 * reproduce because it depends on one precise camera angle.
 *
 * The checks below therefore deliberately place the camera **inside** the
 * geometry rather than in front of it, and verify each interpolated attribute
 * separately — forgetting a single one would only show on clipped triangles,
 * hence rarely, hence late.
 */
#include "render/clip.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "FAIL ", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "FAIL ", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    const int ok = (got - want < tol) && (want - got < tol);
    printf("  %s %-40s expected %.4f, got %.4f\n",
           ok ? "ok   " : "FAIL ", what, want, got);
    if (g_out) {
        fprintf(g_out, "  %s %-40s expected %.4f, got %.4f\n",
                ok ? "ok   " : "FAIL ", what, want, got);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static dkr_clip_vertex vertex(float x, float y, float z, float w,
                              float r, float s, float t)
{
    dkr_clip_vertex v;
    memset(&v, 0, sizeof(v));
    v.x = x; v.y = y; v.z = z; v.w = w;
    v.r = r; v.g = 0.0f; v.b = 0.0f; v.a = 255.0f;
    v.s = s; v.t = t;
    return v;
}

int main(void)
{
    dkr_clip_vertex in[3], out[6];
    dkr_transform   t;

    g_out = fopen("D:\\CLIP.TXT", "w");

    /* --- Entirely in front: nothing to do ---------------------------------- */
    in[0] = vertex(0.0f, 0.0f, 1.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    in[1] = vertex(1.0f, 0.0f, 1.0f, 1.0f, 255.0f, 1.0f, 0.0f);
    in[2] = vertex(0.0f, 1.0f, 1.0f, 1.0f, 255.0f, 0.0f, 1.0f);
    check("a triangle entirely in front comes out intact",
          dkr_clip_near(in, out) == 1 &&
          out[0].w == 1.0f && out[1].w == 1.0f && out[2].w == 1.0f);

    /* --- Entirely behind: nothing comes out -------------------------------- */
    in[0].w = in[1].w = in[2].w = -1.0f;
    check("a triangle entirely behind disappears",
          dkr_clip_near(in, out) == 0);

    /* --- One vertex behind: **two** triangles ------------------------------- *
     *
     * The remaining polygon is a quadrilateral. Forgetting that would make half
     * the surface disappear — a hole, on the triangles that cross the plane
     * alone, hence a rare and baffling defect. */
    in[0] = vertex(0.0f, 0.0f,  1.0f, -1.0f, 255.0f, 0.0f, 0.0f);  /* behind */
    in[1] = vertex(1.0f, 0.0f,  1.0f,  1.0f, 255.0f, 1.0f, 0.0f);
    in[2] = vertex(0.0f, 1.0f,  1.0f,  1.0f, 255.0f, 0.0f, 1.0f);
    check("one vertex behind produces two triangles",
          dkr_clip_near(in, out) == 2);

    /* --- Two vertices behind: a single triangle ----------------------------- */
    in[1].w = -1.0f;
    check("two vertices behind produce one triangle",
          dkr_clip_near(in, out) == 1);

    /* --- Every attribute is interpolated ------------------------------------ *
     *
     * This is the check that counts. A forgotten attribute only shows on clipped
     * triangles; verifying each one separately is the only way not to let the
     * omission through.
     *
     * The plane chosen is the **right-hand guard band**, not the near plane:
     * since the clipper also bounds the sides, a vertex created at the near
     * plane has a tiny `w` and gets re-clipped by the guard — which is the
     * intended effect, but makes the interpolation awkward to check by hand.
     * Here every `w` is 1, so the guard simply falls at `x = 4`. */
    {
        int n, i, found = 0;
        in[0] = vertex(0.0f, 0.0f, 0.0f, 1.0f, 100.0f, 0.0f, 0.0f);
        in[1] = vertex(8.0f, 0.0f, 0.0f, 1.0f, 200.0f, 1.0f, 0.5f);  /* past the guard */
        in[2] = vertex(0.0f, 2.0f, 0.0f, 1.0f, 100.0f, 0.0f, 1.0f);
        n = dkr_clip_near(in, out);
        check("the triangle overrunning the guard is clipped", n >= 1);
        for (i = 0; i < n * 3; i++) {
            /* The vertex created on edge 0-1: x is 4, that is halfway, so every
               attribute must be the average of the two. */
            if (out[i].x > 3.9f && out[i].x < 4.1f && out[i].y < 0.5f) {
                found = 1;
                check_near("the colour is interpolated there", out[i].r, 150.0, 1.0);
                check_near("the s coordinate too",             out[i].s,   0.5, 0.01);
                check_near("and the t coordinate",             out[i].t,  0.25, 0.01);
            }
        }
        check("the created vertex was indeed found", found);
    }

    /* --- The invariant the guard band brings --------------------------------- *
     *
     * **No projected coordinate overruns the band.** That is the property the
     * clipper was generalised for: without it, a vertex created at the near
     * plane projected to sixteen million pixels, and the rasteriser's edge
     * functions lost all precision — to the point where the host and the target
     * no longer rendered the same pixels. */
    {
        dkr_transform vp;
        int n, i;
        dkr_transform_init(&vp);
        dkr_transform_set_viewport(&vp, 160.0f, -120.0f, 160.0f, 120.0f);

        /* The problem case: a vertex behind the camera. */
        in[0] = vertex(-30.0f, 0.0f, -25.0f, -50.0f, 255.0f, 0.0f, 0.0f);
        in[1] = vertex( 90.0f, 0.0f, 150.0f, 300.0f, 255.0f, 1.0f, 0.0f);
        in[2] = vertex( 60.0f, -60.0f, 100.0f, 200.0f, 255.0f, 0.0f, 1.0f);
        n = dkr_clip_near(in, out);
        check("the crossing triangle produces something", n >= 1);
        for (i = 0; i < n * 3; i++) {
            dkr_render_vertex rv;
            dkr_clip_project(&vp, &out[i], &rv);
            /* The guard is 4 half-screens; with a scale of 160 and a
               translation of 160, that bounds x to [-480, 800]. We check
               loosely: what matters is that there are no more millions. */
            check("the projected coordinates stay bounded",
                  rv.x > -2000.0f && rv.x < 3000.0f &&
                  rv.y > -2000.0f && rv.y < 3000.0f);
        }
    }

    /* --- The projection ----------------------------------------------------- */
    dkr_transform_init(&t);
    dkr_transform_set_viewport(&t, 320.0f, -240.0f, 320.0f, 240.0f);
    {
        dkr_clip_vertex  cv = vertex(50.0f, 25.0f, 10.0f, 100.0f, 255.0f, 2.0f, 4.0f);
        dkr_render_vertex rv;
        dkr_clip_project(&t, &cv, &rv);
        check_near("screen x after the divide", rv.x, 50.0 / 100.0 * 320.0 + 320.0, 0.01);
        check_near("1/w",                       rv.oow, 0.01, 0.00001);
        /* The rasteriser and Glide expect s/w, not s — and in the 256-texel
           space the card imposes. The scale is applied here, once per vertex,
           rather than by the backend once per triangle: see `DKR_TEXCOORD_SCALE`
           in `backend.h`, where the measurement is reported. This test checked
           the divide without the scale, and it is the contract change that made
           it fail, not a regression. */
        check_near("s is divided by w, in the 256-texel space",
                   rv.tmu[0][DKR_TMU_SOW], 2.0 / 100.0 * 256.0, 0.001);
        check_near("t as well",
                   rv.tmu[0][DKR_TMU_TOW], 4.0 / 100.0 * 256.0, 0.001);
    }

    /* --- Back faces ---------------------------------------------------------- *
     *
     * The convention comes from the microcode: the direction depends on the
     * **sign of the x scale**. A mirrored viewport flips the apparent winding,
     * and culling the wrong side would empty the screen. */
    check("positive scale: we cull the back",
          dkr_cull_mode_for_viewport( 320.0f, 1) == DKR_CULL_BACK);
    check("negative scale: we cull the front",
          dkr_cull_mode_for_viewport(-320.0f, 1) == DKR_CULL_FRONT);
    check("culling disabled: we keep everything",
          dkr_cull_mode_for_viewport( 320.0f, 0) == DKR_CULL_NONE);
    {
        dkr_render_vertex tri[3];
        memset(tri, 0, sizeof(tri));
        /* Clockwise on screen, origin at the top left: positive area. */
        tri[0].x =  0.0f; tri[0].y =  0.0f;
        tri[1].x = 10.0f; tri[1].y =  0.0f;
        tri[2].x =  0.0f; tri[2].y = 10.0f;
        check("one winding is accepted and the other rejected",
              dkr_cull_accept(tri, DKR_CULL_FRONT) !=
              dkr_cull_accept(tri, DKR_CULL_BACK));
        check("without culling, everything passes", dkr_cull_accept(tri, DKR_CULL_NONE));
        /* Degenerate: no surface, hence nothing to draw whatever the winding. */
        tri[2] = tri[1];
        check("a degenerate triangle is rejected",
              !dkr_cull_accept(tri, DKR_CULL_BACK) &&
              !dkr_cull_accept(tri, DKR_CULL_FRONT));
    }

    /* --- Off-screen rejection ------------------------------------------------ */
    {
        dkr_render_vertex tri[3];
        memset(tri, 0, sizeof(tri));
        tri[0].x = -5000.0f; tri[1].x = -6000.0f; tri[2].x = -7000.0f;
        check("a triangle entirely to the left is rejected",
              dkr_clip_reject_offscreen(tri, 640, 480, DKR_CLIP_DEFAULT_MARGIN));
        /* **Only if all three are on the same side.** A scattered triangle may
           well cover the screen, and rejecting it would be far worse than
           letting a useless triangle through. */
        tri[2].x = 320.0f;
        check("but not if one of them is still on screen",
              !dkr_clip_reject_offscreen(tri, 640, 480, DKR_CLIP_DEFAULT_MARGIN));
    }

    /* --- Split screen -------------------------------------------------------- */
    {
        dkr_scissor s;
        check("one player takes the whole screen",
              dkr_scissor_for_player(1, 0, 640, 480, &s) &&
              s.x0 == 0 && s.y0 == 0 && s.x1 == 640 && s.y1 == 480);
        check("two players split it vertically",
              dkr_scissor_for_player(2, 0, 640, 480, &s) && s.y1 == 240 &&
              dkr_scissor_for_player(2, 1, 640, 480, &s) && s.y0 == 240);
        check("four players take four quadrants",
              dkr_scissor_for_player(4, 0, 640, 480, &s) && s.x0 == 0   && s.y0 == 0   &&
              dkr_scissor_for_player(4, 1, 640, 480, &s) && s.x0 == 320 && s.y0 == 0   &&
              dkr_scissor_for_player(4, 2, 640, 480, &s) && s.x0 == 0   && s.y0 == 240 &&
              dkr_scissor_for_player(4, 3, 640, 480, &s) && s.x0 == 320 && s.y0 == 240);
        check("with three players, the grid is the same",
              dkr_scissor_for_player(3, 2, 640, 480, &s) && s.x0 == 0 && s.y0 == 240);
        check("an absurd request is refused",
              !dkr_scissor_for_player(4, 9, 640, 480, &s) &&
              !dkr_scissor_for_player(0, 0, 640, 480, &s));
        /* The quadrants must not overlap: a pixel drawn twice would belong to
           two players. */
        {
            dkr_scissor a, b;
            dkr_scissor_for_player(4, 0, 640, 480, &a);
            dkr_scissor_for_player(4, 1, 640, 480, &b);
            check("and two neighbouring quadrants do not overlap", a.x1 <= b.x0);
        }
    }

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
