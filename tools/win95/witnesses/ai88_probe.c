/* `GR_TEXFMT_ALPHA_INTENSITY_88`: which byte is which, and how many alpha bits.
 *
 * Every texture this port uploads goes to the card as ARGB1555, which has **one
 * alpha bit**. The N64's `IA16` has eight, and `texture.c` thresholds them at the
 * midpoint — a loss its own comment flags, and one that shows: the halo sprites
 * on the character-select screen come out as opaque grey slabs.
 *
 * The Voodoo has a format that matches `IA` exactly and costs the same sixteen
 * bits a texel. Before anything is written to use it, two things have to be true
 * of the card and neither is worth taking from memory:
 *
 *   1. **Which byte carries the alpha.** The name says alpha first; this port has
 *      already had one texel layout wrong from memory — ARGB1555 written as
 *      RGBA5551 for four months — and the symptom was a magenta sky nobody could
 *      attribute. Reading it off the card costs one quad.
 *   2. **That the alpha really has eight bits.** A format the card accepts and
 *      then rounds to one bit would fix nothing, and would look exactly like the
 *      thing being fixed.
 *
 * ## Both questions have their answer worked out first
 *
 * A uniform texel of intensity 0xC0 and alpha 0x30 drawn opaquely returns the
 * *intensity*, so the frame buffer says which byte held it: 192 or 48, and
 * nothing in between.
 *
 * Then four bands of alpha 0, 85, 170, 255 over a pure blue background, texture
 * intensity 255, blended `src_alpha / one_minus_src_alpha`. The red channel of
 * the result **is** the alpha: 0, 85, 170, 255. One alpha bit would give
 * 0, 0, 255, 255, which is not near-miss but a different answer.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/tmu.h"

#include <stdarg.h>
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

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) { g_fails++; }
}

#define TW 64
#define TH 64
static unsigned short g_texture[TW * TH];
static unsigned       g_pixels[640 * 480];

/* The two candidate readings, named rather than numbered so that the log says
   what was measured and not which branch was taken. */
#define ORDER_ALPHA_HIGH 1
#define ORDER_ALPHA_LOW  0

static void fill_uniform(unsigned int high, unsigned int low)
{
    const unsigned short w = (unsigned short)(((high & 0xFFu) << 8) | (low & 0xFFu));
    int i;
    for (i = 0; i < TW * TH; i++) { g_texture[i] = w; }
}

/* Four horizontal bands of sixteen rows each. */
static const unsigned int BAND_ALPHA[4] = { 0u, 85u, 170u, 255u };

static void fill_bands(int alpha_high)
{
    int y, x;
    for (y = 0; y < TH; y++) {
        const unsigned int a = BAND_ALPHA[y / (TH / 4)];
        const unsigned int i = 255u;
        const unsigned short w = alpha_high
            ? (unsigned short)((a << 8) | i)
            : (unsigned short)((i << 8) | a);
        for (x = 0; x < TW; x++) { g_texture[y * TW + x] = w; }
    }
}

static void draw_quad(dkr_render_backend *bk, int w, int h, float scale)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, (float)w, (float)w, 0.0f, (float)w, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, (float)h, 0.0f, (float)h, (float)h };
    const float ss[6] = { 0.0f, scale, scale, 0.0f, scale, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, scale, 0.0f, scale, scale };
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

/* A flat quad in one colour, no texture: the background the blend is measured
   against. It goes through the same path as everything else so that the state
   the card ends up in is the state the next draw expects. */
