/* E05-S05 - deciding between the Z buffer and the W buffer, by measurement.
 *
 * The ticket names the risk: "16-bit depth fighting does not show on a test
 * scene: it shows far away, on a long track, in motion. It must be sought
 * actively, under the conditions where it appears, rather than waited for."
 *
 * This witness seeks it actively. The scene is not a test scene in the usual
 * sense - it is built to be **the worst case**: two nearly coplanar surfaces,
 * very far away, two parts per thousand apart. That is exactly the configuration
 * of a race track seen from afar, and that is where sixteen bits of depth give
 * way.
 *
 * ## What makes the comparison honest
 *
 * The two buffers do not read the same vertex field: W mode consumes `oow`, Z
 * mode consumes `ooz` over [0, 65535]. Filling both from the same distance, with
 * a realistic perspective projection, is the only way to compare the buffers
 * rather than two different conventions.
 *
 * The near and far distances are chosen to resemble a track: 10 units in front,
 * 20000 at the back.
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

#define NEAR_PLANE  10.0f
#define FAR_PLANE   20000.0f

static unsigned g_px[640 * 480];

/* The normalised depth of a distance, by a classic perspective projection. This
   is what E04-S03 produces, and the agreement between the two is a criterion in
   its own right: a disagreement gives a globally wrong sort. */
static float depth_ndc(float w)
{
    return (FAR_PLANE / (FAR_PLANE - NEAR_PLANE)) * (1.0f - NEAR_PLANE / w);
}

static void quad(dkr_render_backend *bk, float w, float r, float g, float b,
                 int width, int height)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)width, (float)width,
                          0, (float)width, 0 };
    const float ys[6] = { 0, 0, (float)height, 0, (float)height, (float)height };
    const float z = depth_ndc(w);
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = 255.0f;
        v[i].oow = 1.0f / w;
        v[i].z   = z;
        /* Z mode reads `ooz` over [0, 65535], W mode reads `oow`. Filling both
           from the same distance is what makes the comparison honest. */
        v[i].ooz = z * 65535.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

/* Counts the pixels where the far surface won when it should not have - depth
   fighting, measured rather than looked at. */
