/* Where does the source alpha come from, when the local is the constant register?
 *
 * `docs/research/win95-oracle-vs-card.md` ends on a measurement it could not
 * make from the host. The caption of the attract sequence comes out saturated
 * and opaque on the card where the oracle paints it pastel and translucent, and
 * everything else has been excluded by measurement rather than by argument: the
 * second pass is drawn (594 of them), the Glide entry points all resolve (21 of
 * 21), the blend setup and the combiner setup both read correctly out of the
 * source. What is left is the number itself - the source alpha reaching the
 * blender is about 255 where the combiner should be handing it 102 - and two
 * candidate causes that no reading of the code can separate:
 *
 *   a. the Voodoo's alpha unit does not take its local from the constant
 *      register, so `GR_COMBINE_LOCAL_CONSTANT` means something else there;
 *   b. `grConstantColorValue`'s alpha byte does not reach it.
 *
 * Both are answerable in one frame, and neither is answerable from the host.
 *
 * ## How the alpha is read back, given that the frame buffer has none
 *
 * The Voodoo stores 565: the alpha the blender used is not in the image. So the
 * blender is made to reveal it. Over a **black** destination and with
 * `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, the stored pixel is `source x alpha`, and
 * the source colour is measured first, in the same place, with blending off.
 * The alpha then divides out:
 *
 *     alpha = 255 x read / source
 *
 * That is one division against one measured reference, rather than a model of
 * the 1555 texel, the combiner's 255/256 truncation and the 565 store stacked
 * on top of each other. Each of those would have had to be right for the result
 * to mean anything, and the last time this project modelled a quantisation
 * chain instead of measuring its end, it read a shadow for a combiner.
 *
 * The green channel carries the reading: six bits in 565, where red and blue
 * have five.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/combiner.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;

static void say(const char *fmt, ...)
{
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

/* --- The Glide values, as `glide_backend.c` holds them ----------------------- *
 *
 * Written here as numbers on purpose, and named beside: these are the values
 * `combine_enum_probe.c` measured on this card, and this port has had a Glide
 * enumeration wrong from memory three times. A witness that redefined them from
 * memory would be measuring its own mistake. */
#define FN_LOCAL          1     /* result = local */
#define FN_SCALE_OTHER    3     /* result = other x factor */
#define FAC_LOCAL         1
#define FAC_ONE           8
#define LOCAL_ITERATED    0
#define LOCAL_CONSTANT    1
#define OTHER_ITERATED    0
#define OTHER_TEXTURE     1
#define TEXCOMB_DECAL     1     /* the texel, alpha included, unaltered */
/* `(other - local) * factor + local`, and the factor whose reading this witness
   is here to settle. Both are `glide_backend.c`'s, measured by
   `combine_enum_probe.c` -- 7 reads as BLEND on this card. */
#define FN_BLEND_OTHER            7
#define FAC_ONE_MINUS_LOCAL_ALPHA 0x0B

#define TW 32
#define TH 32
static unsigned short g_tex1555[TW * TH];   /* alpha 255, one bit, set */
static unsigned short g_tex4444[TW * TH];   /* alpha 8/15 = 136 */
static unsigned short g_texdark[TW * TH];   /* opaque, RGB 40,40,40 */

static unsigned g_px[640 * 480];

/* White, so that the source is as far from the black destination as the card
   can put it: the division that recovers the alpha is then at its most
   accurate, and a low alpha does not vanish into the quantisation. */
static void build_textures(void)
{
    int i;
    for (i = 0; i < TW * TH; i++) {
        g_tex1555[i] = (unsigned short)0xFFFFu;          /* a=1, 31,31,31 */
        g_tex4444[i] = (unsigned short)0x8FFFu;          /* a=8, 15,15,15 */
        /* 40 >> 3 = 5 in each of the three 1555 fields: the texel the card
           reads back is 41, and the sweep's expectations are computed from
           that rather than from the 40 written. */
        g_texdark[i] = (unsigned short)(0x8000u | (5u << 10) | (5u << 5) | 5u);
    }
}

static void quad(dkr_render_backend *bk, int w, int h, float vertex_alpha)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, (float)w, (float)w, 0.0f, (float)w, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, (float)h, 0.0f, (float)h, (float)h };
    const float ss[6] = { 0.0f, 128.0f, 128.0f, 0.0f, 128.0f, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, 128.0f, 0.0f, 128.0f, 128.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = 255.0f; v[i].g = 255.0f; v[i].b = 255.0f; v[i].a = vertex_alpha;
        v[i].oow = 1.0f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static int draw_and_read_red_local(dkr_render_backend *bk, dkr_render_state *st,
                                   dkr_texture_handle tex,
                                   const dkr_cc_setup *setup,
                                   unsigned constant, float vertex_alpha,
                                   float local_red);

/* The same draw at a chosen size, the texture mapped once over it: `side` pixels
   for `TW` texels. Full screen magnifies 20x, 32 is one texel per pixel, 16
   minifies by two - the only variable the sweeps above have not moved. */
static int draw_at_scale(dkr_render_backend *bk, dkr_render_state *st,
                         dkr_texture_handle tex, const dkr_cc_setup *setup,
                         unsigned constant, int side)
{
    dkr_render_vertex v[6];
    const float x0 = 320.0f - (float)side * 0.5f, x1 = x0 + (float)side;
    const float y0 = 240.0f - (float)side * 0.5f, y1 = y0 + (float)side;
    const float xs[6] = { 0, 0, 0, 0, 0, 0 };
    int rw = 0, rh = 0, k;
    const float px[6] = { x0, x1, x1, x0, x1, x0 };
    const float py[6] = { y0, y0, y1, y0, y1, y1 };
    const float ss[6] = { 0.0f, (float)TW, (float)TW, 0.0f, (float)TW, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, (float)TH, 0.0f, (float)TH, (float)TH };
    (void)xs;
    bk->begin_frame(bk->self, 0x000000);
    bk->set_state(bk->self, st);
    dkr_glide_backend_bind(tex);
    dkr_glide_backend_set_recipe(setup, constant);
    memset(v, 0, sizeof(v));
    for (k = 0; k < 6; k++) {
        v[k].x = px[k]; v[k].y = py[k];
        v[k].r = 0.0f; v[k].g = 255.0f; v[k].b = 255.0f; v[k].a = 255.0f;
        v[k].oow = 1.0f;
        v[k].tmu[0][DKR_TMU_SOW] = ss[k];
        v[k].tmu[0][DKR_TMU_TOW] = ts[k];
        v[k].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
    bk->present(bk->self);
    if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) <= 0) { return -1; }
    return (int)((g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)]
                  >> 16) & 0xFFu);
}

/* The same draw with a cyan iterated colour at full alpha, read on the red
   channel: that is where `(T - E) * f + E` puts the factor, undiluted. */
static int draw_and_read_red(dkr_render_backend *bk, dkr_render_state *st,
                             dkr_texture_handle tex, const dkr_cc_setup *setup,
                             unsigned constant, float vertex_alpha)
{
    return draw_and_read_red_local(bk, st, tex, setup, constant, vertex_alpha,
                                   0.0f);
}

