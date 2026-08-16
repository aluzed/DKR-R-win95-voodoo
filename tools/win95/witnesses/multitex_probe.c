/* E05-S04 - the chaining of the two TMUs, measured rather than assumed.
 *
 * `grTexCombine` chains TMU 1's output into TMU 0. Its functions form a closed
 * list - `DECAL`, `OTHER`, `ADD`, `MULTIPLY`, an interpolation - whose
 * enumeration values are, like everything else about Glide on this machine,
 * written from memory.
 *
 * This project has already paid twice for assuming such values: the w-buffer's
 * comparison direction, then the whole `BLENDI` family of the colour combiner.
 * So we sweep.
 *
 * ## Two textures made to be told apart
 *
 * TMU 1 carries pure red, TMU 0 pure blue. Each candidate function then produces
 * a colour that names it unambiguously:
 *
 *     DECAL     -> blue    (TMU 0 alone)
 *     OTHER     -> red     (TMU 1 alone)
 *     ADD       -> magenta (both)
 *     MULTIPLY  -> black   (red x blue = 0)
 *     lerp      -> a blend, according to the factor
 *
 * Textures that resembled each other would make the sweep mute: it is the same
 * principle as E05-S02's four-colour checkerboard, and for the same reason - a
 * trial must tell faults apart, not merely pass.
 *
 * ## And the coordinates' consistency
 *
 * Each TMU has its own coordinate set in the Glide vertex. A mistake here shifts
 * the two layers relative to each other, which **looks like a combiner defect**
 * and diagnoses very badly. The witness checks it separately, on two textures
 * whose patterns must overlay exactly.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/combiner.h"
#include "render/tmu.h"

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
static unsigned short g_red[TW * TH];
static unsigned short g_blue[TW * TH];
static unsigned short g_bands0[TW * TH];
static unsigned short g_bands1[TW * TH];
static unsigned g_px[640 * 480];

static void build_textures(void)
{
    int x, y;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            g_red[y * TW + x] = (unsigned short)(0x8000u | (31u << 10));
            g_blue [y * TW + x] = (unsigned short)(0x8000u | 31u);
            /* Two complementary patterns: the left half filled on one, the right
               half on the other. Overlaid, they cover everything; shifted, they
               leave a band. */
            g_bands0[y * TW + x] = (unsigned short)
                (0x8000u | ((x < TW / 2) ? (31u << 10) : 0u));
            g_bands1[y * TW + x] = (unsigned short)
                (0x8000u | ((x >= TW / 2) ? 31u : 0u));
        }
    }
}

