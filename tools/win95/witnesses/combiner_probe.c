/* E05-S03 - each configuration's deviation, measured on the card.
 *
 * The ticket warns: "the temptation will be to handle the 33 configurations one
 * by one until it looks about right, without measuring. The visual deviation then
 * accumulates silently, and the final rendering is diffusely wrong without any
 * error being attributable." This witness is the answer to that warning.
 *
 * ## The oracle is the formula, not a second program
 *
 * The ticket plans to compare against the reference rasteriser. Here we compare
 * directly against `(a - b) * c + d`, evaluated by `dkr_combiner_eval_all`. That
 * is not a shortcut but a strengthening: comparing two programs only moves the
 * question of which one is right, whereas a short formula on constant inputs has
 * an answer one can work out by hand.
 *
 * The rasteriser remains the oracle for geometry and interpolation, where there
 * is no closed form. Here there is no need for it.
 *
 * ## Why constant colours everywhere
 *
 * The scene is made so that **no interpolation intervenes**: a full-screen quad,
 * every vertex the same colour, a flat texture. Every pixel read therefore has an
 * exact analytical value, and the measured deviation can only come from the
 * combiner. Mixing interpolation into this measurement would make any deviation
 * unattributable - precisely what the ticket warns against.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/combiner.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

/* --- The inputs, all constant and chosen to be distinct ---------------------- *
 *
 * Close values would merge under 565 quantisation; extreme values (0 and 255)
 * would hide scale errors, a wrong factor not showing when it multiplies zero. So
 * we take middling, well-separated values. */
#define TW 64
#define TH 64
static unsigned short g_texture[TW * TH];

static const float TEXEL[4]  = { 200.0f, 100.0f,  50.0f, 128.0f };
static const float SHADE[4]  = { 128.0f, 192.0f,  64.0f, 255.0f };
static const float PRIM[4]   = { 255.0f,  32.0f,  96.0f, 200.0f };
static const float ENV[4]    = {  16.0f, 224.0f, 160.0f,  64.0f };

static void build_texture(void)
{
    /* A single flat colour: the measurement bears on the combiner, not on
       sampling. The texture witness (E05-S02) has already established that the
       read is correct. */
    const unsigned short c = (unsigned short)
        (0x8000u | (((unsigned)TEXEL[0] >> 3) << 10) |
                   (((unsigned)TEXEL[1] >> 3) <<  5) |
                    ((unsigned)TEXEL[2] >> 3));
    int i;
    for (i = 0; i < TW * TH; i++) { g_texture[i] = c; }
}

/* The texture is in 1555: the texel the card reads is not exactly the one we
   meant to write. The oracle must see **what the card sees**, without which we
   would measure the texture's quantisation and not the combiner. */
static void quantised_texel(float out[4])
{
    int i;
    for (i = 0; i < 3; i++) {
        const unsigned q = ((unsigned)TEXEL[i]) >> 3;
        out[i] = (float)((q << 3) | (q >> 2));
    }
    out[3] = 255.0f;   /* one alpha bit, set to one */
}

