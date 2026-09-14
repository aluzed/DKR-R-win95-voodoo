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

/* The same draw with a cyan iterated colour at full alpha, read on the red
   channel: that is where `(T - E) * f + E` puts the factor, undiluted. */
static int draw_and_read_red(dkr_render_backend *bk, dkr_render_state *st,
                             dkr_texture_handle tex, const dkr_cc_setup *setup,
                             unsigned constant, float vertex_alpha)
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
        v[k].r = 0.0f; v[k].g = 255.0f; v[k].b = 255.0f; v[k].a = vertex_alpha;
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
    dkr_texture_handle tex1555, tex4444;
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

    if (!tex1555 || !tex4444) {
        say("FAILED: a texture will not upload (1555 %s, 4444 %s)\n",
            tex1555 ? "ok" : "no", tex4444 ? "ok" : "no");
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
            say("\n   factor 0x0B fetched nothing: %d out of 255 whichever alpha\n"
                "   was swept. `prepass_draw_texel_alone` therefore draws very\n"
                "   nearly the texel alone - the RDP's cycle at k = 0 - and not\n"
                "   the lerp it says it draws.\n", factor_iter[0]);
        } else {
            say("\n   factor 0x0B moved with one of the three alphas: the table\n"
                "   above says which, and the vertex route can be written from\n"
                "   it.\n");
        }
    }

    bk.close(bk.self);
    say("\n%d control failure(s)\n", fails);
    if (g_out) { fclose(g_out); }
    return fails != 0;
}
