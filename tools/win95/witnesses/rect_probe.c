/* E05-S07 - 2D rectangles, to the pixel.
 *
 * The ticket is explicit about the method: the half-texel offset "is determined
 * by experiment - draw a one-pixel grid on a contrasting background and check its
 * alignment - not by reasoning".
 *
 * It is right, and for a reason that goes beyond Glide: the offset depends on the
 * card's sampling convention, on the pixel centre it assumes, and on its
 * interpolator's rounding. None of those three things is documented on this
 * machine, and their composition cannot be deduced.
 *
 * ## Why a one-pixel grid
 *
 * A texture with one-pixel lines is the severest trial there is for alignment: at
 * the slightest offset, a line falls between two pixels and disappears or
 * doubles. A texture with wide patterns would forgive a half-texel without
 * showing it, and that is precisely what must not happen.
 *
 * The interface is also what the player looks at longest. A half-texel offset is
 * the kind of defect one stops seeing after a few hours and which leaps out at
 * anyone discovering the port.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/clip.h"

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

#define TW 64
#define TH 64
static unsigned short g_grid[TW * TH];
static unsigned g_px[640 * 480];

/* A grid: a one-texel white line every four columns and rows, on a black
   background. Maximally contrasted, and one texel wide. */
static void build_grid(void)
{
    int x, y;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            const int line = (x % 4 == 0) || (y % 4 == 0);
            g_grid[y * TW + x] = (unsigned short)(0x8000u | (line ? 0x7FFFu : 0u));
        }
    }
}

/* A rectangle in screen coordinates, without transformation - that is what the
 * RDP does with its rectangle commands, and putting them through the transform
 * pipeline would introduce a needless conversion and an opportunity for error.
 *
 * `offset` is the half-texel under trial, in texels of the texture. */