static void draw_quad(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, (float)w, (float)w, 0.0f, (float)w, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, (float)h, 0.0f, (float)h, (float)h };
    const float ss[6] = { 0.0f, 256.0f, 256.0f, 0.0f, 256.0f, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, 256.0f, 0.0f, 256.0f, 256.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = SHADE[0]; v[i].g = SHADE[1]; v[i].b = SHADE[2]; v[i].a = SHADE[3];
        v[i].oow = 1.0f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned g_pixels[640 * 480];

static unsigned pack(const float c[4])
{
    unsigned r = (unsigned)(c[0] + 0.5f), g = (unsigned)(c[1] + 0.5f),
             b = (unsigned)(c[2] + 0.5f), a = (unsigned)(c[3] + 0.5f);
    if (r > 255) { r = 255; } if (g > 255) { g = 255; }
    if (b > 255) { b = 255; } if (a > 255) { a = 255; }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Glide's combiner multiplies in 0..255 and **truncates**.
 *
 * Measured: a constant of 32 comes back as 28, one of 96 as 90. The factor is
 * 255/256 and the truncation costs one quantisation step. This is not a
 * translation error, it is the hardware; not modelling it would make every
 * configuration carry a systematic deviation of eight or nine units, which would
 * mask the real deviations by drowning them in a background noise. */
static unsigned glide_truncate(unsigned c)
{
    unsigned out = 0;
    int i;
    for (i = 0; i < 3; i++) {
        const unsigned v = (c >> (i * 8)) & 0xFF;
        out |= ((v * 255u) / 256u) << (i * 8);
    }
    return out;
}

/* Quantises like the card's frame buffer, so as not to count the 565 conversion
   as a deviation of the combiner. */
static unsigned q565(unsigned c)
{
    const unsigned r = ((c >> 16) & 0xFF) >> 3;
    const unsigned g = ((c >> 8)  & 0xFF) >> 2;
    const unsigned b = ( c        & 0xFF) >> 3;
    return (((r << 3) | (r >> 2)) << 16) |
           (((g << 2) | (g >> 4)) << 8)  |
            ((b << 3) | (b >> 2));
}

static int deviation(unsigned a, unsigned b)
{
    int worst = 0, i;
    for (i = 0; i < 3; i++) {
        int d = (int)((a >> (i * 8)) & 0xFF) - (int)((b >> (i * 8)) & 0xFF);
        if (d < 0) { d = -d; }
        if (d > worst) { worst = d; }
    }
    return worst;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_texture_desc   desc;
    dkr_texture_handle handle;
    const int W = 640, H = 480;
    int i, n, rw = 0, rh = 0;
    int worst_exact = 0;
    const char *worst_name = "(none)";

    g_out = fopen("D:\\COMBINER.TXT", "w");
    say("deviation of each combiner configuration, measured on the card\n\n");

    build_texture();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) {
        say("FAILED: the card will not open\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    bk.begin_frame(bk.self, 0x000000);

    memset(&desc, 0, sizeof(desc));
    desc.key = 0xC0FFEEull;
    desc.format = DKR_TEXFMT_RGBA5551;
    desc.width = TW; desc.height = TH;
    desc.pixels = g_texture; desc.size_bytes = sizeof(g_texture);
    handle = bk.texture_upload(bk.self, &desc);
    if (!handle) {
        say("FAILED: the texture will not upload\n");
        bk.close(bk.self);
        if (g_out) { fclose(g_out); }
        return 1;
    }

    {
        dkr_render_state st;
        memset(&st, 0, sizeof(st));
        st.blend = DKR_BLEND_OPAQUE;
        st.depth = DKR_DEPTH_DISABLED;
        st.cull  = DKR_CULL_NONE;
        st.filter = DKR_FILTER_POINT;
        st.wrap_s = st.wrap_t = DKR_WRAP_CLAMP;
        st.texture = handle;
        bk.set_state(bk.self, &st);
    }

    n = dkr_cc_table_count();
    say("%-34s %-12s %8s %8s %s\n",
        "configuration", "category", "expected", "got", "deviation");

    for (i = 0; i < n; i++) {
        const dkr_cc_entry *e = dkr_cc_table_at(i);
        dkr_combiner_inputs in;
        dkr_combiner comb;
        float expected[4];
        unsigned constant, ca, cb;
        int d;

        memset(&in, 0, sizeof(in));
        quantised_texel(in.texel0);
        memcpy(in.texel1,      TEXEL, sizeof(in.texel1));
        memcpy(in.primitive,   PRIM,  sizeof(in.primitive));
        memcpy(in.shade,       SHADE, sizeof(in.shade));
        memcpy(in.environment, ENV,   sizeof(in.environment));

        memset(&comb, 0, sizeof(comb));
        comb.rgb[0]   = e->rgb[0];   comb.rgb[1]   = e->rgb[1];
        comb.alpha[0] = e->alpha[0]; comb.alpha[1] = e->alpha[1];
        dkr_combiner_eval_all(&comb, e->cycle, &in, expected);

        /* The constant register receives what the table decided. That is where the
           wall shows: a configuration marked `BOTH` cannot be served, and its
           deviation will say so. */
        constant = (e->constant == DKR_CONST_PRIMITIVE) ? pack(PRIM)
                 : (e->constant == DKR_CONST_ENVIRONMENT) ? pack(ENV)
                 : 0xFFFFFFFFu;

        dkr_glide_backend_bind(handle);
        dkr_glide_backend_set_recipe(&e->setup, constant);

        bk.begin_frame(bk.self, 0x000000);
        dkr_glide_backend_bind(handle);
        dkr_glide_backend_set_recipe(&e->setup, constant);
        draw_quad(&bk, W, H);
        bk.present(bk.self);

        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) <= 0) {
            say("  %-32s cannot be read back\n", e->name);
            continue;
        }
        ca = q565(glide_truncate(pack(expected) & 0x00FFFFFFu));
        cb = g_pixels[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)] & 0x00FFFFFFu;
        d  = deviation(ca, cb);

        say("%-34s %-12s   %06X   %06X %5d %s\n",
            e->name, dkr_cc_category_text(e->category), ca, cb, d,
            (e->category == DKR_CC_EXACT && d > 8) ? "<-- EXACT BUT WRONG" : "");

        /* **The check that counts.** A configuration declared exact must be so:
           beyond the quantisation, it has been misclassified and the table lies.
           The `multipass` and `approximate` categories announce a deviation on the
           contrary - measuring it is their reason for being, and it is reported
           without being counted as a failure. */
        if (e->category == DKR_CC_EXACT && d > worst_exact) {
            worst_exact = d;
            worst_name = e->name;
        }
    }

    say("\n  worst deviation among the configurations declared exact: %d (%s)\n",
        worst_exact, worst_name);
    if (worst_exact > 8) {
        say("  FAILED: a configuration declared exact is not\n");
        g_fails++;
    } else {
        say("  ok: every exact configuration is exact, "
            "to within the quantisation\n");
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
