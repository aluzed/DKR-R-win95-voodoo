/* E04-S08 — the reference rasteriser test.
 *
 * An oracle checked by eye is not an oracle: its entire value rests on the trust
 * placed in it, and "it looks right" does not transfer. Every check below
 * therefore compares a read-back pixel against an **analytically computed**
 * value.
 *
 * The central check is the one for perspective correction, because that is the
 * one a naive rasteriser misses in silence: interpolating texture coordinates
 * linearly in screen space makes textures ripple across any surface seen at an
 * angle. The artefact is discreet, and it is precisely the kind of thing one
 * would later try to pin on Glide.
 *
 * A single source for host and target, like the repository's other suites.
 */
/* The coordinates are laid down in Glide's 256-texel space, which the contract
   imposes — see `DKR_TEXCOORD_SCALE` in `backend.h`. The expected values stay
   normalised: that is what the rasteriser samples after the divide, and that is
   what reads back. */
#include "render/software.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "FAIL ", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "FAIL ", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    const int ok = (got - want < tol) && (want - got < tol);
    printf("  %s %-46s expected %.4f, got %.4f\n",
           ok ? "ok   " : "FAIL ", what, want, got);
    if (g_out) {
        fprintf(g_out, "  %s %-46s expected %.4f, got %.4f\n",
                ok ? "ok   " : "FAIL ", what, want, got);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

/* A 256-texel intensity texture where texel `i` is `i`. Reading a pixel back
   therefore gives directly the `s` coordinate that sampled it, up to
   quantisation. That is what makes perspective correction measurable rather than
   merely visible. */
static unsigned char g_ramp[256];

static dkr_texture_handle upload_ramp(dkr_render_backend *b)
{
    dkr_texture_desc d;
    int i;
    for (i = 0; i < 256; i++) { g_ramp[i] = (unsigned char)i; }
    memset(&d, 0, sizeof(d));
    d.key        = 0x1234ull;
    d.format     = DKR_TEXFMT_INTENSITY8;
    d.width      = 256;
    d.height     = 1;
    d.pixels     = g_ramp;
    d.size_bytes = sizeof(g_ramp);
    return b->texture_upload(b->self, &d);
}

static unsigned pixel_at(int x, int y)
{
    int w, h;
    const unsigned *fb = dkr_software_framebuffer(&w, &h);
    if (!fb || x < 0 || y < 0 || x >= w || y >= h) { return 0; }
    return fb[(size_t)y * (size_t)w + (size_t)x];
}

int main(void)
{
    dkr_render_backend b;
    dkr_render_state   st;
    dkr_render_vertex  v[6];
    dkr_texture_handle tex;

    g_out = fopen("D:\\SOFTRAS.TXT", "w");

    dkr_render_backend_software(&b);
    check("the backend opens", b.open(b.self, 128, 64) != 0);

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;

    /* --- A filled triangle, vertex colour ---------------------------------- */
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    v[0].x =  10.0f; v[0].y = 10.0f; v[0].r = 255.0f; v[0].a = 255.0f; v[0].oow = 1.0f;
    v[1].x = 110.0f; v[1].y = 10.0f; v[1].r = 255.0f; v[1].a = 255.0f; v[1].oow = 1.0f;
    v[2].x =  60.0f; v[2].y = 54.0f; v[2].r = 255.0f; v[2].a = 255.0f; v[2].oow = 1.0f;
    b.draw_triangles(b.self, v, 1);
    check("a red triangle covers its interior", (pixel_at(60, 25) & 0x00FF0000u) != 0);
    check("and does not spill outside",          (pixel_at(2, 60)  & 0x00FFFFFFu) == 0);

    /* --- Perspective correction -------------------------------------------- *
     *
     * A triangle whose left and right vertices have very different `w` — 1 and
     * 4 — which is the case for a surface seen at an angle. At the middle of the
     * edge:
     *
     *     1/w = (1-a)/w0 + a/w1       = 0.5 * 1 + 0.5 * 0.25 = 0.625
     *     s/w = (1-a)*s0/w0 + a*s1/w1 = 0       + 0.5 * 0.25 = 0.125
     *     s   = 0.125 / 0.625                                = 0.20
     *
     * **There are two ways to get this wrong, and they do not give the same
     * value.** The self-test showed it, and showed it against a first version of
     * this check that only aimed at one of them:
     *
     *     0.20    correct — we divide s/w by 1/w
     *     0.125   we interpolate s/w and forget to divide
     *     0.50    we store `s` instead of `s/w` and interpolate in screen space
     *
     * The check therefore aims at 0.20 with a tight tolerance, and explicitly
     * rejects the other two. A loose tolerance would have accepted 0.125. */
    tex = upload_ramp(&b);
    check("the texture uploads", tex != 0);
    st.combine = DKR_COMBINE_TEXTURE;
    st.texture = tex;
    st.wrap_s  = DKR_WRAP_CLAMP;
    st.wrap_t  = DKR_WRAP_CLAMP;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);

    memset(v, 0, sizeof(v));
    /* Horizontal band from y=20 to y=40, from x=0 to x=100. */
    v[0].x =   0.0f; v[0].y = 20.0f; v[0].oow = 1.0f;    /* w = 1 */
    v[1].x = 100.0f; v[1].y = 20.0f; v[1].oow = 0.25f;   /* w = 4 */
    v[2].x =   0.0f; v[2].y = 40.0f; v[2].oow = 1.0f;
    v[3] = v[1];
    v[4].x = 100.0f; v[4].y = 40.0f; v[4].oow = 0.25f;
    v[5] = v[2];
    /* `s/w` and not `s`: that is what a vertex carries, here as in Glide. */
    v[0].tmu[0][DKR_TMU_SOW] = (0.0f  * 1.0f) * DKR_TEXCOORD_SCALE;
    v[1].tmu[0][DKR_TMU_SOW] = (1.0f  * 0.25f) * DKR_TEXCOORD_SCALE;
    v[2].tmu[0][DKR_TMU_SOW] = (0.0f  * 1.0f) * DKR_TEXCOORD_SCALE;
    v[3].tmu[0][DKR_TMU_SOW] = (1.0f  * 0.25f) * DKR_TEXCOORD_SCALE;
    v[4].tmu[0][DKR_TMU_SOW] = (1.0f  * 0.25f) * DKR_TEXCOORD_SCALE;
    v[5].tmu[0][DKR_TMU_SOW] = (0.0f  * 1.0f) * DKR_TEXCOORD_SCALE;
    { int i; for (i = 0; i < 6; i++) { v[i].a = 255.0f; } }
    b.draw_triangles(b.self, v, 2);

    {
        /* At the middle of the band. The read-back intensity gives `s`
           directly. */
        const unsigned c = pixel_at(50, 30);
        const double s = (double)(c & 0xFFu) / 255.0;
        check_near("s at the middle, perspective corrected", s, 0.20, 0.03);
        /* The two named errors, so that the failure reads without re-reading the
           comment above. */
        check("s is not 0.125 - forgotten divide",       s > 0.16);
        check("s is not 0.50 - s stored instead of s/w", s < 0.35);
    }

    /* --- Wrapping ----------------------------------------------------------- */
    {
        struct { dkr_wrap_mode mode; float s; double want; const char *name; } cases[] = {
            { DKR_WRAP_CLAMP,  1.50f, 1.00, "clamping beyond 1" },
            { DKR_WRAP_CLAMP, -0.50f, 0.00, "clamping below 0" },
            { DKR_WRAP_REPEAT, 1.25f, 0.25, "repeat at 1.25" },
            { DKR_WRAP_MIRROR, 1.25f, 0.75, "mirror at 1.25" },
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            st.wrap_s = cases[i].mode;
            b.begin_frame(b.self, 0x000000);
            b.set_state(b.self, &st);
            memset(v, 0, sizeof(v));
            v[0].x =  0.0f; v[0].y = 10.0f;
            v[1].x = 60.0f; v[1].y = 10.0f;
            v[2].x =  0.0f; v[2].y = 50.0f;
            { int k; for (k = 0; k < 3; k++) {
                v[k].oow = 1.0f; v[k].a = 255.0f;
                v[k].tmu[0][DKR_TMU_SOW] = (cases[i].s) * DKR_TEXCOORD_SCALE;
            } }
            b.draw_triangles(b.self, v, 1);
            {
                const double s = (double)(pixel_at(10, 20) & 0xFFu) / 255.0;
                check_near(cases[i].name, s, cases[i].want, 0.02);
            }
        }
        st.wrap_s = DKR_WRAP_CLAMP;
    }

    /* --- Bilinear filtering -------------------------------------------------- *
     *
     * Between two neighbouring texels of the ramp, the halfway sample must be
     * their average. The check aims at the **half-texel**: without it the image
     * is offset by half a texel width, which does not show on a test pattern and
     * shows perfectly in an image comparison. */
    st.filter  = DKR_FILTER_BILINEAR;
    st.combine = DKR_COMBINE_TEXTURE;
    st.texture = tex;
    st.wrap_s  = DKR_WRAP_CLAMP;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    v[0].x =  0.0f; v[0].y = 10.0f;
    v[1].x = 60.0f; v[1].y = 10.0f;
    v[2].x =  0.0f; v[2].y = 50.0f;
    { int k; for (k = 0; k < 3; k++) {
        v[k].oow = 1.0f; v[k].a = 255.0f;
        /* Exactly between texel 100 and texel 101: (100.5)/256. */
        v[k].tmu[0][DKR_TMU_SOW] = (100.5f / 256.0f) * DKR_TEXCOORD_SCALE;
    } }
    b.draw_triangles(b.self, v, 1);
    check_near("bilinear: the average of two neighbouring texels",
               (double)(pixel_at(10, 20) & 0xFFu), 100.5, 1.0);
    st.filter = DKR_FILTER_POINT;

    /* --- Depth --------------------------------------------------------------- */
    st.combine = DKR_COMBINE_SHADE;
    st.texture = 0;
    st.depth   = DKR_DEPTH_TEST_AND_WRITE;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    /* A green triangle far away, then a red one near: red must win.
     *
     * **It is `oow` that carries depth**, as on the Voodoo: a large `1/w` means
     * near. The first version of this test filled in `z` and set `oow = 1`
     * everywhere — it then passed on a z buffer and failed as soon as the
     * rasteriser sorted like the hardware. Both fields are therefore filled in
     * consistently: `w` of 5 for the far one, 1.25 for the near one.
     *
     * `z` stays filled in because it is not dead: it will serve the day we
     * confront the port with the RDP rather than with Glide. */
    v[0].x = 10.0f; v[0].y = 10.0f; v[0].z = 0.8f; v[0].oow = 0.20f; v[0].g = 255.0f;
    v[1].x = 90.0f; v[1].y = 10.0f; v[1].z = 0.8f; v[1].oow = 0.20f; v[1].g = 255.0f;
    v[2].x = 10.0f; v[2].y = 50.0f; v[2].z = 0.8f; v[2].oow = 0.20f; v[2].g = 255.0f;
    v[3].x = 10.0f; v[3].y = 10.0f; v[3].z = 0.2f; v[3].oow = 0.80f; v[3].r = 255.0f;
    v[4].x = 90.0f; v[4].y = 10.0f; v[4].z = 0.2f; v[4].oow = 0.80f; v[4].r = 255.0f;
    v[5].x = 10.0f; v[5].y = 50.0f; v[5].z = 0.2f; v[5].oow = 0.80f; v[5].r = 255.0f;
    { int i; for (i = 0; i < 6; i++) { v[i].a = 255.0f; } }
    b.draw_triangles(b.self, v, 2);
    check("the nearer one hides the farther one",
          (pixel_at(30, 20) & 0x00FF0000u) != 0 &&
          (pixel_at(30, 20) & 0x0000FF00u) == 0);

    /* The reverse order must give the same result: that is what the depth buffer
       promises, and forgetting it gives rendering that depends on emission order
       — a very painful defect to diagnose later. */
    b.begin_frame(b.self, 0x000000);
    { dkr_render_vertex tmp[3]; memcpy(tmp, v, sizeof(tmp));
      memcpy(v, v + 3, sizeof(tmp)); memcpy(v + 3, tmp, sizeof(tmp)); }
    b.draw_triangles(b.self, v, 2);
    check("and the result does not depend on emission order",
          (pixel_at(30, 20) & 0x00FF0000u) != 0 &&
          (pixel_at(30, 20) & 0x0000FF00u) == 0);

    /* --- Alpha test ---------------------------------------------------------- */
    st.depth = DKR_DEPTH_DISABLED;
    st.alpha_test = 1;
    st.alpha_reference = 128;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    v[0].x = 10.0f; v[0].y = 10.0f;
    v[1].x = 90.0f; v[1].y = 10.0f;
    v[2].x = 10.0f; v[2].y = 50.0f;
    { int i; for (i = 0; i < 3; i++) {
        v[i].oow = 1.0f; v[i].b = 255.0f; v[i].a = 64.0f; } }
    b.draw_triangles(b.self, v, 1);
    check("the alpha test rejects below the threshold", (pixel_at(30, 20) & 0x00FFFFFFu) == 0);
    { int i; for (i = 0; i < 3; i++) { v[i].a = 200.0f; } }
    b.draw_triangles(b.self, v, 1);
    check("and lets through above it", (pixel_at(30, 20) & 0x000000FFu) != 0);
    st.alpha_test = 0;

    /* --- Blending ------------------------------------------------------------ */
    st.blend = DKR_BLEND_ALPHA;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    { int i; for (i = 0; i < 3; i++) {
        v[i].r = 255.0f; v[i].g = 0.0f; v[i].b = 0.0f; v[i].a = 128.0f; } }
    b.draw_triangles(b.self, v, 1);
    {
        /* Red at half over black: about 128. */
        const unsigned red = (pixel_at(30, 20) >> 16) & 0xFFu;
        check_near("alpha blending at half", (double)red, 128.0, 4.0);
    }
    st.blend = DKR_BLEND_OPAQUE;

    /* --- Scissor window ------------------------------------------------------ */
    b.begin_frame(b.self, 0x000000);
    b.set_scissor(b.self, 40, 0, 60, 64);
    b.set_state(b.self, &st);
    { int i; for (i = 0; i < 3; i++) { v[i].a = 255.0f; } }
    v[0].x = 0.0f; v[0].y = 0.0f; v[1].x = 120.0f; v[1].y = 0.0f;
    v[2].x = 0.0f; v[2].y = 60.0f;
    b.draw_triangles(b.self, v, 1);
    check("the scissor window cuts on the left", (pixel_at(20, 10) & 0x00FFFFFFu) == 0);
    check("it lets through in the middle",       (pixel_at(50, 10) & 0x00FF0000u) != 0);
    check("and cuts on the right",               (pixel_at(80, 10) & 0x00FFFFFFu) == 0);
    b.set_scissor(b.self, 0, 0, 128, 64);

    /* --- Filled rectangle ---------------------------------------------------- */
    b.begin_frame(b.self, 0x000000);
    b.fill_rect(b.self, 10, 10, 30, 30, 0x00FF00);
    check("the rectangle is filled",            (pixel_at(20, 20) & 0x0000FF00u) != 0);
    check("and the right bound is exclusive",   (pixel_at(30, 20) & 0x00FFFFFFu) == 0);

    /* --- File output ---------------------------------------------------------- */
    {
        const char *path = "D:\\SOFTRAS.BMP";
        FILE *f;
        check("the image writes out as a BMP", dkr_software_write_bmp(path) != 0);
        f = fopen(path, "rb");
        if (f) {
            unsigned char head[26];
            const size_t got = fread(head, 1, sizeof(head), f);
            fclose(f);
            check("the file carries a BMP header",
                  got == sizeof(head) && head[0] == 'B' && head[1] == 'M');
            check("and the buffer's dimensions",
                  got == sizeof(head) &&
                  *(const int *)&head[18] == 128 && *(const int *)&head[22] == 64);
        } else {
            check("the BMP file reads back", 0);
        }
    }

    b.close(b.self);
    check("closing frees without crashing", 1);

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
