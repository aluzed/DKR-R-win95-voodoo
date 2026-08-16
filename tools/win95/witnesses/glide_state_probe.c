/* Checking Glide 2.x's constants **by measurement**, one by one.
 *
 * `glide_backend.c` translates the render state into Glide calls, using
 * enumeration values written from memory: there is no `glide.h` on this machine.
 * The danger is not that a wrong value crashes - Glide validates nothing - but
 * that it programs a neighbouring register. The image comes out different,
 * without an error, and the deviation is then blamed on the display-list decoder.
 *
 * Each trial below exercises **one single** mode, draws, then reads the frame
 * buffer back through `grLfbLock`. The expected result is worked out by hand and
 * written into the code: this is not a screenshot one looks at, it is a pixel one
 * compares.
 *
 * What passes here is a dated fact; what fails names the constant to revisit.
 */
#include "render/glide.h"
#include "render/backend.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

static void check(const char *what, int ok, unsigned got, unsigned expected)
{
    say("  %s %-46s  read 0x%06X  expected ~0x%06X\n",
        ok ? "ok  " : "FAIL", what, got & 0x00FFFFFFu, expected & 0x00FFFFFFu);
    if (!ok) { g_fails++; }
}

/* The card works in 565: two colours less than one quantisation step apart are
   the same colour. Comparing to the byte would reject correct results - and
   accepting too broadly would prove nothing. Eight units, that is one step of red
   and of blue, is the smallest honest tolerance. */
static int near_color(unsigned a, unsigned b, int tol)
{
    int i;
    for (i = 0; i < 3; i++) {
        const int ca = (int)((a >> (i * 8)) & 0xFF);
        const int cb = (int)((b >> (i * 8)) & 0xFF);
        const int d  = ca - cb;
        if (d > tol || d < -tol) { return 0; }
    }
    return 1;
}

/* --- The trial scene --------------------------------------------------------- *
 *
 * A triangle covering the centre of the screen generously, of which we read only
 * one pixel: the exact centre. Every vertex carries the same colour, so that
 * interpolation cannot be confused with the effect being measured. */
static void triangle(dkr_render_backend *bk, unsigned char r, unsigned char g,
                     unsigned char bl, unsigned char a, float oow)
{
    dkr_render_vertex v[3];
    int i;
    const float xs[3] = { 320.0f,  600.0f,   40.0f };
    const float ys[3] = {  40.0f,  440.0f,  440.0f };

    memset(v, 0, sizeof(v));
    for (i = 0; i < 3; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = (float)r; v[i].g = (float)g; v[i].b = (float)bl;
        v[i].a = (float)a;
        v[i].oow = oow;
        v[i].ooz = 0.0f;
    }
    bk->draw_triangles(bk->self, v, 1);
}

static unsigned centre_pixel(void)
{
    static unsigned px[640 * 480];
    int w = 0, h = 0;
    if (dkr_glide_read_framebuffer(px, 640 * 480, &w, &h) <= 0 || w <= 0) {
        return 0xDEADBEEFu;
    }
    return px[(size_t)(h / 2) * (size_t)w + (size_t)(w / 2)];
}

/* The number of non-black pixels, for the trials where one wants to know whether
   anything was drawn at all - the alpha test, for instance, whose effect is
   "nothing" and not "another colour". */
static int painted_count(void)
{
    static unsigned px[640 * 480];
    int w = 0, h = 0, i, n = 0;
    const int got = dkr_glide_read_framebuffer(px, 640 * 480, &w, &h);
    for (i = 0; i < got; i++) {
        if ((px[i] & 0x00FFFFFFu) != 0) { n++; }
    }
    return n;
}

