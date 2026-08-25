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

    /* --- 4. The control's own control -------------------------------------- *
     *
     * Test 3 drew an ARGB1555 texel whose alpha bit was **set**, over a quad
     * whose vertices are opaque white, and read back white. That is what the
     * texture alpha arriving looks like — and it is also exactly what the
     * *vertex* alpha arriving looks like, the texture playing no part. The two
     * cannot be told apart by a value both would produce, which is the mistake
     * this repository keeps a list of.
     *
     * Clearing the alpha bit separates them. If the blender reads the texel, a
     * fully transparent white leaves the blue background; if it reads the
     * vertex, it paints white just the same. */
    say("\n-- and the control's control: 1555 with the alpha bit clear --\n");
    {
        int j;
        for (j = 0; j < TW * TH; j++) {
            g_texture[j] = (unsigned short)((31u << 10) | (31u << 5) | 31u);
        }
        bk.begin_frame(bk.self, 0x000000);
        draw_background(&bk, W, H, 0.0f, 0.0f, 255.0f);
        desc.format = DKR_TEXFMT_ARGB1555;
        desc.key    = 0xA188000000000004ull;
        handle = bk.texture_upload(bk.self, &desc);
        st.texture = handle;
        st.blend   = DKR_BLEND_ALPHA;
        bk.set_state(bk.self, &st);
        draw_quad(&bk, W, H, (float)TW);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
            const unsigned c = read_at(rw / 2, rh / 2, rw);
            say("  transparent white 1555 over blue -> %06X\n", c);
            say("  %s\n", (((c >> 16) & 0xFFu) < 60u)
                ? "the blender reads the TEXEL's alpha"
                : "the blender reads the VERTEX's alpha, and test 3 proved nothing");
            check("a texel alpha of zero leaves the background alone",
                  ((c >> 16) & 0xFFu) < 60u);
        } else {
            check("the second control frame can be read back", 0);
        }
    }

    /* --- 5. The byte order, asked the other way round ----------------------- *
     *
     * Test 1 read the intensity out of the low byte. A single reading of a
     * single texel is one arithmetic slip away from being an artefact, and the
     * cheapest way to strengthen it is to swap the two bytes and require the
     * answer to swap with them. `0x30C0` must return 192 where `0xC030`
     * returned 48. */
    say("\n-- byte order, the same question with the bytes swapped --\n");
    {
        fill_uniform(0x30u, 0xC0u);
        bk.begin_frame(bk.self, 0x000000);
        desc.format = DKR_TEXFMT_ALPHA_INTENSITY88;
        desc.key    = 0xA188000000000005ull;
        handle = bk.texture_upload(bk.self, &desc);
        st.texture = handle;
        st.blend   = DKR_BLEND_OPAQUE;
        bk.set_state(bk.self, &st);
        draw_quad(&bk, W, H, (float)TW);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
            const unsigned c = read_at(rw / 2, rh / 2, rw);
            const int red = (int)((c >> 16) & 0xFFu);
            say("  texel 0x30C0 drawn opaque -> %06X, red=%d\n", c, red);
            check("swapping the bytes swaps the intensity",
                  alpha_high ? near_to(red, 0xC0, 16) : near_to(red, 0x30, 16));
        } else {
            check("the swapped frame can be read back", 0);
        }
    }

    /* --- 6. ARGB4444, the other sixteen-bit format with an alpha ------------ *
     *
     * AI88 would have been exact and it does not deliver its alpha. ARGB4444
     * costs the same sixteen bits a texel and carries **four** alpha bits, which
     * is exactly what the N64's `IA8` holds and sixteen times what a threshold
     * leaves of `IA16`. Its layout mirrors ARGB1555's, which this port has
     * already had measured right.
     *
     * The same four bands, and the same answer known in advance: alpha nibbles
     * 0, 5, 10 and 15 blend to a red of 0, 85, 170 and 255. */
    say("\n-- ARGB4444, four alpha bits --\n");
    {
        static const unsigned int NIB[4] = { 0u, 5u, 10u, 15u };
        int y, x;
        for (y = 0; y < TH; y++) {
            const unsigned int a = NIB[y / (TH / 4)];
            const unsigned short w = (unsigned short)((a << 12) | 0x0FFFu);
            for (x = 0; x < TW; x++) { g_texture[y * TW + x] = w; }
        }
        bk.begin_frame(bk.self, 0x000000);
        draw_background(&bk, W, H, 0.0f, 0.0f, 255.0f);
        desc.format = DKR_TEXFMT_ARGB4444;
        desc.key    = 0xA188000000000006ull;
        handle = bk.texture_upload(bk.self, &desc);
        check("the card accepts an ARGB4444 texture", handle != 0);
        st.texture = handle;
        st.blend   = DKR_BLEND_ALPHA;
        bk.set_state(bk.self, &st);
        draw_quad(&bk, W, H, (float)TW);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
            int ok_all = 1, mid_ok;
            int red4[4];
            say("  %-8s %8s %8s   %s\n", "nibble", "expected", "red", "pixel");
            for (i = 0; i < 4; i++) {
                const int yy = (rh / 8) + i * (rh / 4);
                const unsigned c = read_at(rw / 2, yy, rw);
                red4[i] = (int)((c >> 16) & 0xFFu);
                say("  %-8u %8u %8d   %06X\n",
                    NIB[i], (NIB[i] * 255u) / 15u, red4[i], c);
                if (!near_to(red4[i], (int)((NIB[i] * 255u) / 15u), 20)) {
                    ok_all = 0;
                }
            }
            mid_ok = red4[1] > 30 && red4[1] < 200 &&
                     red4[2] > 60 && red4[2] < 240;
            check("every one of the four alphas comes back as itself", ok_all);
            check("the two intermediate alphas are neither 0 nor 255", mid_ok);
        } else {
            check("the ARGB4444 frame can be read back", 0);
        }
    }

    /* --- 7. Is it the format, or is it what came before it? ----------------- *
     *
     * Three formats, and the pattern is not the one the tests were built to
     * find. AI88 paints in tests 1 and 5 and vanishes in test 2; ARGB4444
     * vanishes in test 6 even at a fully opaque nibble, which no alpha depth can
     * explain. What the vanishing draws share is not their format: it is that a
     * quad in `DKR_COMBINE_SHADE` was drawn immediately before them, and
     * `apply_combine` returns early for that mode **without touching the TMU**.
     *
     * So the same AI88 texel that worked, drawn opaquely, with a shade quad in
     * front of it. If it disappears, the format was never the question. */
    say("\n-- the same AI88 texel, opaque, after a shade quad --\n");
    {
        fill_uniform(0xC0u, 0x30u);
        bk.begin_frame(bk.self, 0x000000);
        draw_background(&bk, W, H, 0.0f, 0.0f, 255.0f);
        desc.format = DKR_TEXFMT_ALPHA_INTENSITY88;
        desc.key    = 0xA188000000000007ull;
        handle = bk.texture_upload(bk.self, &desc);
        st.texture = handle;
        st.blend   = DKR_BLEND_OPAQUE;
        bk.set_state(bk.self, &st);
        draw_quad(&bk, W, H, (float)TW);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
            const unsigned c = read_at(rw / 2, rh / 2, rw);
            const int red = (int)((c >> 16) & 0xFFu);
            say("  texel 0xC030 opaque, shade quad first -> %06X, red=%d\n",
                c, red);
            say("  %s\n", near_to(red, 0x30, 16)
                ? "the texel still arrives: the shade quad is innocent"
                : "the texel is gone: what precedes a draw is the question, "
                  "not the format");
            check("a shade quad does not cost the next draw its texture",
                  near_to(red, 0x30, 16));
        } else {
            check("the sequence frame can be read back", 0);
        }
    }

    /* --- 8. Does ARGB4444's colour arrive at all? --------------------------- *
     *
     * Under `src_alpha / one_minus_src_alpha`, a source alpha of zero returns
     * the destination whatever the source colour was. So test 6's four blue
     * bands say the alpha is zero and say **nothing** about the colour — the
     * same conflation as reading a coverage figure off a corner.
     *
     * Drawn opaquely, the colour has nowhere to hide. A texel of `0x0F30`
     * — alpha nibble 0, red 15, green 3, blue 0 — must come back orange. If it
     * does, the card reads ARGB4444 and only its alpha is lost; if it does not,
     * the format is not being read at all. */
    say("\n-- ARGB4444 drawn opaque: does the colour arrive? --\n");
    {
        int j;
        for (j = 0; j < TW * TH; j++) { g_texture[j] = (unsigned short)0x0F30u; }
        bk.begin_frame(bk.self, 0x000000);
        desc.format = DKR_TEXFMT_ARGB4444;
        desc.key    = 0xA188000000000008ull;
        handle = bk.texture_upload(bk.self, &desc);
        st.texture = handle;
        st.blend   = DKR_BLEND_OPAQUE;
        bk.set_state(bk.self, &st);
        draw_quad(&bk, W, H, (float)TW);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
            const unsigned c = read_at(rw / 2, rh / 2, rw);
            const int r8 = (int)((c >> 16) & 0xFFu);
            const int g8 = (int)((c >> 8) & 0xFFu);
            const int b8 = (int)(c & 0xFFu);
            say("  texel 0x0F30 opaque -> %06X (r=%d g=%d b=%d)\n",
                c, r8, g8, b8);
            check("ARGB4444's colour reaches the frame buffer",
                  r8 > 200 && g8 > 20 && g8 < 90 && b8 < 40);
        } else {
            check("the ARGB4444 opaque frame can be read back", 0);
        }
    }

    say("\n  the layout to write: alpha in the %s byte\n",
        alpha_high ? "HIGH" : "LOW");

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