static int lost(int got, unsigned expected)
{
    int i, n = 0;
    for (i = 0; i < got; i++) {
        if ((g_px[i] & 0x00FFFFFFu) != expected) { n++; }
    }
    return n;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    const int W = 640, H = 480;
    int rw = 0, rh = 0, got;
    int fight_w = -1, fight_z = -1;

    g_out = fopen("D:\\DEPTH.TXT", "w");
    say("Z buffer or W buffer: the measurement, on the worst case\n\n");
    say("  near plane %.0f, far plane %.0f\n",
        (double)NEAR_PLANE, (double)FAR_PLANE);

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAILED to open\n"); return 1; }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_TEST_AND_WRITE;
    st.cull    = DKR_CULL_NONE;

    /* --- The worst case: two nearly coplanar surfaces, very far away --------- *
     *
     * 5000 and 5010: two parts per thousand apart, at a quarter of the maximum
     * distance. A race track shows this kind of pair permanently - the road and
     * its markings, a bridge and its shadow. */
    {
        static const struct { float far_w, near_w; const char *name; } CASES[] = {
            { 5010.0f, 5000.0f, "2 per thousand at 5000" },
            { 15030.0f, 15000.0f, "2 per thousand at 15000" },
            /* A case deliberately beyond what sixteen bits can hold: if neither
               buffer resolves it, the limit has been reached and not that one is
               better. A comparison with no saturation point does not say where the
               wall is. */
            { 15003.0f, 15000.0f, "0.2 per thousand at 15000" },
        };
        int c;

        /* **Two warm-up frames, thrown away.**
         *
         * This witness's first measurement gave an impossible result: the W buffer
         * failed at 5000 and succeeded at 10000 and 15000, whereas precision
         * degrades with distance and never improves. The anomaly struck only the
         * very first case measured, which points at the card's state on opening
         * rather than at depth.
         *
         * Measuring the first frame after opening a context is measuring a machine
         * that has not finished settling in. */
        say("\n-- W buffer --\n");
        dkr_glide_backend_depth_mode(1);       /* 1 = W */
        {
            int warmup;
            for (warmup = 0; warmup < 2; warmup++) {
                bk.begin_frame(bk.self, 0x000000);
                bk.set_state(bk.self, &st);
                quad(&bk, 5010.0f, 255.0f, 0.0f, 0.0f, W, H);
                quad(&bk, 5000.0f, 0.0f, 255.0f, 0.0f, W, H);
                bk.present(bk.self);
            }
        }
        for (c = 0; c < 3; c++) {
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            quad(&bk, CASES[c].far_w, 255.0f, 0.0f, 0.0f, W, H);  /* red, far */
            quad(&bk, CASES[c].near_w, 0.0f, 255.0f, 0.0f, W, H);  /* green, near */
            bk.present(bk.self);
            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            {
                const int n = lost(got, 0x00FF00u);
                say("  %-26s: %6d fighting pixels out of %d (%d per thousand)\n",
                    CASES[c].name, n, got, got ? (1000 * n / got) : 0);
                if (c == 0) { fight_w = n; }
            }
        }

        say("\n-- Z buffer --\n");
        dkr_glide_backend_depth_mode(0);       /* 0 = Z */
        {
            int warmup;
            for (warmup = 0; warmup < 2; warmup++) {
                bk.begin_frame(bk.self, 0x000000);
                bk.set_state(bk.self, &st);
                quad(&bk, 5010.0f, 255.0f, 0.0f, 0.0f, W, H);
                quad(&bk, 5000.0f, 0.0f, 255.0f, 0.0f, W, H);
                bk.present(bk.self);
            }
        }
        for (c = 0; c < 3; c++) {
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            quad(&bk, CASES[c].far_w, 255.0f, 0.0f, 0.0f, W, H);
            quad(&bk, CASES[c].near_w, 0.0f, 255.0f, 0.0f, W, H);
            bk.present(bk.self);
            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            {
                const int n = lost(got, 0x00FF00u);
                say("  %-26s: %6d fighting pixels out of %d (%d per thousand)\n",
                    CASES[c].name, n, got, got ? (1000 * n / got) : 0);
                if (c == 0) { fight_z = n; }
            }
        }

        say("\n  verdict: %s\n",
            (fight_w < fight_z) ? "the W buffer separates better at distance" :
            (fight_z < fight_w) ? "the Z buffer separates better at distance" :
                                  "the two are equal on this case");
        /* The check is not "W wins" - that would presume the result. It is that
           one of the two really resolves the closest case, without which sixteen
           bits would be good for nothing and the depth range would have to be
           reconsidered rather than the buffer choice. */
        check("at least one of the two buffers resolves the case at 5000",
              (fight_w >= 0 && fight_w * 100 < 307200) ||
              (fight_z >= 0 && fight_z * 100 < 307200));
    }

    /* --- Agreement with E04-S03 ---------------------------------------------- *
     *
     * The depth range must coincide with what the chain produces. A disagreement
     * does not show on an isolated surface: it gives a globally wrong sort, hence
     * one piece of scenery passing in front of another - very visible, and blamed
     * on the decoder rather than on the range. */
    dkr_glide_backend_depth_mode(1);
    {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, 15000.0f, 255.0f, 0.0f, 0.0f, W, H);
        quad(&bk, 20.0f,     0.0f, 0.0f, 255.0f, W, H);
        bk.present(bk.self);
        got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
        say("\n-- agreement of the range with E04-S03 --\n");
        say("  normalised depth: at 20 = %.5f, at 15000 = %.5f\n",
            (double)depth_ndc(20.0f), (double)depth_ndc(15000.0f));
        check("over the whole range, the near hides the far",
              lost(got, 0x0000FFu) * 100 < got);
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
