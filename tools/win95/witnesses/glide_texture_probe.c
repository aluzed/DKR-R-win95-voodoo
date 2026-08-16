/* Does the texture really reach the card, and in the right place?
 *
 * `grTexDownloadMipMap` returns no error code. Neither does `grTexSource`. The
 * whole texture chain - allocation, transfer, binding, combiner - is therefore
 * **silent end to end**: it can only be checked by looking at the image. Reading
 * the buffer back (`win95-glide-bringup.md`) makes that check possible, and it is
 * the only one there is.
 *
 * ## The trial is built to tell faults apart, not to pass
 *
 * We draw a full-screen quad carrying a 4x4 checkerboard of colours that are
 * **all different and chosen on purpose**. Each identifies a cell, hence a range
 * of texture coordinates. Reading four points checks, in one go:
 *
 *   - that the texture arrived (otherwise: white, or noise);
 *   - that it is read **the right way round** - swapping s and t is invisible on
 *     a symmetric checkerboard and gets the whole game wrong;
 *   - that the aspect ratio is right - a wrong LOD stretches the texture, and the
 *     four points' colours shift together;
 *   - that the combiner really takes the texel and not the vertex colour.
 *
 * A black-and-white checkerboard would have passed all these faults without
 * flagging one.
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

/* The checkerboard. ARGB 1555: one alpha bit, five per component. */
#define TW 64
#define TH 64
static unsigned short g_texture[TW * TH];

/* Sixteen distinct colours, one per cell. The four we will read are at the
   corners, and are deliberately far apart from one another: red, green, blue,
   yellow. A crooked read would swap them in a perfectly legible way. */
static unsigned short cell_color(int cx, int cy)
{
    static const unsigned short CORNERS[4] = {
        0x8000 | (31 << 10),                 /* red    : top-left corner    */
        0x8000 | (31 <<  5),                 /* green  : top-right corner   */
        0x8000 | (31),                       /* blue   : bottom-left corner */
        0x8000 | (31 << 10) | (31 << 5)      /* yellow : bottom-right corner */
    };
    if (cx == 0 && cy == 0) { return CORNERS[0]; }
    if (cx == 3 && cy == 0) { return CORNERS[1]; }
    if (cx == 0 && cy == 3) { return CORNERS[2]; }
    if (cx == 3 && cy == 3) { return CORNERS[3]; }
    /* The inner cells carry stepped greys: they play no part in the verdict but
       make the image legible if one looks at it. */
    {
        const unsigned short g = (unsigned short)(4 + (cx + cy * 4) % 24);
        return (unsigned short)(0x8000 | (g << 10) | (g << 5) | g);
    }
}

static void build_texture(void)
{
    int x, y;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            g_texture[y * TW + x] = cell_color(x / (TW / 4), y / (TH / 4));
        }
    }
}

/* A full-screen quad, s and t covering exactly [0, 64].
 *
 * **Glide wants `s/w` and `t/w`, and its scale is in texels, not in units.** With
 * `oow = 1`, `s` therefore runs from 0 to 64 and not from 0 to 1. Getting that
 * wrong does not crash: the texture repeats 64 times or occupies a single texel,
 * which looks like a coordinate defect in the decoder. */
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

static unsigned g_pixels[640 * 480];