static void draw_background(dkr_render_backend *bk, int w, int h,
                            float r, float g, float b)
{
    dkr_render_state st;
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, (float)w, (float)w, 0.0f, (float)w, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, (float)h, 0.0f, (float)h, (float)h };
    int i;

    /* **A zeroed block means "no recipe", and that took a black frame to get.**
       `recipe` used to hold the catalogue index as it was, with -1 for "none";
       a `memset` therefore selected entry 0, a two-texel configuration this port
       cannot serve, and this witness painted nothing until the field was set by
       hand. It is stored plus one now, so the zero a `memset` leaves is the safe
       value and no witness has to know. */
    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    bk->set_state(bk->self, &st);

    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = 255.0f;
        v[i].oow = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned read_at(int x, int y, int w)
{
    return g_pixels[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

static int near_to(int got, int want, int tol)
{
    const int d = (got > want) ? (got - want) : (want - got);
    return d <= tol;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   desc;
    dkr_texture_handle handle;
    const int W = 640, H = 480;
    int rw = 0, rh = 0;
    int alpha_high = ORDER_ALPHA_HIGH;
    int red_at_band[4];
    int i;

    g_out = fopen("D:\\AI88.TXT", "w");
    say("ALPHA_INTENSITY_88 on the card: byte order, then alpha depth\n\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) {
        say("FAILED: the card will not open\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }

    memset(&st, 0, sizeof(st));      /* recipe zero is "none": see draw_background */
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;
    st.wrap_s  = DKR_WRAP_CLAMP;
    st.wrap_t  = DKR_WRAP_CLAMP;

    memset(&desc, 0, sizeof(desc));
    desc.format     = DKR_TEXFMT_ALPHA_INTENSITY88;
    desc.width      = TW;
    desc.height     = TH;
    desc.pixels     = g_texture;
    desc.size_bytes = sizeof(g_texture);

    /* --- 1. Which byte is the intensity ------------------------------------ */
    say("-- byte order --\n");
    fill_uniform(0xC0u, 0x30u);
    bk.begin_frame(bk.self, 0x000000);
    desc.key = 0xA188000000000001ull;
    handle = bk.texture_upload(bk.self, &desc);
    check("the card accepts an AI88 texture", handle != 0);
    if (handle == 0) {
        say("\n  nothing further can be measured without a handle\n");
        bk.close(bk.self);
        say("\n%d failure(s)\n", g_fails + 1);
        if (g_out) { fclose(g_out); }
        return 1;
    }
    st.texture = handle;
    bk.set_state(bk.self, &st);
    draw_quad(&bk, W, H, (float)TW);
    bk.present(bk.self);

    if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) <= 0) {
        say("FAILED: the frame buffer cannot be read back\n");
        bk.close(bk.self);
        if (g_out) { fclose(g_out); }
        return 1;
    }
    {
        const unsigned c = read_at(rw / 2, rh / 2, rw);
        const int red = (int)((c >> 16) & 0xFFu);
        say("  texel 0x%04X drawn opaque -> %06X, red=%d\n",
            (unsigned)g_texture[0], c, red);
        if (near_to(red, 0xC0, 12)) {
            alpha_high = ORDER_ALPHA_LOW;
            say("  the HIGH byte is the intensity, so the LOW byte is the alpha\n");
        } else if (near_to(red, 0x30, 12)) {
            alpha_high = ORDER_ALPHA_HIGH;
            say("  the LOW byte is the intensity, so the HIGH byte is the alpha\n");
        } else {
            check("the intensity is one of the two bytes and neither is a mix", 0);
            say("  neither 192 nor 48 came back; the format is not what is assumed\n");
        }
        check("the intensity reaches the frame buffer unaltered",
              near_to(red, 0xC0, 12) || near_to(red, 0x30, 12));
    }

    /* --- 2. How many alpha bits -------------------------------------------- *
     *
     * Blue background, white texture, alpha in four steps. The red channel of
     * the blend is the alpha and nothing else: 0, 85, 170, 255 if the card
     * carries eight bits, 0, 0, 255, 255 if it carries one. */
    say("\n-- alpha depth --\n");
    fill_bands(alpha_high);
    bk.begin_frame(bk.self, 0x000000);
    draw_background(&bk, W, H, 0.0f, 0.0f, 255.0f);

    desc.key = 0xA188000000000002ull;
    handle = bk.texture_upload(bk.self, &desc);
    check("the banded texture uploads", handle != 0);
    say("  handle=%u, first band texel=0x%04X, last=0x%04X\n",
        (unsigned)handle, (unsigned)g_texture[0],
        (unsigned)g_texture[(TH - 1) * TW]);
    st.texture = handle;
    st.blend   = DKR_BLEND_ALPHA;
    bk.set_state(bk.self, &st);
    draw_quad(&bk, W, H, (float)TW);
    bk.present(bk.self);

    if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) <= 0) {
        say("FAILED: the frame buffer cannot be read back\n");
        bk.close(bk.self);
        if (g_out) { fclose(g_out); }
        return 1;
    }
    say("  %-8s %8s %8s   %s\n", "alpha", "expected", "red", "pixel");
    for (i = 0; i < 4; i++) {
        /* The middle of each band, and the middle of the screen across. */
        const int y = (rh / 8) + i * (rh / 4);
        const unsigned c = read_at(rw / 2, y, rw);
        red_at_band[i] = (int)((c >> 16) & 0xFFu);
        /* **The whole pixel, not the red alone.** A band that reads red=0 is
           either the blue background showing through an alpha of zero or a quad
           that never drew, and the red channel cannot tell those apart -- which
           is the same mistake as reading a coverage figure off a corner. */
        say("  %-8u %8u %8d   %06X\n",
            BAND_ALPHA[i], BAND_ALPHA[i], red_at_band[i], c);
    }
    {
        int all = 1;
        for (i = 0; i < 4; i++) {
            /* Twelve units of tolerance: the frame buffer is 565, so red is
               quantised to five bits — eight units — and the blend truncates. */
            if (!near_to(red_at_band[i], (int)BAND_ALPHA[i], 12)) { all = 0; }
        }
        check("every one of the four alphas comes back as itself", all);
        /* The one that says a fix is a fix. Under a single alpha bit the two
           middle bands collapse onto the ends, and this is the check that
           notices. */
        check("the two intermediate alphas are neither 0 nor 255",
              red_at_band[1] > 30 && red_at_band[1] < 200 &&
              red_at_band[2] > 60 && red_at_band[2] < 240);
    }

    /* --- 3. The control, and the reason it is here -------------------------- *
     *
     * Test 2 says the alpha of an AI88 texel does not reach the blender. That
     * sentence has two readings and they lead in opposite directions: either the
     * format is at fault, or this backend's alpha path is, in which case the
     * game's own hundred and fifty alpha-blended triangles a frame are wrong too
     * and AI88 has nothing to do with it.
     *
     * The same quad in ARGB1555 with its alpha bit **set** separates them. Fully
     * opaque white over blue must come back white; anything else indicts the
     * path rather than the format. */
    say("\n-- control: the same blend with an ARGB1555 texel --\n");
    {
        int j;
        for (j = 0; j < TW * TH; j++) {
            g_texture[j] = (unsigned short)(0x8000u | (31u << 10) | (31u << 5) | 31u);
        }
        bk.begin_frame(bk.self, 0x000000);
        draw_background(&bk, W, H, 0.0f, 0.0f, 255.0f);
        desc.format = DKR_TEXFMT_ARGB1555;
        desc.key    = 0xA188000000000003ull;
        handle = bk.texture_upload(bk.self, &desc);
        st.texture = handle;
        st.blend   = DKR_BLEND_ALPHA;
        bk.set_state(bk.self, &st);
        draw_quad(&bk, W, H, (float)TW);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
            const unsigned c = read_at(rw / 2, rh / 2, rw);
            say("  opaque white 1555 over blue -> %06X\n", c);
            check("the alpha path carries an ARGB1555 alpha of one",
                  ((c >> 16) & 0xFFu) > 200u);
        } else {
            check("the control frame can be read back", 0);
        }
    }

    say("\n  the layout to write: alpha in the %s byte\n",
        alpha_high ? "HIGH" : "LOW");

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
