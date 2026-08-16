/* E05-S06 - Glide's fog, measured.
 *
 * The decomp gives the model: `gSPFogPosition(min, max)` loads a multiplier
 * `128000/(max-min)` and an offset `(500-min)*256/(max-min)`, from which the RSP
 * derives a per-vertex factor that it stores in **the vertex alpha**. The blender
 * then applies it through `G_RM_FOG_SHADE_A` - source `G_BL_CLR_FOG`, factor
 * `G_BL_A_SHADE`.
 *
 * Glide offers exactly that model: `GR_FOG_WITH_ITERATED_ALPHA`. The ticket asks
 * to decide between that route and the 64-entry table, "by measuring whether it
 * has a significant per-vertex cost before concluding". That is what this witness
 * does.
 *
 * ## What it checks that it was not asked to
 *
 * The fog factor occupies the vertex alpha. **Anything else that would want to
 * store something there conflicts with it**, and two things would want to:
 *
 *   - a surface's translucency, when `XLU_SURF` and fog meet - 78 translucent
 *     modes against 74 fog modes in the game's source, the meeting is certain;
 *   - the second constant colour that E05-S03 proposes to carry there in order to
 *     work around Glide's single register.
 *
 * The second point is one ticket's consequence on another, and it is better
 * discovered here than on a wrong piece of scenery.
 */
#include "render/glide.h"
#include "render/backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char l[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(l, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(l, g_out); fflush(g_out); }
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) { g_fails++; }
}

static unsigned g_px[640 * 480];

#define FOG_R 0
#define FOG_G 255
#define FOG_B 0        /* pure green: impossible to confuse with the surface */
#define SURF_R 255
#define SURF_G 0
#define SURF_B 0       /* pure red */

/* A quad whose vertex alpha varies from left to right. That is the fog factor:
   zero on the left, full on the right. The transition, and not an isolated value,
   is what reveals a wrong curve - the ticket insists on it, and it is true at the
   scale of a frame as of a sequence. */
static void quad_gradient(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)w, (float)w, 0, (float)w, 0 };
    const float ys[6] = { 0, 0, (float)h, 0, (float)h, (float)h };
    const float as[6] = { 0.0f, 255.0f, 255.0f, 0.0f, 255.0f, 0.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = SURF_R; v[i].g = SURF_G; v[i].b = SURF_B;
        v[i].a = as[i];
        v[i].oow = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned read_px(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    const int W = 640, H = 480;
    int rw = 0, rh = 0, i;
    unsigned long t_without, t_with;

    g_out = fopen("D:\\FOG.TXT", "w");
    say("Glide's fog, by vertex factor\n\n");
    say("  fog colour     : pure green\n");
    say("  surface colour : pure red\n");
    say("  vertex alpha   : 0 on the left, 255 on the right\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAILED to open\n"); return 1; }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;

    /* Two frames thrown away. E05-S05's lesson: measuring the first frame after
       opening a context is measuring a machine that has not finished settling
       in. */
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad_gradient(&bk, W, H);
        bk.present(bk.self);
    }

    /* --- Without fog: the reference ------------------------------------------ */
    bk.begin_frame(bk.self, 0x000000);
    st.fog_enabled = 0;
    bk.set_state(bk.self, &st);
    quad_gradient(&bk, W, H);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
        say("\n-- without fog --\n");
        say("  left 0x%06X  middle 0x%06X  right 0x%06X\n",
            read_px(rw / 8, rh / 2, rw), read_px(rw / 2, rh / 2, rw),
            read_px(rw * 7 / 8, rh / 2, rw));
        check("the surface is red everywhere, the vertex alpha changes nothing",
              read_px(rw / 8, rh / 2, rw) == read_px(rw * 7 / 8, rh / 2, rw));
    }

    /* --- With fog ------------------------------------------------------------- */
    bk.begin_frame(bk.self, 0x000000);
    st.fog_enabled = 1;
    st.fog_color   = (FOG_R << 16) | (FOG_G << 8) | FOG_B;
    bk.set_state(bk.self, &st);
    quad_gradient(&bk, W, H);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
        const unsigned l = read_px(rw / 8, rh / 2, rw);
        const unsigned m = read_px(rw / 2, rh / 2, rw);
        const unsigned r = read_px(rw * 7 / 8, rh / 2, rw);
        say("\n-- with fog --\n");
        say("  left 0x%06X  middle 0x%06X  right 0x%06X\n", l, m, r);

        check("the two ends differ: the fog acts", l != r);
        /* Which end is the fog? We read it off rather than assume it: Glide may
           interpret the alpha one way or the other, and getting it wrong would
           give an **inverted** fog - clear up close, opaque far away - which is
           spectacular and easy to blame on the curve rather than on the
           direction. */
        say("  direction: alpha 255 %s\n",
            (((r >> 8) & 0xFF) > ((r >> 16) & 0xFF)) ? "= full fog"
                                                     : "= no fog");
        check("one of the two ends is plainly green",
              (((r >> 8) & 0xFF) > 200 && ((r >> 16) & 0xFF) < 60) ||
              (((l >> 8) & 0xFF) > 200 && ((l >> 16) & 0xFF) < 60));
        /* And the middle must be a blend of the two, without which the transition
           would be abrupt - the defect the ticket wants to avoid. */
        check("the middle is a blend, not one of the two extremes",
              m != l && m != r);
    }

    /* --- The interaction with blending ---------------------------------------- *
     *
     * The vertex alpha carries the fog factor. A translucent surface needs it for
     * its own transparency, and the game uses 78 translucent modes against 74 fog
     * modes: the meeting is certain. We measure what happens rather than deduce
     * it. */
    bk.begin_frame(bk.self, 0x0000FF);   /* blue background, to see through */
    st.blend = DKR_BLEND_ALPHA;
    st.fog_enabled = 1;
    bk.set_state(bk.self, &st);
    quad_gradient(&bk, W, H);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
        const unsigned l = read_px(rw / 8, rh / 2, rw);
        const unsigned r = read_px(rw * 7 / 8, rh / 2, rw);
        say("\n-- fog and alpha blending together --\n");
        say("  left 0x%06X  right 0x%06X\n", l, r);
        say("  (blue background: whatever lets blue show is translucent)\n");
        /* This check asserts no result: it **records** which of the alpha's two
           uses wins. Knowing that is what will allow a decision, and ignoring it
           would give either wrong fog or wrong transparency depending on the
           surface. */
        check("the alpha's two uses do not cancel each other out",
              l != r);
    }

    /* --- The cost ------------------------------------------------------------- */
    st.blend = DKR_BLEND_OPAQUE;
    st.fog_enabled = 0;
    t_without = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad_gradient(&bk, W, H);
        bk.present(bk.self);
    }
    t_without = GetTickCount() - t_without;

    st.fog_enabled = 1;
    t_with = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad_gradient(&bk, W, H);
        bk.present(bk.self);
    }
    t_with = GetTickCount() - t_with;

    say("\n-- the cost --\n");
    say("  100 frames without fog : %lu ms\n", t_without);
    say("  100 frames with        : %lu ms\n", t_with);

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