static unsigned read_at(int x, int y, int w)
{
    return g_pixels[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

/* The card works in 565; the texture is in 1555. A "full" red there is 31/31,
   which becomes 255 again after replication. We are generous with the tolerance:
   what is tested is the colour's identity, not its precision. */
static int dominant(unsigned c, int r, int g, int b)
{
    const int cr = (int)((c >> 16) & 0xFF);
    const int cg = (int)((c >> 8) & 0xFF);
    const int cb = (int)(c & 0xFF);
    const int threshold = 100;
    return ((cr > threshold) == (r != 0)) && ((cg > threshold) == (g != 0)) &&
           ((cb > threshold) == (b != 0));
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   desc;
    dkr_texture_handle handle;
    int rw = 0, rh = 0;
    const int W = 640, H = 480;

    g_out = fopen("D:\\GLTEX.TXT", "w");
    say("does the texture reach the card, and the right way round?\n\n");

    build_texture();

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) {
        say("FAILED: the card will not open\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    bk.begin_frame(bk.self, 0x000000);

    memset(&desc, 0, sizeof(desc));
    desc.key        = 0x1234567890ABCDEFull;
    desc.format     = DKR_TEXFMT_RGBA5551;
    desc.width      = TW;
    desc.height     = TH;
    desc.pixels     = g_texture;
    desc.size_bytes = sizeof(g_texture);

    handle = bk.texture_upload(bk.self, &desc);
    check("the texture gets a handle", handle != 0);
    say("  handle returned: %u\n", (unsigned)handle);

    /* The same key must return the same handle without downloading again: that is
       the whole point of the cache, and forgetting it would send the texture over
       the bus once per triangle. */
    check("the same key returns the same handle",
          bk.texture_upload(bk.self, &desc) == handle);

    {
        const dkr_tmu *t = dkr_glide_backend_tmu(0);
        if (t) {
            char line[160];
            dkr_tmu_format_status(t, line, sizeof(line));
            say("  %s\n", line);
            check("a single download for two requests",
                  t->stats.downloads == 1 && t->stats.hits == 1);
            check("the occupancy matches a 64x64 texture in 16 bits",
                  dkr_tmu_used(t) == 8192u);
        } else {
            check("the TMU's state is readable", 0);
        }
    }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;   /* point: bilinear would blend the cells */
    st.wrap_s  = DKR_WRAP_CLAMP;
    st.wrap_t  = DKR_WRAP_CLAMP;
    st.texture = handle;
    bk.set_state(bk.self, &st);

    /* --- At what scale does Glide expect s and t? ---------------------------- *
     *
     * This witness's first version laid `s` from 0 to 64 for a 64-texel texture,
     * and the whole screen came out red - that is, cell (0,0) everywhere. The
     * texture had duly arrived, but was sampled over a fraction of itself.
     *
     * Rather than pick between the possible conventions - real texels, space
     * normalised over 256, space over 255 - we try them all and look. The right
     * scale is the one that puts the four colours at the four corners; the others
     * mix them in a way that says which way round we got it wrong. */
    {
        static const float SCALES[] = { 64.0f, 128.0f, 255.0f, 256.0f, 512.0f };
        const int n = (int)(sizeof(SCALES) / sizeof(SCALES[0]));
        int i, right = -1;

        say("\n-- the scale of the texture coordinates --\n");
        for (i = 0; i < n; i++) {
            unsigned tl, tr, bl, br;
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            draw_quad(&bk, W, H, SCALES[i]);
            bk.present(bk.self);
            if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) <= 0) {
                continue;
            }
            tl = read_at(rw / 8,     rh / 8,     rw);
            tr = read_at(rw * 7 / 8, rh / 8,     rw);
            bl = read_at(rw / 8,     rh * 7 / 8, rw);
            br = read_at(rw * 7 / 8, rh * 7 / 8, rw);
            say("  s,t over %6.1f : TL 0x%06X  TR 0x%06X  BL 0x%06X  BR 0x%06X %s\n",
                (double)SCALES[i], tl, tr, bl, br,
                (dominant(tl,1,0,0) && dominant(tr,0,1,0) &&
                 dominant(bl,0,0,1) && dominant(br,1,1,0)) ? "<-- the four corners" : "");
            if (dominant(tl,1,0,0) && dominant(tr,0,1,0) &&
                dominant(bl,0,0,1) && dominant(br,1,1,0)) {
                right = i;
            }
        }
        check("one scale puts the four colours at the four corners", right >= 0);
        if (right >= 0) {
            say("\n  scale retained: %g for a texture of %d texels\n",
                (double)SCALES[right], TW);
            say("  that is a factor of %g\n", (double)SCALES[right] / (double)TW);
        }
        /* **The sweep served to find; the assertion must bear on the contract.**
           Leaving the check at "some scale works" would accept any future value,
           including one that no longer matched what the chain produces. It is
           `DKR_TEXCOORD_SCALE` that `clip.c` uses, so it is what must be put to
           the test. */
        {
            unsigned tl, tr, bl, br;
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            draw_quad(&bk, W, H, DKR_TEXCOORD_SCALE);
            bk.present(bk.self);
            if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
                tl = read_at(rw / 8,     rh / 8,     rw);
                tr = read_at(rw * 7 / 8, rh / 8,     rw);
                bl = read_at(rw / 8,     rh * 7 / 8, rw);
                br = read_at(rw * 7 / 8, rh * 7 / 8, rw);
                check("at DKR_TEXCOORD_SCALE, the top-left corner is red",
                      dominant(tl, 1, 0, 0));
                check("the top-right corner is green: s grows rightwards",
                      dominant(tr, 0, 1, 0));
                check("the bottom-left corner is blue: t grows downwards",
                      dominant(bl, 0, 0, 1));
                check("the bottom-right corner is yellow: neither s nor t is flipped",
                      dominant(br, 1, 1, 0));
            }
        }
    }

    /* And the negative control, without which the previous ones prove nothing:
       in vertex-colour mode, the texture must **not** appear. If it appeared all
       the same, the TMU stays bound from one frame to the next and the four checks
       above were measuring inherited state. */
    st.combine = DKR_COMBINE_SHADE;
    st.texture = 0;
    bk.begin_frame(bk.self, 0x000000);
    bk.set_state(bk.self, &st);
    draw_quad(&bk, W, H, 64.0f);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
        const unsigned tl = read_at(rw / 8, rh / 8, rw);
        const unsigned tr = read_at(rw * 7 / 8, rh / 8, rw);
        say("\n  without a texture: top-left 0x%06X  top-right 0x%06X\n", tl, tr);
        check("without a texture, the screen is uniformly white",
              dominant(tl, 1, 1, 1) && tl == tr);
    }

    bk.texture_release(bk.self, handle);
    bk.close(bk.self);

    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
