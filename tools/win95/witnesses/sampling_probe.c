/* E05-S08 - wrapping, clamping and filtering, measured for every size.
 *
 * Two of the ticket's premises fall before the card is even plugged in, and it is
 * better said than measuring what serves no purpose:
 *
 *   - **DKR uses no mipmaps.** `G_TL_TILE` appears fifteen times in the source,
 *     `G_TL_LOD` not once. The extra third of texture memory the ticket feared
 *     for E05-S02 does not exist, and distant textures will shimmer exactly as
 *     they do on the console.
 *   - **DKR uses no mirroring.** The ticket describes it as "much used to save
 *     texture memory"; the source does not contain a single occurrence of
 *     `G_TX_MIRROR`. Eighteen `G_TX_WRAP`, sixteen `G_TX_NOMIRROR`, four
 *     `G_TX_CLAMP`.
 *
 * What remains is to check wrapping and clamping **for every size in use** - that
 * is the criterion, and it bears on the sizes because Glide imposes dimension
 * constraints that the N64 does not.
 *
 * ## How to tell the two modes apart unambiguously
 *
 * The texture carries a red column on the left and nothing else. We draw it over
 * three repetitions: under wrapping, three red columns appear; under clamping,
 * one. The count decides without anyone having to look.
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

static unsigned short g_tex[256 * 256];
static unsigned g_px[640 * 480];

/* A red column at `x == 0`, the rest black. The simplest pattern that allows the
   repetitions to be counted. */
static void build(int size)
{
    int x, y;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            g_tex[y * size + x] = (unsigned short)
                (0x8000u | ((x == 0) ? (31u << 10) : 0u));
        }
    }
}

static void quad(dkr_render_backend *bk, int w, int h, float repetitions)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)w, (float)w, 0, (float)w, 0 };
    const float ys[6] = { 0, 0, (float)h, 0, (float)h, (float)h };
    const float s1 = 256.0f * repetitions;   /* Glide's 256-texel space */
    const float ss[6] = { 0, s1, s1, 0, s1, 0 };
    const float ts[6] = { 0, 0, 8.0f, 0, 8.0f, 8.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
        v[i].oow = 1.0f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

/* Counts the groups of red columns - not the columns, the groups: a stretched
   texture column covers several pixels, and counting the pixels would give a
   number that depends on the texture's size rather than on the mode. */
static int red_groups(int w, int y)
{
    int x, n = 0, inside = 0;
    for (x = 0; x < w; x++) {
        const unsigned c = g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
        const int red = ((c >> 16) & 0xFF) > 128u && ((c >> 8) & 0xFF) < 100u;
        if (red && !inside) { n++; }
        inside = red;
    }
    return n;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    const int W = 640, H = 480;
    static const int SIZES[] = { 4, 8, 16, 32, 64, 128, 256 };
    int rw = 0, rh = 0, i, t;
    unsigned long t_point, t_bilinear;
    int wrap_ok = 0, clamp_ok = 0;

    g_out = fopen("D:\\SAMPLING.TXT", "w");
    say("wrapping and clamping, for every size\n\n");
    say("  DKR uses neither mipmaps (G_TL_TILE x15, G_TL_LOD x0)\n");
    say("  nor mirroring (G_TX_MIRROR x0; WRAP x18, NOMIRROR x16, CLAMP x4)\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAILED to open\n"); return 1; }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;

    /* Warm-up - E05-S05's lesson. */
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        bk.present(bk.self);
    }

    say("\n%-8s %-14s %-14s\n", "size", "wrapping", "clamping");
    for (t = 0; t < 7; t++) {
        const int size = SIZES[t];
        dkr_texture_desc d;
        dkr_texture_handle h;
        int n_wrap = -1, n_clamp = -1;

        build(size);
        memset(&d, 0, sizeof(d));
        d.key = 0x5000ull + (unsigned)size;
        d.format = DKR_TEXFMT_ARGB1555;
        d.width = size; d.height = size;
        d.pixels = g_tex;
        d.size_bytes = (size_t)(size * size * 2);
        bk.begin_frame(bk.self, 0x000000);
        h = bk.texture_upload(bk.self, &d);
        if (!h) {
            say("%-8d  refused at upload\n", size);
            g_fails++;
            continue;
        }
        st.texture = h;

        st.wrap_s = DKR_WRAP_REPEAT;
        st.wrap_t = DKR_WRAP_REPEAT;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
            n_wrap = red_groups(rw, rh / 2);
        }

        st.wrap_s = DKR_WRAP_CLAMP;
        st.wrap_t = DKR_WRAP_CLAMP;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
            n_clamp = red_groups(rw, rh / 2);
        }

        say("%-8d %-14d %-14d %s\n", size, n_wrap, n_clamp,
            (n_wrap == 3 && n_clamp == 1) ? "" : "<-- unexpected");
        if (n_wrap == 3)  { wrap_ok++; }
        if (n_clamp == 1) { clamp_ok++; }
    }

    /* Three repetitions must give three columns under wrapping, one under
       clamping. A mode that did nothing would give the same count on both sides,
       and that is what comparing the two rows catches. */
    check("wrapping repeats for all seven sizes", wrap_ok == 7);
    check("clamping does not repeat, for all seven sizes", clamp_ok == 7);

    /* --- The cost of filtering ------------------------------------------------- *
     *
     * The ticket announces bilinear as free on a Voodoo. We check it rather than
     * believe it: it is cheap to measure, and a surprise here weighs on the whole
     * fill-rate budget. */
    st.wrap_s = st.wrap_t = DKR_WRAP_REPEAT;
    st.filter = DKR_FILTER_POINT;
    t_point = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
    }
    t_point = GetTickCount() - t_point;

    st.filter = DKR_FILTER_BILINEAR;
    t_bilinear = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
    }
    t_bilinear = GetTickCount() - t_bilinear;

    say("\n-- the cost of filtering --\n");
    say("  100 frames point-sampled : %lu ms\n", t_point);
    say("  100 frames bilinear      : %lu ms\n", t_bilinear);
    /* The threshold is generous on purpose: the measurement is quantised by the
       buffer swap, as E05-S06 established. What is sought here is not a fine
       figure but the absence of a surprise - a bilinear costing twice as much
       would show despite the quantisation. */
    check("bilinear does not cost significantly more",
          t_point == 0 || t_bilinear * 100u < t_point * 130u);

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