static void rect(dkr_render_backend *bk, float x0, float y0, float x1, float y1,
                 float s0, float t0, float s1, float t1, float offset,
                 unsigned argb)
{
    dkr_render_vertex v[6];
    const float xs[6] = { x0, x1, x1, x0, x1, x0 };
    const float ys[6] = { y0, y0, y1, y0, y1, y1 };
    const float ss[6] = { s0, s1, s1, s0, s1, s0 };
    const float ts[6] = { t0, t0, t1, t0, t1, t1 };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = (float)((argb >> 16) & 0xFF);
        v[i].g = (float)((argb >> 8) & 0xFF);
        v[i].b = (float)(argb & 0xFF);
        v[i].a = (float)((argb >> 24) & 0xFF);
        v[i].oow = 1.0f;
        /* The 256-texel scale is Glide's, measured in E05-S02. */
        v[i].tmu[0][DKR_TMU_SOW] = (ss[i] + offset) * (256.0f / (float)TW);
        v[i].tmu[0][DKR_TMU_TOW] = (ts[i] + offset) * (256.0f / (float)TH);
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned read_px(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

/* How many columns fall **where they should**.
 *
 * The first version counted the "clean" columns, that is, neither grey nor
 * intermediate. It returned 64 out of 64 for *every* offset, and discriminated
 * nothing: under point sampling there is never an intermediate value, only
 * displaced lines. A metric that cannot fail does not measure.
 *
 * So we compare against the expected grid: at scale one, texel `x` must fall on
 * pixel `x`, hence a white line every four columns starting from zero. The right
 * offset is the one that puts the lines opposite. */
static int columns_matching(int x0, int width, int y, int w)
{
    int x, n = 0;
    for (x = x0; x < x0 + width; x++) {
        const unsigned r = (read_px(x, y, w) >> 16) & 0xFF;
        const int expect_white = ((x - x0) % 4) == 0;
        const int is_white = r > 128u;
        if (is_white == expect_white) { n++; }
    }
    return n;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   d;
    dkr_texture_handle h;
    const int W = 640, H = 480;
    int rw = 0, rh = 0, i;
    float best = 0.0f;
    int best_score = -1;

    g_out = fopen("D:\\RECT.TXT", "w");
    say("2D rectangles: half-texel, seams, scissor\n\n");

    build_grid();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAILED to open\n"); return 1; }

    memset(&d, 0, sizeof(d));
    d.key = 0x9999ull; d.format = DKR_TEXFMT_ARGB1555;
    d.width = TW; d.height = TH;
    d.pixels = g_grid; d.size_bytes = sizeof(g_grid);
    bk.begin_frame(bk.self, 0x000000);
    h = bk.texture_upload(bk.self, &d);
    check("the grid uploads", h != 0);

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;     /* point: bilinear would blur the measurement */
    st.wrap_s  = st.wrap_t = DKR_WRAP_CLAMP;
    st.texture = h;

    /* Two warm-up frames - E05-S05's lesson. */
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        rect(&bk, 0, 0, 64, 64, 0, 0, (float)TW, (float)TH, 0.0f, 0xFFFFFFFFu);
        bk.present(bk.self);
    }

    /* --- The half-texel, searched for rather than reasoned about ------------- *
     *
     * The grid is drawn at scale one - 64 texels over 64 pixels - at several
     * offsets. The right one is the one that lands the most columns where they
     * belong. */
    say("\n-- the half-texel offset --\n");
    say("  grid of 64 texels over 64 pixels, one line every four\n");
    say("%-10s %s\n", "offset", "columns in place out of 64");
    {
        static const float TRIALS[] = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (i = 0; i < 7; i++) {
            int n;
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            rect(&bk, 0, 0, 64, 64, 0, 0, (float)TW, (float)TH, TRIALS[i],
                 0xFFFFFFFFu);
            bk.present(bk.self);
            if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
            n = columns_matching(0, 64, 34, rw);
            say("%-10.2f %d\n", (double)TRIALS[i], n);
            if (n > best_score) { best_score = n; best = TRIALS[i]; }
        }
        say("\n  best offset: %.2f texel (%d columns in place out of 64)\n",
            (double)best, best_score);
        check("one offset puts every column in place",
              best_score == 64);
    }

    /* --- The seams ----------------------------------------------------------- *
     *
     * Four adjacent rectangles, of different colours, laid edge to edge. A wrong
     * fill rule leaves a line of background between them, or makes them overlap.
     * Tiled backgrounds are where this shows most, and the game's interface is
     * made of them. */
    say("\n-- the seams between adjacent rectangles --\n");
    {
        static const unsigned COLOURS[4] = {
            0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u
        };
        int blacks = 0, x;
        st.combine = DKR_COMBINE_SHADE;
        st.texture = 0;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        for (i = 0; i < 4; i++) {
            rect(&bk, (float)(100 + i * 50), 100.0f,
                      (float)(100 + (i + 1) * 50), 200.0f,
                 0, 0, 0, 0, 0.0f, COLOURS[i]);
        }
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
            for (x = 100; x < 300; x++) {
                if (read_px(x, 150, rw) == 0u) { blacks++; }
            }
            say("  background pixels on the line of four rectangles: %d out of 200\n",
                blacks);
            check("no seam between adjacent rectangles", blacks == 0);
            /* And the negative control: the four colours must really be present,
               without which a single rectangle covering everything would pass. */
            check("and the four rectangles are indeed distinct",
                  read_px(120, 150, rw) != read_px(170, 150, rw) &&
                  read_px(170, 150, rw) != read_px(220, 150, rw) &&
                  read_px(220, 150, rw) != read_px(270, 150, rw));
        }
    }

    /* --- The HUD in split screen --------------------------------------------- *
     *
     * The interface rectangles are constrained by E04-S05's scissor window. We
     * check that a full-screen rectangle, restricted to one player's quadrant,
     * stays inside it. */
    say("\n-- the HUD in split screen --\n");
    {
        static const struct { int players, player; const char *name; } CASES[] = {
            { 2, 0, "2 players, top" },
            { 2, 1, "2 players, bottom" },
            { 4, 0, "4 players, top-left" },
            { 4, 3, "4 players, bottom-right" },
        };
        for (i = 0; i < 4; i++) {
            dkr_scissor sc;
            long painted = 0;
            int j, expected;
            if (!dkr_scissor_for_player(CASES[i].players, CASES[i].player, W, H, &sc)) {
                continue;
            }
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            bk.set_scissor(bk.self, sc.x0, sc.y0, sc.x1, sc.y1);
            rect(&bk, 0, 0, (float)W, (float)H, 0, 0, 0, 0, 0.0f, 0xFFFFFFFFu);
            bk.present(bk.self);
            bk.set_scissor(bk.self, 0, 0, W, H);
            if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
            for (j = 0; j < rw * rh; j++) {
                if ((g_px[j] & 0x00FFFFFFu) != 0u) { painted++; }
            }
            expected = (sc.x1 - sc.x0) * (sc.y1 - sc.y0);
            say("  %-24s painted %6ld, expected %6d\n",
                CASES[i].name, painted, expected);
            check(CASES[i].name, painted == expected);
        }
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
