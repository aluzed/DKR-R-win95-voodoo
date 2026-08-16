/* Replay the render state the game produces, and read the pixels back.
 *
 * The game's screen is black even though its six inputs all measured healthy:
 * vertex colour at 255, non-empty textures, texture bound, a combiner that
 * reads the texel, opaque blending, depth ruled out. Whatever is wrong is in
 * what actually gets programmed on the card, and instrumenting the game cannot
 * reach it -- there you observe what you send, never what comes out.
 *
 * This probe sets the same state, draws a known triangle, and reads the frame
 * buffer back.
 *
 * ## What makes it useful: bisection
 *
 * It does not set the state once but six times, degrading it step by step from
 * closest-to-the-game down to the simplest possible draw. The first case that
 * paints names the culprit, because it is the only thing that changed between
 * it and the previous case. A probe that only tested the full state would say
 * "black" and teach nothing the game had not already said.
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
    say("  %s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) { g_fails++; }
}

static unsigned short g_tex[32 * 32];
static unsigned       g_px[640 * 480];

/* A hard checkerboard: no way to confuse "nothing drawn" with "drawn in a
   colour close to the background". */
static void build_tex(void)
{
    int x, y;
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            const int light = ((x / 4) + (y / 4)) & 1;
            g_tex[y * 32 + x] = (unsigned short)(light ? 0xFFFFu : 0x8421u);
        }
    }
}

/* A triangle covering most of the screen, carrying the colour and coordinates
   the game produces: shade at 255, s and t normalised then scaled into Glide's
   256-texel space, and an oow taken from the range measured on the machine. */
static void triangle(dkr_render_backend *bk, float oow)
{
    dkr_render_vertex v[3];
    const float xs[3] = {  40.0f, 600.0f,  40.0f };
    const float ys[3] = {  40.0f,  40.0f, 440.0f };
    const float ss[3] = {   0.0f,   1.0f,   0.0f };
    const float ts[3] = {   0.0f,   0.0f,   1.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 3; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
        v[i].oow = oow;
        v[i].z = 0.5f;
        v[i].ooz = 0.5f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i] * DKR_TEXCOORD_SCALE * oow;
        v[i].tmu[0][DKR_TMU_TOW] = ts[i] * DKR_TEXCOORD_SCALE * oow;
        v[i].tmu[0][DKR_TMU_OOW] = oow;
    }
    bk->draw_triangles(bk->self, v, 1);
}

/* **Two sampled points, not a count against an assumed background.**
 *
 * The first version of this probe counted pixels differing from the clear
 * colour it had asked for. It returned 307200 out of 307200 -- the whole
 * screen -- for all six cases, including cases that cannot possibly paint the
 * same thing. A saturated count does not mean "everything is painted": it means
 * the reference colour is wrong, the card not reading back in the format it was
 * assumed to.
 *
 * So we read two points and print their values: one inside the triangle, one
 * outside. Two concrete colours cannot saturate, and their difference is
 * exactly the question -- was the triangle painted. */
static unsigned sample(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   d;
    dkr_texture_handle h;
    const int W = 640, H = 480;
    const unsigned BACKGROUND = 0x000040u;   /* dark blue, distinct from black */
    int rw = 0, rh = 0, got, i;
    int first_painted = -1;

    g_out = fopen("D:\\TEST.TXT", "w");
    say("the game's render state, replayed and read back\n\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAIL: cannot open Glide\n"); return 1; }

    build_tex();
    memset(&d, 0, sizeof(d));
    d.key = 0x9001ull;
    d.format = DKR_TEXFMT_RGBA5551;
    d.width = 32; d.height = 32;
    d.pixels = g_tex;
    d.size_bytes = sizeof(g_tex);

    /* Warm-up: the lesson from E05-S05, two frames thrown away. Measuring the
       first frame after opening a context measures a card that has not finished
       settling. */
    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, BACKGROUND);
        bk.set_state(bk.self, &st);
        bk.present(bk.self);
    }

    bk.begin_frame(bk.self, BACKGROUND);
    h = bk.texture_upload(bk.self, &d);
    check("the probe texture uploads", h != 0);

    /* --- The bisection ------------------------------------------------------
     *
     * From closest-to-the-game down to the simplest draw. The first case that
     * paints names the culprit: it is the only thing that changed. */
    {
        static const struct {
            const char       *name;
            dkr_combine_mode  combine;
            dkr_blend_mode    blend;
            dkr_depth_mode    depth;
            int               with_texture;
            float             oow;
        } CASES[] = {
            { "the game's state, as is",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_TEST_AND_WRITE, 1, 0.001f },
            { "without depth",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 0.001f },
            { "oow of 1 instead of 0.001",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 1.0f },
            { "texel-only combiner",
              DKR_COMBINE_TEXTURE, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 1.0f },
            { "no texture bound",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 0, 1.0f },
            { "vertex colour only",
              DKR_COMBINE_SHADE, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 0, 1.0f },
        };
        const int N = (int)(sizeof(CASES) / sizeof(CASES[0]));
        int c;

        say("\n%-40s %s\n", "case", "sampled colours");
        for (c = 0; c < N; c++) {
            memset(&st, 0, sizeof(st));
            st.combine = CASES[c].combine;
            st.blend   = CASES[c].blend;
            st.depth   = CASES[c].depth;
            st.cull    = DKR_CULL_NONE;
            st.filter  = DKR_FILTER_BILINEAR;
            st.wrap_s  = st.wrap_t = DKR_WRAP_REPEAT;
            st.texture = CASES[c].with_texture ? h : 0;

            bk.begin_frame(bk.self, BACKGROUND);
            bk.set_state(bk.self, &st);
            triangle(&bk, CASES[c].oow);
            bk.present(bk.self);

            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            if (got > 0) {
                /* 200,200 lies inside the triangle (40,40)-(600,40)-(40,440);
                   620,460 lies outside it, in the opposite corner. */
                const unsigned inside  = sample(200, 200, rw);
                const unsigned outside = sample(620, 460, rw);
                say("%-40s in=0x%06X out=0x%06X %s\n",
                    CASES[c].name, inside, outside,
                    (inside != outside) ? "<-- painted" : "");
                if (inside != outside && first_painted < 0) { first_painted = c; }
            } else {
                say("%-40s read-back failed\n", CASES[c].name);
            }
        }

        say("\n");
        if (first_painted < 0) {
            say("  nothing painted: the defect is upstream of the state\n");
        } else {
            say("  first case that paints: %s\n", CASES[first_painted].name);
            if (first_painted > 0) {
                say("  so the culprit is what that case removes\n");
            }
        }
        /* The check that stops this probe passing vacuously: at least one case
           must paint. Otherwise the state is not what is at fault but the
           context, the scissor window or the geometry -- and that has to be
           known before reading anything above. */
        check("at least one case paints something", first_painted >= 0);
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