static void quad(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)w, (float)w, 0, (float)w, 0 };
    const float ys[6] = { 0, 0, (float)h, 0, (float)h, (float)h };
    const float ss[6] = { 0, 256.0f, 256.0f, 0, 256.0f, 0 };
    const float ts[6] = { 0, 0, 256.0f, 0, 256.0f, 256.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
        v[i].oow = 1.0f;
        /* **Both units receive the same coordinates**, and that is what is
           checked further down: each TMU has its own set in the Glide vertex, and
           filling them separately is the chance to forget one. */
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
        v[i].tmu[1][DKR_TMU_SOW] = ss[i];
        v[i].tmu[1][DKR_TMU_TOW] = ts[i];
        v[i].tmu[1][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned read_px(int x, int y, int w) { return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu; }

static int colour_is(unsigned c, int r, int g, int b)
{
    const int cr = (int)((c >> 16) & 0xFF), cg = (int)((c >> 8) & 0xFF),
              cb = (int)(c & 0xFF), s = 100;
    return ((cr > s) == (r != 0)) && ((cg > s) == (g != 0)) && ((cb > s) == (b != 0));
}

static dkr_texture_handle upload(dkr_render_backend *bk, const void *px,
                                 unsigned long long key, int tmu)
{
    dkr_texture_desc d;
    memset(&d, 0, sizeof(d));
    d.key = key; d.format = DKR_TEXFMT_RGBA5551;
    d.width = TW; d.height = TH;
    d.pixels = px; d.size_bytes = (size_t)(TW * TH * 2);
    d.tmu = tmu;
    return bk->texture_upload(bk->self, &d);
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_glide_hardware hw;
    dkr_texture_handle h_red, h_blue, h_b0, h_b1;
    const int W = 640, H = 480;
    int fn, rw = 0, rh = 0;

    g_out = fopen("D:\\MULTITEX.TXT", "w");
    say("chaining of the two TMUs, measured\n\n");

    if (dkr_glide_detect(&hw) != DKR_GLIDE_OK) {
        say("FAILED: no board\n"); return 1;
    }
    say("  TMUs detected: %d\n", hw.tmu_count);
    check("the board really has two", hw.tmu_count >= 2);

    build_textures();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAILED to open\n"); return 1; }
    bk.begin_frame(bk.self, 0x000000);

    h_red = upload(&bk, g_red, 0x1001ull, 1);   /* TMU 1 */
    h_blue  = upload(&bk, g_blue,  0x1002ull, 0);   /* TMU 0 */
    check("TMU 1's texture uploads", h_red != 0);
    check("and TMU 0's as well",      h_blue  != 0);

    /* Both allocators must have served, each its own. If the two textures landed
       on the same unit, everything that follows would measure something other
       than the chaining. */
    {
        const dkr_tmu *t0 = dkr_glide_backend_tmu(0);
        const dkr_tmu *t1 = dkr_glide_backend_tmu(1);
        say("  TMU0 holds %u B, TMU1 %u B\n",
            t0 ? dkr_tmu_used(t0) : 0u, t1 ? dkr_tmu_used(t1) : 0u);
        check("each unit carries exactly one texture",
              t0 && t1 && dkr_tmu_used(t0) == 8192u && dkr_tmu_used(t1) == 8192u);
    }

    memset(&st, 0, sizeof(st));
    /* **Without this mode, the whole sweep comes out white.** The colour combiner
       ignores the TMUs' output as long as it is not asked for the texel: the
       first version of this witness left `combine` at SHADE, and the twelve
       functions rendered the vertex colour - a uniform white screen that said
       nothing. The coordinate-consistency check "passed" there too, without
       establishing anything, for want of being able to tell the background from
       the result. */
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend = DKR_BLEND_OPAQUE; st.depth = DKR_DEPTH_DISABLED;
    st.cull = DKR_CULL_NONE; st.filter = DKR_FILTER_POINT;
    st.wrap_s = st.wrap_t = DKR_WRAP_CLAMP;
    st.texture = h_blue; st.texture1 = h_red;

    say("\n-- sweep of grTexCombine's functions on TMU 0 --\n");
    say("  TMU1 = pure red, TMU0 = pure blue\n");
    say("%-4s %-8s %s\n", "fn", "read", "reading");

    for (fn = 0; fn <= 11; fn++) {
        unsigned c;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        dkr_glide_backend_chain(h_blue, h_red, (unsigned char)fn, 8);
        quad(&bk, W, H);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
        c = read_px(rw / 2, rh / 2, rw);
        say("%-4d %06X   %s\n", fn, c,
            colour_is(c, 0, 0, 1) ? "DECAL: TMU 0 alone" :
            colour_is(c, 1, 0, 0) ? "OTHER: TMU 1 alone" :
            colour_is(c, 1, 0, 1) ? "ADD: both" :
            (c == 0)           ? "MULTIPLY, or zero" : "a blend");
    }

    /* --- The coordinates' consistency ---------------------------------------- *
     *
     * Two complementary patterns: the left half filled on one, the right half on
     * the other. Added together, they must cover **the whole** screen. A shift
     * between the two units' coordinate sets would leave a black band, or make a
     * doubled one appear.
     *
     * This is the check the ticket asks for, and it is worth separating: a shift
     * is indistinguishable from a combiner defect, and one searches a long time
     * on the wrong side. */
    h_b0 = upload(&bk, g_bands0, 0x2001ull, 0);
    h_b1 = upload(&bk, g_bands1, 0x2002ull, 1);
    if (h_b0 && h_b1) {
        int blacks = 0, i, got;
        st.texture = h_b0; st.texture1 = h_b1;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        /* ADD: each half comes from a different unit.
         *
         * **The value is 4, not 3.** The sweep above established it: DECAL is 1,
         * OTHER is 3, ADD is 4 - shifted by one notch from what had been written
         * from memory. This very check therefore asked for OTHER believing it
         * asked for ADD, and saw only one layer. That is the third time in this
         * port that an assumed Glide enumeration value turns out wrong, and the
         * third time only measurement says so. */
        dkr_glide_backend_chain(h_b0, h_b1, 4, 8);
        quad(&bk, W, H);
        bk.present(bk.self);
        got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
        for (i = 0; i < got; i++) {
            if ((g_px[i] & 0x00FFFFFFu) == 0) { blacks++; }
        }
        say("\n-- coordinate consistency between the two units --\n");
        say("  black pixels: %d out of %d\n", blacks, got);
        say("  left quarter 0x%06X, right quarter 0x%06X\n",
            read_px(rw / 4, rh / 2, rw), read_px(rw * 3 / 4, rh / 2, rw));
        /* An edge column may be missing; a band may not. */
        check("the two layers overlay without leaving a band",
              got > 0 && blacks < got / 100);
        /* **And the check that stops a uniform screen from passing.** The left
           half comes from one unit, the right from the other: they must therefore
           differ. Without it, a uniform white - the exact symptom of this
           witness's first version - would satisfy the previous check. */
        check("and each half really comes from a different unit",
              read_px(rw / 4, rh / 2, rw) != read_px(rw * 3 / 4, rh / 2, rw));
    }

    /* --- The single-TMU fallback, and the gain -------------------------------- *
     *
     * Two additive passes give the same result as a chained ADD: the first lays
     * down TMU 0's layer, the second adds TMU 1's. The equality is not obvious and
     * must be **verified by difference**, not assumed - that is what the ticket
     * asks for. */
    if (h_b0 && h_b1) {
        static unsigned single_pass[640 * 480];
        int i, got, differing = 0;
        unsigned long t_single, t_double;

        /* One pass, two TMUs. */
        dkr_glide_backend_force_single_tmu(0);
        st.texture = h_b0; st.texture1 = h_b1;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        dkr_glide_backend_chain(h_b0, h_b1, 4, 8);
        quad(&bk, W, H);
        bk.present(bk.self);
        got = dkr_glide_read_framebuffer(single_pass, W * H, &rw, &rh);

        /* Two passes, a single TMU forced. The second pass adds. */
        dkr_glide_backend_force_single_tmu(1);
        bk.begin_frame(bk.self, 0x000000);
        st.texture = h_b0; st.texture1 = 0;
        bk.set_state(bk.self, &st);
        quad(&bk, W, H);
        {
            dkr_render_state st2 = st;
            st2.blend = DKR_BLEND_ADDITIVE;
            st2.texture = h_b1;
            bk.set_state(bk.self, &st2);
            /* The second layer's texture lives on TMU 1; in fallback, it is TMU
               0 that must sample it. So we upload it again at its address - the
               duplication is the fallback's price, and precisely why it is not
               the default path. */
            {
                dkr_texture_handle h = upload(&bk, g_bands1, 0x2003ull, 0);
                if (h) { st2.texture = h; bk.set_state(bk.self, &st2); }
            }
            quad(&bk, W, H);
        }
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0 && got > 0) {
            for (i = 0; i < got; i++) {
                if ((single_pass[i] & 0x00FFFFFFu) != (g_px[i] & 0x00FFFFFFu)) {
                    differing++;
                }
            }
        }
        say("\n-- single-TMU fallback --\n");
        say("  pixels differing between one pass and two: %d out of %d\n",
            differing, got);
        check("the fallback produces the same image, verified by difference",
              got > 0 && differing * 100 < got);

        /* The gain. A hundred frames on each side: that is the measurement the
           ticket asks for, and it puts a number on what the fallback really costs
           rather than assuming it double. */
        dkr_glide_backend_force_single_tmu(0);
        t_single = GetTickCount();
        for (i = 0; i < 100; i++) {
            bk.begin_frame(bk.self, 0x000000);
            st.texture = h_b0; st.texture1 = h_b1;
            bk.set_state(bk.self, &st);
            dkr_glide_backend_chain(h_b0, h_b1, 4, 8);
            quad(&bk, W, H);
            bk.present(bk.self);
        }
        t_single = GetTickCount() - t_single;

        dkr_glide_backend_force_single_tmu(1);
        t_double = GetTickCount();
        for (i = 0; i < 100; i++) {
            dkr_render_state st2;
            bk.begin_frame(bk.self, 0x000000);
            st.texture = h_b0; st.texture1 = 0;
            bk.set_state(bk.self, &st);
            quad(&bk, W, H);
            st2 = st; st2.blend = DKR_BLEND_ADDITIVE; st2.texture = h_b1;
            bk.set_state(bk.self, &st2);
            quad(&bk, W, H);
            bk.present(bk.self);
        }
        t_double = GetTickCount() - t_double;
        dkr_glide_backend_force_single_tmu(0);

        say("\n-- the gain --\n");
        say("  100 frames in one pass   : %lu ms\n", t_single);
        say("  100 frames in two passes : %lu ms\n", t_double);
        if (t_single) {
            say("  fallback overhead        : %lu %%\n",
                (t_double * 100u) / t_single - 100u);
        }
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