static void base_state(dkr_render_state *st)
{
    memset(st, 0, sizeof(*st));
    st->combine = DKR_COMBINE_SHADE;
    st->blend   = DKR_BLEND_OPAQUE;
    st->depth   = DKR_DEPTH_DISABLED;
    st->cull    = DKR_CULL_NONE;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;

    g_out = fopen("D:\\GLSTATE.TXT", "w");
    say("checking Glide 2.x's constants by reading the buffer back\n\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, 640, 480)) {
        say("FAILED: the card will not open\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }

    /* --- Blending: opaque ---------------------------------------------------- */
    base_state(&st);
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000040);            /* dark blue background */
    triangle(&bk, 255, 0, 0, 255, 1.0f);
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("opaque blending: the red covers the background",
              near_color(c, 0xFF0000u, 8), c, 0xFF0000u);
    }

    /* --- Blending: alpha ------------------------------------------------------ *
     *
     * Red at alpha 128 on a black background: half the red, that is 0x800000. If
     * `GR_BLEND_SRC_ALPHA` actually names another factor, we shall get full red or
     * black - two very recognisable results. */
    st.blend = DKR_BLEND_ALPHA;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 0, 0, 128, 1.0f);
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("alpha blending: half red on black",
              near_color(c, 0x800000u, 12), c, 0x800000u);
    }

    /* --- Blending: additive --------------------------------------------------- *
     * Full red added to a blue background: the two components coexist. */
    st.blend = DKR_BLEND_ADDITIVE;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000040);
    triangle(&bk, 255, 0, 0, 255, 1.0f);
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("additive blending: red + blue = magenta",
              near_color(c, 0xFF0040u, 12), c, 0xFF0040u);
    }

    /* --- Depth: the comparison's direction in w mode -------------------------- *
     *
     * **This is the most important trial in the file.** In a w buffer, a near
     * object has a large `1/w`, so the comparison reverses relative to a z buffer.
     * Getting the direction wrong does not empty the screen: it paints the scene
     * inside out, which goes unnoticed on a simple scene and becomes
     * incomprehensible on the game.
     *
     * We draw the far one first, then the near one: the near one must win. */
    st.blend = DKR_BLEND_OPAQUE;
    st.depth = DKR_DEPTH_TEST_AND_WRITE;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 0, 255, 0, 255, 0.001f);        /* far : 1/w small */
    triangle(&bk, 255, 0, 0, 255, 1.0f);          /* near: 1/w large */
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("depth: the near one covers the far one",
              near_color(c, 0xFF0000u, 8), c, 0xFF0000u);
    }

    /* And the reverse order - the near one first. The far one must be rejected.
       Without this second half, a depth test that was simply disabled would pass
       the first. */
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 0, 0, 255, 1.0f);          /* near first */
    triangle(&bk, 0, 255, 0, 255, 0.001f);        /* far next */
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("depth: the far one is rejected by the near one",
              near_color(c, 0xFF0000u, 8), c, 0xFF0000u);
    }

    /* --- Scissor -------------------------------------------------------------- *
     * Window limited to the right half: the exact centre is on the boundary, so we
     * read by counting rather than by pixel. A triangle covering ~75000 pixels
     * must lose roughly half of them. */
    st.depth = DKR_DEPTH_DISABLED;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    bk.set_scissor(bk.self, 320, 0, 640, 480);
    triangle(&bk, 255, 255, 255, 255, 1.0f);
    bk.present(bk.self);
    {
        const int n = painted_count();
        say("  %s scissor: right half only                        %d pixels painted\n",
            (n > 20000 && n < 60000) ? "ok  " : "FAIL", n);
        if (!(n > 20000 && n < 60000)) { g_fails++; }
    }
    bk.set_scissor(bk.self, 0, 0, 640, 480);

    /* --- Alpha test ----------------------------------------------------------- *
     * Reference 128: a triangle at alpha 64 must disappear entirely. */
    st.alpha_test      = 1;
    st.alpha_reference = 128;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 255, 255, 64, 1.0f);
    bk.present(bk.self);
    {
        const int n = painted_count();
        say("  %s alpha test: 64 < 128, nothing is painted         %d pixels painted\n",
            (n < 100) ? "ok  " : "FAIL", n);
        if (n >= 100) { g_fails++; }
    }

    /* And the complement: alpha 200 passes. Without it, an alpha test stuck on
       "never" would pass the previous trial. */
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 255, 255, 200, 1.0f);
    bk.present(bk.self);
    {
        const int n = painted_count();
        say("  %s alpha test: 200 >= 128, the triangle passes      %d pixels painted\n",
            (n > 60000) ? "ok  " : "FAIL", n);
        if (n <= 60000) { g_fails++; }
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