static int draw_and_read_red_local(dkr_render_backend *bk, dkr_render_state *st,
                                   dkr_texture_handle tex,
                                   const dkr_cc_setup *setup,
                                   unsigned constant, float vertex_alpha,
                                   float local_red)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, 640.0f, 640.0f, 0.0f, 640.0f, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, 480.0f, 0.0f, 480.0f, 480.0f };
    const float ss[6] = { 0.0f, 128.0f, 128.0f, 0.0f, 128.0f, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, 128.0f, 0.0f, 128.0f, 128.0f };
    int rw = 0, rh = 0, k;
    bk->begin_frame(bk->self, 0x000000);
    bk->set_state(bk->self, st);
    dkr_glide_backend_bind(tex);
    dkr_glide_backend_set_recipe(setup, constant);
    memset(v, 0, sizeof(v));
    for (k = 0; k < 6; k++) {
        v[k].x = xs[k]; v[k].y = ys[k];
        v[k].r = local_red; v[k].g = 255.0f; v[k].b = 255.0f;
        v[k].a = vertex_alpha;
        v[k].oow = 1.0f;
        v[k].tmu[0][DKR_TMU_SOW] = ss[k];
        v[k].tmu[0][DKR_TMU_TOW] = ts[k];
        v[k].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
    bk->present(bk->self);
    if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) <= 0) { return -1; }
    return (int)((g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)]
                  >> 16) & 0xFFu);
}

/* One draw, one pixel, one green channel. */
static int draw_and_read(dkr_render_backend *bk, dkr_render_state *st,
                         dkr_texture_handle tex, const dkr_cc_setup *setup,
                         unsigned constant, float vertex_alpha)
{
    int rw = 0, rh = 0;
    unsigned c;
    bk->begin_frame(bk->self, 0x000000);
    bk->set_state(bk->self, st);
    dkr_glide_backend_bind(tex);
    dkr_glide_backend_set_recipe(setup, constant);
    quad(bk, 640, 480, vertex_alpha);
    bk->present(bk->self);
    if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) <= 0) { return -1; }
    c = g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
    return (int)((c >> 8) & 0xFFu);
}

/* The colour combiner is the same throughout: the texel, straight through. The
   measurement bears on the alpha side, and a colour side that computed anything
   would put its own error into the divisor. */
static void setup_alpha(dkr_cc_setup *r, int fn, int factor, int local,
                        int other, int uses_texture)
{
    memset(r, 0, sizeof(*r));
    r->cc_function = FN_SCALE_OTHER; r->cc_factor = FAC_ONE;
    r->cc_local = LOCAL_ITERATED;    r->cc_other = OTHER_TEXTURE;
    r->ac_function = (unsigned char)fn;    r->ac_factor = (unsigned char)factor;
    r->ac_local    = (unsigned char)local; r->ac_other  = (unsigned char)other;
    r->tc_function = TEXCOMB_DECAL;  r->tc_factor = 0;
    r->uses_texture = (unsigned char)uses_texture;
}

/* `prepass_draw_texel_alone`'s colour combiner, to the letter. The alpha side is
   parked on the iterated alpha: this section reads the colour. */
static void setup_blend_other(dkr_cc_setup *r)
{
    memset(r, 0, sizeof(*r));
    r->cc_function = FN_BLEND_OTHER; r->cc_factor = FAC_ONE_MINUS_LOCAL_ALPHA;
    r->cc_local = LOCAL_ITERATED;    r->cc_other = OTHER_TEXTURE;
    r->ac_function = FN_LOCAL;       r->ac_factor = FAC_ONE;
    r->ac_local = LOCAL_ITERATED;    r->ac_other = OTHER_ITERATED;
    r->tc_function = TEXCOMB_DECAL;  r->tc_factor = 0;
    r->uses_texture = 1;
}

static int alpha_from(int read, int source)
{
    if (source <= 0 || read < 0) { return -1; }
    return (read * 255 + source / 2) / source;
}

static const int SWEEP[6] = { 0, 51, 102, 153, 204, 255 };

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   desc;
    dkr_texture_handle tex1555, tex4444, texdark;
    dkr_cc_setup       r;
    const int W = 640, H = 480;
    int source = 0, i;
    int const_local[6], suspect_opaque[6], suspect_half[6], byte_pos[4];
    int factor_iter[6], factor_const[6], factor_texel = -1;
    int fails = 0;

    g_out = fopen("D:\\CONSTA.TXT", "w");
    say("the source alpha, when the local is the constant register\n\n");

    build_textures();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) {
        say("FAILED: the card will not open\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }

    memset(&desc, 0, sizeof(desc));
    desc.key = 0xA1FA01ull;
    desc.format = DKR_TEXFMT_ARGB1555;
    desc.width = TW; desc.height = TH;
    desc.pixels = g_tex1555; desc.size_bytes = sizeof(g_tex1555);
    tex1555 = bk.texture_upload(bk.self, &desc);

    desc.key = 0xA1FA02ull;
    desc.format = DKR_TEXFMT_ARGB4444;
    desc.pixels = g_tex4444; desc.size_bytes = sizeof(g_tex4444);
    tex4444 = bk.texture_upload(bk.self, &desc);

    desc.key = 0xA1FA03ull;
    desc.format = DKR_TEXFMT_ARGB1555;
    desc.pixels = g_texdark; desc.size_bytes = sizeof(g_texdark);
    texdark = bk.texture_upload(bk.self, &desc);

    if (!tex1555 || !tex4444 || !texdark) {
        say("FAILED: a texture will not upload (1555 %s, 4444 %s, dark %s)\n",
            tex1555 ? "ok" : "no", tex4444 ? "ok" : "no", texdark ? "ok" : "no");
        bk.close(bk.self);
        if (g_out) { fclose(g_out); }
        return 1;
    }

    memset(&st, 0, sizeof(st));
    st.depth = DKR_DEPTH_DISABLED;
    st.cull  = DKR_CULL_NONE;
    st.filter = DKR_FILTER_POINT;
    st.wrap_s = st.wrap_t = DKR_WRAP_CLAMP;

    /* --- The divisor, measured rather than modelled -------------------------- */
    st.blend = DKR_BLEND_OPAQUE;
    st.texture = tex1555;
    setup_alpha(&r, FN_LOCAL, FAC_ONE, LOCAL_ITERATED, OTHER_ITERATED, 1);
    source = draw_and_read(&bk, &st, tex1555, &r, 0xFFFFFFFFu, 255.0f);
    say("  the white texel, blending off: green = %d\n", source);
    say("  every alpha below is 255 x read / %d\n\n", source);
    if (source <= 32) {
        say("FAILED: the source is too dark to divide by - the texture or the "
            "colour combiner is not doing what this witness assumes.\n");
        bk.close(bk.self);
        if (g_out) { fclose(g_out); }
        return 1;
    }

    st.blend = DKR_BLEND_ALPHA;

    /* --- Control: an alpha the blender is known to receive -------------------- *
     *
     * The vertex alpha, straight through. If this does not track, nothing below
     * means anything, and the fault is in the reading rather than in the card. */
    say("-- control: alpha = the vertex's own, FUNCTION_LOCAL on the iterated\n");
    for (i = 0; i < 6; i++) {
        int got, a;
        setup_alpha(&r, FN_LOCAL, FAC_ONE, LOCAL_ITERATED, OTHER_ITERATED, 1);
        got = draw_and_read(&bk, &st, tex1555, &r, 0xFFFFFFFFu, (float)SWEEP[i]);
        a = alpha_from(got, source);
        say("   vertex alpha %3d -> read %3d -> alpha %3d %s\n",
            SWEEP[i], got, a,
            (a >= 0 && a > SWEEP[i] - 16 && a < SWEEP[i] + 16) ? "" : "<-- off");
        if (!(a >= 0 && a > SWEEP[i] - 16 && a < SWEEP[i] + 16)) { fails++; }
    }

    /* --- (b): does the constant's alpha byte reach the alpha unit at all? ----- *
     *
     * FUNCTION_LOCAL with LOCAL_CONSTANT asks for the constant and nothing else.
     * No factor, no other input, no texture in the alpha path: if the byte
     * arrives, this reads it back. */
    say("\n-- the constant register alone: FUNCTION_LOCAL, LOCAL_CONSTANT\n");
    for (i = 0; i < 6; i++) {
        unsigned k = (unsigned)SWEEP[i];
        int got;
        setup_alpha(&r, FN_LOCAL, FAC_ONE, LOCAL_CONSTANT, OTHER_ITERATED, 1);
        got = draw_and_read(&bk, &st, tex1555, &r,
                            (k << 24) | 0x00FFFFFFu, 255.0f);
        const_local[i] = alpha_from(got, source);
        say("   constant %08lX -> read %3d -> alpha %3d\n",
            (unsigned long)((k << 24) | 0x00FFFFFFu), got, const_local[i]);
    }

    /* --- (a): the setup the game actually programs ---------------------------- *
     *
     * `gl_set_state`'s blend-constant path, to the letter: SCALE_OTHER by
     * FACTOR_LOCAL, local the constant, other the texture. Intended:
     * `alpha = texel_alpha x constant_alpha / 255`. */
    say("\n-- the game's setup: SCALE_OTHER x FACTOR_LOCAL, "
        "LOCAL_CONSTANT, OTHER_TEXTURE\n");
    say("   texel alpha 255 (1555)\n");
    for (i = 0; i < 6; i++) {
        unsigned k = (unsigned)SWEEP[i];
        int got;
        setup_alpha(&r, FN_SCALE_OTHER, FAC_LOCAL, LOCAL_CONSTANT,
                    OTHER_TEXTURE, 1);
        got = draw_and_read(&bk, &st, tex1555, &r,
                            (k << 24) | 0x00FFFFFFu, 255.0f);
        suspect_opaque[i] = alpha_from(got, source);
        say("   constant alpha %3d -> read %3d -> alpha %3d (wanted %3d)\n",
            SWEEP[i], got, suspect_opaque[i], SWEEP[i]);
    }
    say("   texel alpha 136 (4444), which separates the product from the "
        "constant alone\n");
    for (i = 0; i < 6; i++) {
        unsigned k = (unsigned)SWEEP[i];
        int got, want = SWEEP[i] * 136 / 255;
        setup_alpha(&r, FN_SCALE_OTHER, FAC_LOCAL, LOCAL_CONSTANT,
                    OTHER_TEXTURE, 1);
        got = draw_and_read(&bk, &st, tex4444, &r,
                            (k << 24) | 0x00FFFFFFu, 255.0f);
        suspect_half[i] = alpha_from(got, source);
        say("   constant alpha %3d -> read %3d -> alpha %3d (product %3d, "
            "constant alone %3d)\n",
            SWEEP[i], got, suspect_half[i], want, SWEEP[i]);
    }

    /* --- Which byte of the constant drives the alpha unit --------------------- *
     *
     * Only asked because the answer decides what to change. The card was opened
     * with GR_COLORFORMAT_ARGB, so the alpha is the top byte; a register that
     * reads a different one would have made `grConstantColorValue` silently
     * carry the wrong channel, which is a mistake this port has already made
     * once in the other direction - the RDP word passed where ARGB was meant. */
    say("\n-- 102 in each byte position, FUNCTION_LOCAL on the constant\n");
    for (i = 0; i < 4; i++) {
        unsigned c = 102u << (i * 8);
        int got;
        setup_alpha(&r, FN_LOCAL, FAC_ONE, LOCAL_CONSTANT, OTHER_ITERATED, 1);
        got = draw_and_read(&bk, &st, tex1555, &r, c, 255.0f);
        byte_pos[i] = alpha_from(got, source);
        say("   constant %08lX (byte %d) -> read %3d -> alpha %3d %s\n",
            (unsigned long)c, i, got, byte_pos[i],
            (byte_pos[i] > 86 && byte_pos[i] < 118) ? "<-- this one" : "");
    }

    /* --- Whose alpha does factor 0x0B read? ---------------------------------- *
     *
     * `prepass_draw_texel_alone` draws the caption with
     * `BLEND_OTHER / ONE_MINUS_LOCAL_ALPHA`, carrying the environment in the
     * vertex so that the factor fetches the lerp constant from the iterated
     * alpha. The file says in as many words that the explanation does not hold -
     * "what this setting actually computes on a font atlas is not known" - and
     * asks whoever comes next to start from that.
     *
     * Here is the question in one channel. `other` is a white texel, `local` the
     * iterated cyan: the result is `(T - E) * f + E`, whose **red** is `255 x f`
     * and nothing else. Read the red, and the factor reads itself.
     *
     * Three candidates, and the sweeps separate them: the iterated alpha, the
     * constant's alpha, and the texel's. Blending is off - this measures the
     * colour combiner, not the blender. */
    st.blend = DKR_BLEND_OPAQUE;
    say("\n-- factor 0x0B in BLEND_OTHER: red = 255 x factor\n");
    say("   iterated alpha swept, constant alpha 255, texel alpha 255\n");
    for (i = 0; i < 6; i++) {
        int red;
        setup_blend_other(&r);
        red = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu,
                                (float)SWEEP[i]);
        factor_iter[i] = red;
        say("   iterated alpha %3d -> red %3d -> factor %3d "
            "(1 - iterated = %3d)\n",
            SWEEP[i], red, red, 255 - SWEEP[i]);
    }
    say("   constant alpha swept, iterated alpha 255, texel alpha 255\n");
    for (i = 0; i < 6; i++) {
        int red;
        setup_blend_other(&r);
        red = draw_and_read_red(&bk, &st, tex1555, &r,
                                ((unsigned)SWEEP[i] << 24) | 0x0000FFFFu,
                                255.0f);
        factor_const[i] = red;
        say("   constant alpha %3d -> red %3d -> factor %3d "
            "(1 - constant = %3d)\n",
            SWEEP[i], red, red, 255 - SWEEP[i]);
    }
    say("   texel alpha 136 (4444), iterated and constant alpha 255\n");
    {
        int red;
        setup_blend_other(&r);
        red = draw_and_read_red(&bk, &st, tex4444, &r, 0xFF00FFFFu, 255.0f);
        factor_texel = red;
        say("   texel alpha 136 -> red %3d -> factor %3d "
            "(1 - texel = 119)\n", red, red);
    }

    /* --- What every factor does when `other` is the texture ------------------ *
     *
     * The sweep `combine_enum_probe.c` ran drove `other` from the **constant**
     * register, and `glide_backend.c` says in as many words that its reading does
     * not carry over: "With `other` driven from the texture, which is how it is
     * used below, it evidently does not". That is the configuration the game
     * draws its text in, and it has never been swept.
     *
     * Here it is, for both functions this port uses over a texture. With a white
     * texel as `other` and a cyan iterated colour as `local`, the **red** channel
     * is `255 x factor` under either function - `SCALE_OTHER` gives `T x f` and
     * `BLEND_OTHER` gives `(T - E) x f + E`, and E has no red - so one column
     * compares them directly.
     *
     * What it is for: 640 pixels of the copyright screen, where `SCALE_OTHER`
     * with `FACTOR_ONE` renders 140 and `BLEND_OTHER` with `0x0B` renders the
     * oracle's 255. Two settings that were supposed to compute nearly the same
     * thing. */
    say("\n-- the factor sweep nobody ran: other = TEXTURE, red = 255 x factor\n");
    say("   %-6s %-14s %-14s\n", "factor", "SCALE_OTHER", "BLEND_OTHER");
    for (i = 0; i <= 15; i++) {
        int red_scale, red_blend;
        setup_blend_other(&r);
        r.cc_function = FN_SCALE_OTHER; r.cc_factor = (unsigned char)i;
        red_scale = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu, 255.0f);
        setup_blend_other(&r);
        r.cc_factor = (unsigned char)i;
        red_blend = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu, 255.0f);
        say("   0x%02X   %-14d %-14d %s\n", i, red_scale, red_blend,
            (i == 8) ? "<- called ONE" : (i == 0x0B) ? "<- the one in use" : "");
    }

    /* --- Is 0x0B reading the local's *colour* rather than its alpha? --------- *
     *
     * The sweep above reads 0x0B as very nearly one, with a cyan local whose red
     * is zero. The copyright screen reads it as very nearly **zero**, with a
     * white local - the arithmetic of the scene leaves no room for anything else:
     * the card writes `E` where `SCALE_OTHER` writes `T`.
     *
     * One factor answers both: `ONE_MINUS_LOCAL`, per channel, rather than
     * `ONE_MINUS_LOCAL_ALPHA`. Glide's canonical table puts those at 0x9 and 0xB,
     * and this card's enumeration has been found shifted before - the
     * texture-combine values were, by one.
     *
     * The two candidates give curves that cannot be confused. `other` is the
     * white texel, `local` the iterated colour whose **red** is swept, and the
     * result's red is `(255 - E) * f + E`:
     *
     *     ONE_MINUS_LOCAL        f = 1 - E/255  ->  255, 214, 194, 194, 214, 255
     *     ONE_MINUS_LOCAL_ALPHA  f = 0 (alpha is 255)  ->    0,  51, 102, 153, 204, 255
     *
     * `SCALE_OTHER / 0x08` is carried alongside as the control: it ignores the
     * local entirely, so its column should not move at all. */
    say("\n-- 0x0B against the local's red, texel white, local alpha 255\n");
    say("   %-8s %-12s %-12s %-10s %s\n", "local R", "BLEND 0x0B", "SCALE 0x08",
        "1-LOCAL", "1-LOCAL_A");
    for (i = 0; i < 6; i++) {
        int red_blend, red_scale;
        const int want_local = ((255 - SWEEP[i]) * (255 - SWEEP[i])) / 255
                               + SWEEP[i];
        setup_blend_other(&r);
        red_blend = draw_and_read_red_local(&bk, &st, tex1555, &r, 0xFF00FFFFu,
                                            255.0f, (float)SWEEP[i]);
        setup_blend_other(&r);
        r.cc_function = FN_SCALE_OTHER; r.cc_factor = FAC_ONE;
        red_scale = draw_and_read_red_local(&bk, &st, tex1555, &r, 0xFF00FFFFu,
                                            255.0f, (float)SWEEP[i]);
        say("   %-8d %-12d %-12d %-10d %d\n",
            SWEEP[i], red_blend, red_scale, want_local, SWEEP[i]);
    }

    /* --- Does the colour's factor follow the *alpha* unit's local? ----------- *
     *
     * Everything the factor could read has been swept and it moved for none of
     * them, while the copyright screen says it reads nearly zero where this quad
     * says nearly one. One difference is left between the two, and it is not in
     * the colour combiner at all: `prepass_draw_texel_alone` programs the alpha
     * unit as `SCALE_OTHER / FACTOR_ONE / LOCAL_ITERATED / OTHER_TEXTURE` - the
     * texel's alpha - where the sweeps above parked it on `FUNCTION_LOCAL` over
     * the iterated alpha.
     *
     * On the Voodoo both calls write the same path register, so a factor named
     * after "the local's alpha" may well be reading the **alpha unit's** local
     * rather than the colour unit's. If that is it, the column below moves. */
    say("\n-- the same colour setup under two alpha setups, texel white\n");
    say("   %-34s %s\n", "alpha unit", "red");
    {
        int red;
        setup_blend_other(&r);
        red = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu, 255.0f);
        say("   %-34s %d\n", "LOCAL / ONE / ITERATED (swept above)", red);

        setup_blend_other(&r);
        r.ac_function = FN_SCALE_OTHER; r.ac_factor = FAC_ONE;
        r.ac_local = LOCAL_ITERATED;    r.ac_other = OTHER_TEXTURE;
        red = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu, 255.0f);
        say("   %-34s %d\n", "SCALE_OTHER / ONE / TEXTURE (the game's)", red);

        setup_blend_other(&r);
        r.ac_function = FN_SCALE_OTHER; r.ac_factor = FAC_LOCAL;
        r.ac_local = LOCAL_CONSTANT;    r.ac_other = OTHER_TEXTURE;
        red = draw_and_read_red(&bk, &st, tex1555, &r, 0x0000FFFFu, 255.0f);
        say("   %-34s %d\n", "FACTOR_LOCAL / CONSTANT, alpha 0", red);
    }

    /* --- The one variable left: how much the texture is magnified ------------ *
     *
     * Every sweep above draws a 32x32 texture over the whole screen - a
     * magnification of twenty. DKR's text is drawn at very nearly one texel per
     * pixel. If the factor is one of Glide's LOD-derived ones, that is exactly
     * the difference the sweeps could not see, and the column below moves with
     * the scale rather than with any colour. */
    say("\n-- the same setups at three sampling scales, texel white\n");
    say("   %-22s %-12s %s\n", "quad", "BLEND 0x0B", "SCALE 0x08");
    {
        const int sides[3] = { 480, 32, 16 };
        const char *names[3] = { "480 px (magnified 15x)", "32 px (one to one)",
                                 "16 px (minified 2x)" };
        int k;
        for (k = 0; k < 3; k++) {
            int red_blend, red_scale;
            setup_blend_other(&r);
            red_blend = draw_at_scale(&bk, &st, tex1555, &r, 0xFF00FFFFu,
                                      sides[k]);
            setup_blend_other(&r);
            r.cc_function = FN_SCALE_OTHER; r.cc_factor = FAC_ONE;
            red_scale = draw_at_scale(&bk, &st, tex1555, &r, 0xFF00FFFFu,
                                      sides[k]);
            say("   %-22s %-12d %d\n", names[k], red_blend, red_scale);
        }
    }

    /* --- And the last difference in the state: the filter --------------------- *
     *
     * Every sweep above samples with `DKR_FILTER_POINT`. DKR's text does not:
     * the decoder asks for bilinear, and that is the only field of the state
     * left between this quad and the pass whose factor reads differently. */
    say("\n-- the same setups under bilinear sampling, texel white\n");
    st.filter = DKR_FILTER_BILINEAR;
    {
        int red_blend, red_scale;
        setup_blend_other(&r);
        red_blend = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu, 255.0f);
        setup_blend_other(&r);
        r.cc_function = FN_SCALE_OTHER; r.cc_factor = FAC_ONE;
        red_scale = draw_and_read_red(&bk, &st, tex1555, &r, 0xFF00FFFFu, 255.0f);
        say("   %-22s %-12d %d\n", "bilinear", red_blend, red_scale);
    }
    st.filter = DKR_FILTER_POINT;

    /* --- The same sweep on a texel that is NOT opaque ------------------------ *
     *
     * Every reading above used a texel at alpha 255, and on such a texel `ONE`
     * and `TEXTURE_ALPHA` are the same number. That is exactly the confusion the
     * copyright screen exposes: `SCALE_OTHER / 0x08` renders 140 there, and the
     * scene's arithmetic says the texel is white, so 0x08 delivered 0.55 - which
     * is a glyph's alpha, not one.
     *
     * The 4444 texel at alpha 136 separates them in one column: a factor that
     * reads the texture's alpha drops to about 136, `ONE` stays at 255. */
    say("\n-- the same sweep, texel alpha 136 (4444): ONE and TEXTURE_ALPHA part\n");
    say("   %-6s %-14s %-14s %s\n", "factor", "SCALE_OTHER", "BLEND_OTHER",
        "reading");
    for (i = 0; i <= 15; i++) {
        int red_scale, red_blend;
        setup_blend_other(&r);
        r.cc_function = FN_SCALE_OTHER; r.cc_factor = (unsigned char)i;
        red_scale = draw_and_read_red(&bk, &st, tex4444, &r, 0xFF00FFFFu, 255.0f);
        setup_blend_other(&r);
        r.cc_factor = (unsigned char)i;
        red_blend = draw_and_read_red(&bk, &st, tex4444, &r, 0xFF00FFFFu, 255.0f);
        say("   0x%02X   %-14d %-14d %s\n", i, red_scale, red_blend,
            (red_scale > 120 && red_scale < 152) ? "<- the texel's alpha" :
            (red_scale > 235) ? "one" : "");
    }

    /* --- And the function itself, which no sweep here could see -------------- *
     *
     * Every reading above puts a `local` whose red is zero against a white
     * texel, so that the red channel reads `255 x factor`. That choice makes
     * `SCALE_OTHER` and `BLEND_OTHER` **identical on that channel** - `f x other`
     * and `(other - 0) x f + 0` are the same number - and the sweeps therefore
     * say nothing at all about the function. The one reading on record comes
     * from `combine_enum_probe.c`, with `other` driven from the *constant*
     * register, and this file's own warning is that such a reading does not
     * carry over to a texture.
     *
     * So: a dark texel against a mid-grey local, where the four candidates land
     * far apart. `other` = 41 (what the card reads back from 40 in 1555),
     * `local` = 100, factor 0x0B = 247/255:
     *
     *     LOCAL                       100
     *     SCALE_OTHER   f x T          39
     *     BLEND         (T-L) f + L     43
     *     SCALE_OTHER_ADD_LOCAL  fT+L  139
     *
     * Forty units between the nearest pair. The copyright screen needs an
     * answer this sweep can give: `BLEND` cannot put white where the texel is
     * dark, and `ADD_LOCAL` can. */
    say("\n-- the function, other = TEXTURE (41,41,41), local = (100,100,100)\n");
    say("   candidates on red: LOCAL 100, SCALE 39, BLEND 43, ADD 139\n");
    say("   %-4s %-8s %-8s %s\n", "fn", "0x0B", "0x08", "reading");
    for (i = 0; i <= 15; i++) {
        int red_b, red_8;
        const char *reading;
        setup_blend_other(&r);
        r.cc_function = (unsigned char)i;
        red_b = draw_and_read_red_local(&bk, &st, texdark, &r, 0xFF00FFFFu,
                                        255.0f, 100.0f);
        setup_blend_other(&r);
        r.cc_function = (unsigned char)i; r.cc_factor = FAC_ONE;
        red_8 = draw_and_read_red_local(&bk, &st, texdark, &r, 0xFF00FFFFu,
                                        255.0f, 100.0f);
        reading = (red_b > 120 && red_b < 160) ? "<- f x other + local"
                : (red_b >  85 && red_b < 115) ? "local"
                : (red_b >  30 && red_b <  50) ? "f x other, or the lerp"
                : (red_b == 0)                 ? "zero" : "";
        say("   %-4d %-8d %-8d %s\n", i, red_b, red_8, reading);
    }
    say("   the lerp and the scale are 4 apart here and the pair below\n"
        "   separates them: local 200 puts BLEND at 161 and SCALE still at 39.\n");
    {
        int red_b;
        setup_blend_other(&r);
        r.cc_function = FN_BLEND_OTHER;
        red_b = draw_and_read_red_local(&bk, &st, texdark, &r, 0xFF00FFFFu,
                                        255.0f, 200.0f);
        say("   function 7, local 200 -> %d  (LOCAL 200, SCALE 39, "
            "BLEND 161, ADD 239)\n", red_b);
    }

    /* --- Does the colour unit's *function* move the alpha? ------------------- *
     *
     * Everything measured says the two settings of `prepass_draw_texel_alone`
     * differ by eight levels of the texel, and the copyright screen says they
     * differ by a hundred and fifteen. The colour side cannot produce that. The
     * one thing not yet tried is whether programming the colour unit moves the
     * *alpha* the blender receives - the two calls write the same path register
     * on this hardware, and a field that belongs to one could be read by the
     * other.
     *
     * Same reading as the first section: blending on, over black, so the stored
     * pixel is `source x alpha`, divided by the source measured with blending
     * off. The alpha unit is held at the game's setup throughout; only the
     * colour unit changes. */
    say("\n-- the alpha under each colour function, texel alpha 136\n");
    {
        int src, i2;
        const int fns[2] = { FN_SCALE_OTHER, FN_BLEND_OTHER };
        const int facs[2] = { FAC_ONE, FAC_ONE_MINUS_LOCAL_ALPHA };
        const char *names[2] = { "SCALE_OTHER / ONE (the switch on)",
                                 "BLEND_OTHER / 0x0B (the default)" };
        st.blend = DKR_BLEND_OPAQUE;
        setup_alpha(&r, FN_LOCAL, FAC_ONE, LOCAL_ITERATED, OTHER_ITERATED, 1);
        src = draw_and_read(&bk, &st, tex4444, &r, 0xFFFFFFFFu, 255.0f);
        say("   the 4444 texel with blending off: green = %d\n", src);
        st.blend = DKR_BLEND_ALPHA;
        for (i2 = 0; i2 < 2; i2++) {
            int got, a;
            memset(&r, 0, sizeof(r));
            r.cc_function = (unsigned char)fns[i2];
            r.cc_factor   = (unsigned char)facs[i2];
            r.cc_local = LOCAL_ITERATED; r.cc_other = OTHER_TEXTURE;
            /* the game's alpha side, unchanged between the two rows */
            r.ac_function = FN_SCALE_OTHER; r.ac_factor = FAC_ONE;
            r.ac_local = LOCAL_ITERATED;    r.ac_other = OTHER_TEXTURE;
            r.tc_function = TEXCOMB_DECAL;  r.tc_factor = 0;
            r.uses_texture = 1;
            got = draw_and_read(&bk, &st, tex4444, &r, 0xFFFFFFFFu, 255.0f);
            a = alpha_from(got, src);
            say("   %-34s read %3d -> alpha %3d\n", names[i2], got, a);
        }
        st.blend = DKR_BLEND_OPAQUE;
    }

    /* --- The same pass, drawn by the engine rather than by hand -------------- *
     *
     * Everything above programs the card directly. The copyright screen does not:
     * it goes through `gl_set_state`, which for recipe 20 programs
     * `apply_combine`, then `apply_texture_modes`, then `apply_blend`, and only
     * then does `gl_draw_triangles` find the shape and call
     * `prepass_draw_texel_alone` over the top of it. If the two settings differ
     * by a hundred and fifteen levels there and by eight here, the difference is
     * in what that path leaves programmed - so the path is what this section
     * exercises.
     *
     * The state is the copyright text's, read off `replay --probe 369,421`:
     * recipe 20, TEXTURE_CONSTANT, constant and environment both white at full
     * alpha, `alpha_scale` 255, blending on. The texel is the dark one, standing
     * in for a glyph. */
    say("\n-- the same two settings, drawn through gl_set_state (recipe 20)\n");
    {
        dkr_render_state gs_state;
        int k;
        const char *names[2] = { "default  (BLEND_OTHER / 0x0B)",
                                 "switched (SCALE_OTHER / ONE)" };
        for (k = 0; k < 2; k++) {
            int rw = 0, rh = 0;
            unsigned c;
            dkr_glide_backend_texel_factor_one(k);
            memset(&gs_state, 0, sizeof(gs_state));
            gs_state.combine = DKR_COMBINE_TEXTURE_CONSTANT;
            gs_state.constant_color = 0xFFFFFFFFu;
            gs_state.env_color = 0xFFFFFFFFu;
            gs_state.prim_color = 0xFFFFFFFFu;
            gs_state.blend = DKR_BLEND_ALPHA;
            gs_state.depth = DKR_DEPTH_DISABLED;
            gs_state.cull = DKR_CULL_NONE;
            gs_state.filter = DKR_FILTER_POINT;
            gs_state.wrap_s = gs_state.wrap_t = DKR_WRAP_CLAMP;
            gs_state.alpha_scale = 255;
            gs_state.recipe = 20;
            gs_state.texture = texdark;
            bk.begin_frame(bk.self, 0x000000);
            /* Invalidated between the two, or the second `set_state` sees an
               identical block and returns without programming anything - which
               would make this section compare a setting with itself. */
            bk.invalidate(bk.self);
            bk.set_state(bk.self, &gs_state);
            quad(&bk, W, H, 255.0f);
            bk.present(bk.self);
            c = 0;
            if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) > 0) {
                c = g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
            }
            say("   %-32s 0x%06lX\n", names[k],
                (unsigned long)(c & 0x00FFFFFFu));
        }
        dkr_glide_backend_texel_factor_one(0);
    }

    /* --- State, or the draw? One row each ------------------------------------ *
     *
     * The section above reproduces the scene outside it: the same two settings,
     * 247 against 41, where programming the card by hand gives 33 against 41.
     * The two differ in two ways at once - what `gl_set_state` leaves programmed
     * before the pass, and the fact that the pass itself tints the vertices and
     * draws them. These rows separate the two.
     *
     * Row A lets the engine program everything, then re-issues by hand exactly
     * what the pass programs, and draws the quad directly. If the engine's
     * reading survives that, it is in the state; if it falls back to the hand
     * reading, it is in the draw. */
    say("\n-- state or draw: the engine's state, then a hand draw over it\n");
    {
        dkr_render_state gs_state;
        dkr_cc_setup hand;
        int k;
        for (k = 0; k < 2; k++) {
            int rw = 0, rh = 0;
            unsigned c;
            memset(&gs_state, 0, sizeof(gs_state));
            gs_state.combine = DKR_COMBINE_TEXTURE_CONSTANT;
            gs_state.constant_color = 0xFFFFFFFFu;
            gs_state.env_color = 0xFFFFFFFFu;
            gs_state.prim_color = 0xFFFFFFFFu;
            gs_state.blend = DKR_BLEND_ALPHA;
            gs_state.depth = DKR_DEPTH_DISABLED;
            gs_state.cull = DKR_CULL_NONE;
            gs_state.filter = DKR_FILTER_POINT;
            gs_state.wrap_s = gs_state.wrap_t = DKR_WRAP_CLAMP;
            gs_state.alpha_scale = 255;
            gs_state.recipe = 0;          /* no prepass: the ordinary draw */
            gs_state.texture = texdark;
            bk.begin_frame(bk.self, 0x000000);
            bk.invalidate(bk.self);
            bk.set_state(bk.self, &gs_state);
            /* exactly what `prepass_draw_texel_alone` programs, by hand */
            memset(&hand, 0, sizeof(hand));
            hand.cc_function = (unsigned char)(k ? FN_SCALE_OTHER
                                                 : FN_BLEND_OTHER);
            hand.cc_factor   = (unsigned char)(k ? FAC_ONE
                                                 : FAC_ONE_MINUS_LOCAL_ALPHA);
            hand.cc_local = LOCAL_ITERATED; hand.cc_other = OTHER_TEXTURE;
            hand.ac_function = FN_SCALE_OTHER; hand.ac_factor = FAC_ONE;
            hand.ac_local = LOCAL_ITERATED;    hand.ac_other = OTHER_TEXTURE;
            hand.tc_function = TEXCOMB_DECAL;  hand.tc_factor = 0;
            hand.uses_texture = 1;
            dkr_glide_backend_bind(texdark);
            dkr_glide_backend_set_recipe(&hand, 0xFFFFFFFFu);
            quad(&bk, W, H, 255.0f);      /* white vertices, like the tint */
            bk.present(bk.self);
            c = 0;
            if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) > 0) {
                c = g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
            }
            say("   %-32s 0x%06lX\n",
                k ? "SCALE_OTHER / ONE, by hand" : "BLEND_OTHER / 0x0B, by hand",
                (unsigned long)(c & 0x00FFFFFFu));
        }
    }

    /* --- Which field of the state, then ------------------------------------- *
     *
     * The draw is exonerated: the same hand programming reads 247 after the
     * engine's state and 33 after the sweeps' own. Everything the recipe
     * programs is re-issued in both, so the difference is in a field
     * `set_recipe` does not touch. Blending is the one that changed between the
     * two - the sweeps read the combiner with it off, the engine's state has it
     * on - and the rest of the block is equal. One row each way. */
    say("\n-- the engine's state with blending on and off\n");
    {
        dkr_render_state gs_state;
        dkr_cc_setup hand;
        int k, bl;
        for (bl = 0; bl < 2; bl++) {
            for (k = 0; k < 2; k++) {
                int rw = 0, rh = 0;
                unsigned c;
                memset(&gs_state, 0, sizeof(gs_state));
                gs_state.combine = DKR_COMBINE_TEXTURE_CONSTANT;
                gs_state.constant_color = 0xFFFFFFFFu;
                gs_state.env_color = 0xFFFFFFFFu;
                gs_state.prim_color = 0xFFFFFFFFu;
                gs_state.blend = bl ? DKR_BLEND_ALPHA : DKR_BLEND_OPAQUE;
                gs_state.depth = DKR_DEPTH_DISABLED;
                gs_state.cull = DKR_CULL_NONE;
                gs_state.filter = DKR_FILTER_POINT;
                gs_state.wrap_s = gs_state.wrap_t = DKR_WRAP_CLAMP;
                gs_state.alpha_scale = 255;
                gs_state.recipe = 0;
                gs_state.texture = texdark;
                bk.begin_frame(bk.self, 0x000000);
                bk.invalidate(bk.self);
                bk.set_state(bk.self, &gs_state);
                memset(&hand, 0, sizeof(hand));
                hand.cc_function = (unsigned char)(k ? FN_SCALE_OTHER
                                                     : FN_BLEND_OTHER);
                hand.cc_factor   = (unsigned char)(k ? FAC_ONE
                                                     : FAC_ONE_MINUS_LOCAL_ALPHA);
                hand.cc_local = LOCAL_ITERATED; hand.cc_other = OTHER_TEXTURE;
                hand.ac_function = FN_SCALE_OTHER; hand.ac_factor = FAC_ONE;
                hand.ac_local = LOCAL_ITERATED;    hand.ac_other = OTHER_TEXTURE;
                hand.tc_function = TEXCOMB_DECAL;  hand.tc_factor = 0;
                hand.uses_texture = 1;
                dkr_glide_backend_bind(texdark);
                dkr_glide_backend_set_recipe(&hand, 0xFFFFFFFFu);
                quad(&bk, W, H, 255.0f);
                bk.present(bk.self);
                c = 0;
                if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) > 0) {
                    c = g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
                }
                say("   blend %-7s %-22s 0x%06lX\n",
                    bl ? "alpha" : "opaque",
                    k ? "SCALE_OTHER / ONE" : "BLEND_OTHER / 0x0B",
                    (unsigned long)(c & 0x00FFFFFFu));
            }
        }
    }

    /* --- What exactly does blending change ----------------------------------- *
     *
     * Blending off, `BLEND_OTHER / 0x0B` delivers the texel; blending on, it
     * delivers the local. Two questions follow, and one run answers both.
     *
     * Additive blending uses `ONE / ONE` and never fetches a source alpha. If
     * the factor comes back to the texel there, what matters is the *source
     * alpha factor* in the blender, not blending as such.
     *
     * And under alpha blending the iterated alpha is swept again. If the factor
     * now tracks `1 - iterated alpha`, then 0x0B is the `ONE_MINUS_LOCAL_ALPHA`
     * it is named after all along, and its local alpha only reaches the colour
     * unit when the blender asks the alpha path for something. */
    say("\n-- what blending changes: texel 41, local white\n");
    {
        dkr_render_state gs_state;
        dkr_cc_setup hand;
        const int blends[3] = { DKR_BLEND_OPAQUE, DKR_BLEND_ALPHA,
                                DKR_BLEND_ADDITIVE };
        const char *bn[3] = { "opaque", "alpha", "additive" };
        const int alphas[3] = { 0, 128, 255 };
        int bi, ai;
        for (bi = 0; bi < 3; bi++) {
            for (ai = 0; ai < 3; ai++) {
                int rw = 0, rh = 0;
                unsigned c;
                memset(&gs_state, 0, sizeof(gs_state));
                gs_state.combine = DKR_COMBINE_TEXTURE_CONSTANT;
                gs_state.constant_color = 0xFFFFFFFFu;
                gs_state.env_color = 0xFFFFFFFFu;
                gs_state.prim_color = 0xFFFFFFFFu;
                gs_state.blend = (dkr_blend_mode)blends[bi];
                gs_state.depth = DKR_DEPTH_DISABLED;
                gs_state.cull = DKR_CULL_NONE;
                gs_state.filter = DKR_FILTER_POINT;
                gs_state.wrap_s = gs_state.wrap_t = DKR_WRAP_CLAMP;
                gs_state.alpha_scale = 255;
                gs_state.recipe = 0;
                gs_state.texture = texdark;
                bk.begin_frame(bk.self, 0x000000);
                bk.invalidate(bk.self);
                bk.set_state(bk.self, &gs_state);
                memset(&hand, 0, sizeof(hand));
                hand.cc_function = FN_BLEND_OTHER;
                hand.cc_factor   = FAC_ONE_MINUS_LOCAL_ALPHA;
                hand.cc_local = LOCAL_ITERATED; hand.cc_other = OTHER_TEXTURE;
                /* the alpha the blender will use is the iterated one here, so
                   that sweeping the vertex sweeps both at once */
                hand.ac_function = FN_LOCAL;    hand.ac_factor = FAC_ONE;
                hand.ac_local = LOCAL_ITERATED; hand.ac_other = OTHER_ITERATED;
                hand.tc_function = TEXCOMB_DECAL; hand.tc_factor = 0;
                hand.uses_texture = 1;
                dkr_glide_backend_bind(texdark);
                dkr_glide_backend_set_recipe(&hand, 0xFFFFFFFFu);
                quad(&bk, W, H, (float)alphas[ai]);
                bk.present(bk.self);
                c = 0;
                if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) > 0) {
                    c = g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
                }
                say("   blend %-9s iterated alpha %3d -> 0x%06lX\n",
                    bn[bi], alphas[ai], (unsigned long)(c & 0x00FFFFFFu));
            }
        }
        say("   (over black: with alpha blending the stored pixel is "
            "source x alpha,\n"
            "    so read the ratio between the rows, not the absolute value)\n");
    }

    /* --- The iterated alpha, or the alpha unit's output? --------------------- *
     *
     * The sweep above moves both at once: the alpha combiner was parked on the
     * iterated alpha, so sweeping the vertex swept the factor's input whichever
     * of the two it reads. The distinction decides whether this pass's colour
     * and its alpha can be fixed separately - and on 14 September an attempt to
     * give it `alpha_scale` through the alpha unit made the image worse for
     * reasons nobody could name.
     *
     * Here the vertex alpha is held at 255 and the alpha unit is made to deliver
     * something else - the constant's alpha, swept. If the colour follows, the
     * factor reads the unit's output, and touching the alpha moves the colour. */
    say("\n-- vertex alpha held at 255, the alpha unit's output swept\n");
    {
        dkr_render_state gs_state;
        dkr_cc_setup hand;
        int k2;
        for (k2 = 0; k2 < 6; k2++) {
            int rw = 0, rh = 0;
            unsigned c;
            memset(&gs_state, 0, sizeof(gs_state));
            gs_state.combine = DKR_COMBINE_TEXTURE_CONSTANT;
            gs_state.constant_color = 0xFFFFFFFFu;
            gs_state.env_color = 0xFFFFFFFFu;
            gs_state.prim_color = 0xFFFFFFFFu;
            gs_state.blend = DKR_BLEND_ADDITIVE;   /* ONE/ONE: the stored pixel
                                                      is the combiner's own */
            gs_state.depth = DKR_DEPTH_DISABLED;
            gs_state.cull = DKR_CULL_NONE;
            gs_state.filter = DKR_FILTER_POINT;
            gs_state.wrap_s = gs_state.wrap_t = DKR_WRAP_CLAMP;
            gs_state.alpha_scale = 255;
            gs_state.recipe = 0;
            gs_state.texture = texdark;
            bk.begin_frame(bk.self, 0x000000);
            bk.invalidate(bk.self);
            bk.set_state(bk.self, &gs_state);
            memset(&hand, 0, sizeof(hand));
            hand.cc_function = FN_BLEND_OTHER;
            hand.cc_factor   = FAC_ONE_MINUS_LOCAL_ALPHA;
            hand.cc_local = LOCAL_ITERATED; hand.cc_other = OTHER_TEXTURE;
            /* alpha = the constant's, and nothing of the vertex's */
            hand.ac_function = FN_LOCAL;     hand.ac_factor = FAC_ONE;
            hand.ac_local = LOCAL_CONSTANT;  hand.ac_other = OTHER_TEXTURE;
            hand.tc_function = TEXCOMB_DECAL; hand.tc_factor = 0;
            hand.uses_texture = 1;
            dkr_glide_backend_bind(texdark);
            dkr_glide_backend_set_recipe(&hand,
                ((unsigned)SWEEP[k2] << 24) | 0x00FFFFFFu);
            quad(&bk, W, H, 255.0f);
            bk.present(bk.self);
            c = 0;
            if (dkr_glide_read_framebuffer(g_px, 640 * 480, &rw, &rh) > 0) {
                c = g_px[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
            }
            say("   alpha unit delivers %3d -> 0x%06lX   (texel 41, local 255:\n"
                "        reading the unit gives %3d, reading the vertex gives "
                "247)\n",
                SWEEP[k2], (unsigned long)(c & 0x00FFFFFFu),
                ((41 - 255) * (255 - SWEEP[k2])) / 255 + 255);
        }
    }

    /* --- What the numbers say ------------------------------------------------- */
    say("\n-- reading\n");
    {
        int tracks_const = 1, tracks_suspect = 1, flat = 1;
        for (i = 0; i < 6; i++) {
            if (!(const_local[i] > SWEEP[i] - 20 && const_local[i] < SWEEP[i] + 20)) {
                tracks_const = 0;
            }
            if (!(suspect_opaque[i] > SWEEP[i] - 20 &&
                  suspect_opaque[i] < SWEEP[i] + 20)) {
                tracks_suspect = 0;
            }
            if (i > 0 && const_local[i] < const_local[0] - 20) { flat = 0; }
            if (i > 0 && const_local[i] > const_local[0] + 20) { flat = 0; }
        }
        if (tracks_const && tracks_suspect) {
            say("   the constant's alpha reaches the alpha unit, and the game's\n"
                "   setup delivers it. The caption's opacity is therefore not in\n"
                "   this setup - and on 14 September 2026 it was found one level\n"
                "   up: `prepass_draw_texel_alone` replaces the ordinary draw for\n"
                "   that configuration and never programs the scale at all.\n");
        } else if (tracks_const && !tracks_suspect) {
            say("   the byte arrives - FUNCTION_LOCAL reads it back - but the\n"
                "   game's setup does not deliver it. FACTOR_LOCAL on the alpha\n"
                "   unit is not the constant's alpha. That is cause (a), and it\n"
                "   is a translation to change, not a register.\n");
        } else if (flat) {
            say("   the alpha does not move with the constant at all: the byte\n"
                "   does not reach the alpha unit. That is cause (b), and the\n"
                "   byte position table above says whether another byte does.\n");
        } else {
            say("   neither candidate as stated: the constant moves the alpha\n"
                "   but not to the value asked for. The tables above are the\n"
                "   measurement; read them before theorising.\n");
        }
    }
    {
        int moved = 0;
        for (i = 1; i < 6; i++) {
            if (factor_iter[i] < factor_iter[0] - 8 ||
                factor_iter[i] > factor_iter[0] + 8) { moved = 1; }
            if (factor_const[i] < factor_const[0] - 8 ||
                factor_const[i] > factor_const[0] + 8) { moved = 1; }
        }
        if (factor_texel < factor_iter[0] - 8 ||
            factor_texel > factor_iter[0] + 8) { moved = 1; }
        if (!moved) {
            say("\n   factor 0x0B read %d out of 255 whichever alpha was swept -\n"
                "   **with the blender off**, which is how those sweeps run. It\n"
                "   is not inert: the blended sections below show it reading the\n"
                "   alpha unit's output exactly, and degenerating to one only\n"
                "   when nothing asks the alpha path for anything.\n",
                factor_iter[0]);
        } else {
            say("\n   factor 0x0B moved with one of the three alphas: the table\n"
                "   above says which.\n");
        }
        say("\n   Read the last three sections together. Under blending,\n"
            "   ONE_MINUS_LOCAL_ALPHA is exactly that, and its local alpha is\n"
            "   the **alpha combiner's output** - not the iterated alpha, which\n"
            "   the vertex-alpha sweep alone could not separate from it.\n"
            "   `prepass_draw_texel_alone` therefore does compute the RDP's\n"
            "   lerp, with k = whatever the alpha unit delivers, and its blend\n"
            "   factor is that same value. One register serves both: scaling the\n"
            "   alpha to carry `alpha_scale` scales the lerp with it.\n");
    }

    bk.close(bk.self);
    say("\n%d control failure(s)\n", fails);
    if (g_out) { fclose(g_out); }
    return fails != 0;
}
