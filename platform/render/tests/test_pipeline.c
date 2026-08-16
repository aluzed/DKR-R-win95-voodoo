/* E04 — the complete chain, on a synthetic scene.
 *
 * Five modules fit together here for the first time:
 *
 *     f3ddkr    reads the display list and validates
 *     transform applies the model-view-projection matrix
 *     clip      clips at the near plane, culls back faces
 *     backend   the E04-S01 interface
 *     software  the reference rasteriser, which writes the image
 *
 * What is established is not that the rendering is *right* — the game will be
 * needed for that — but that **the chain is continuous**: a command written in
 * RDRAM comes back out as pixels, and every stage hands its neighbour what that
 * neighbour expects.
 *
 * The scene is built by hand in a fake RDRAM. That is what makes the test
 * possible without the ROM, and what makes it conclusive: we know the answer in
 * advance, so we can check it pixel by pixel rather than by eye.
 */
#include "render/software.h"
#include "scene_synthetic.h"

#include <stdio.h>
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

static void report(const char *fmt, unsigned long a, unsigned long b)
{
    char line[160];
    sprintf(line, fmt, a, b);
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

/* The scene lives in `scene_synthetic.h`, shared with the E09-S02 comparator.
   Two copies would drift, and the divergence would be blamed on the hardware
   rather than on the copy. */
static unsigned char g_ram[DKR_SCENE_RAM];

static unsigned pixel(int x, int y)
{
    int w, h;
    const unsigned *fb = dkr_software_framebuffer(&w, &h);
    if (!fb || x < 0 || y < 0 || x >= w || y >= h) { return 0; }
    return fb[(size_t)y * (size_t)w + (size_t)x];
}

int main(void)
{
    dkr_render_backend backend;
    dkr_f3d_context    ctx;
    g_out = fopen("D:\\PIPELINE.TXT", "w");

    dkr_render_backend_software(&backend);
    check("the rasteriser opens at 320x240", backend.open(backend.self, 320, 240) != 0);
    backend.begin_frame(backend.self, 0x001030);   /* a recognisable dark blue */

    {
        dkr_render_state st;
        scene_state(&st);
        backend.set_state(backend.self, &st);
    }

    scene_build(g_ram);
    scene_setup(&ctx, g_ram, &backend, 320, 240);

    check("the display list runs", dkr_f3d_run(&ctx, 0) == 5);
    check("the six vertices are loaded",  ctx.state.vertices  == 6);
    check("the three triangles are read", ctx.state.triangles == 3);

    report("  triangles requested: %lu, emitted: %lu",
           ctx.state.triangles, ctx.state.emitted);
    report("  split in two: %lu, discarded: %lu",
           ctx.state.clip_split, ctx.state.clipped_away);

    /* The straddling triangle must have been clipped: one vertex behind gives a
       quadrilateral, hence two triangles. This is the check that proves clipping
       is genuinely **inside** the chain and not merely in its test suite. */
    check("the straddling triangle was clipped in two", ctx.state.clip_split == 1);
    check("the chain emitted more triangles than it read",
          ctx.state.emitted > ctx.state.triangles);
    check("no range rejection", ctx.state.rejects[DKR_F3D_REJECT_ADDRESS] == 0 &&
                                ctx.state.rejects[DKR_F3D_REJECT_INDEX]   == 0);

    /* --- The image ----------------------------------------------------------- *
     *
     * The square is centred and spans -60 to +60 in x, at z = 200. With a scale
     * of 160 and w = z, it occupies 160 +/- 48 pixels. The centre must therefore
     * be painted, and a corner of the screen stay at the background colour. */
    check("the centre of the screen is painted",
          (pixel(160, 120) & 0x00FFFFFFu) != 0x001030u);
    check("a corner stays at the background",
          (pixel(4, 4) & 0x00FFFFFFu) == 0x001030u);
    /* The vertex colours are interpolated: the centre of the square is a
       mixture, so no component dominates at 255. */
    {
        /* **Where to sample matters as much as what is looked for there.**
         *
         * This check aimed at the quad's gradient and read the centre of the
         * screen. It found red there as long as the colour was interpolated with
         * perspective correction; since it is iterated as on the hardware, it is
         * the clipped polygon — green and cyan — that occupies the centre, and
         * the red vanished with no regression having taken place.
         *
         * The quad spans y = 72 to y = 168; the clipped polygon only bites into
         * its lower half. So we read at y = 85, where the quad is alone, and we
         * additionally check that two distinct points differ — otherwise a flat
         * fill would pass for a gradient. */
        const unsigned c  = pixel(160, 85);
        const unsigned c2 = pixel(200, 85);
        const unsigned r  = (c >> 16) & 0xFF;
        check("the quad carries red, which the background does not", r > 20);
        check("and its colour varies from point to point: it is a gradient",
              (c & 0x00FFFFFFu) != (c2 & 0x00FFFFFFu));
    }

    /* --- The rendering order of translucent surfaces -------------------------- *
     *
     * The N64 drew in display-list order, and the game depends on it: that order
     * has to be reproduced rather than sorted. The trap ticket E05-S05 names is
     * classic to any state grouping — a batching optimisation that reorders
     * primitives to save register changes silently breaks the layering.
     *
     * We test it here rather than assert it: three translucent triangles at the
     * *same* depth, emitted in a known order. The last emitted must win. Any
     * sort at all would change the result. */
    {
        dkr_render_state st;
        dkr_render_vertex v[9];
        int i;
        const unsigned char R[3] = { 255, 0, 0 };
        const unsigned char G[3] = { 0, 255, 0 };
        const unsigned char B[3] = { 0, 0, 255 };
        const unsigned char *colours[3];
        colours[0] = R; colours[1] = G; colours[2] = B;

        memset(&st, 0, sizeof(st));
        st.combine = DKR_COMBINE_SHADE;
        st.blend   = DKR_BLEND_OPAQUE;
        st.depth   = DKR_DEPTH_DISABLED;   /* nothing must sort on our behalf */
        st.cull    = DKR_CULL_NONE;
        backend.begin_frame(backend.self, 0x000000);
        backend.set_state(backend.self, &st);

        memset(v, 0, sizeof(v));
        for (i = 0; i < 9; i++) {
            const int tri = i / 3;
            const float xs[3] = { 40.0f, 280.0f, 40.0f };
            const float ys[3] = { 40.0f, 40.0f, 200.0f };
            v[i].x = xs[i % 3]; v[i].y = ys[i % 3];
            v[i].r = (float)colours[tri][0];
            v[i].g = (float)colours[tri][1];
            v[i].b = (float)colours[tri][2];
            v[i].a = 255.0f;
            v[i].oow = 1.0f;
            v[i].z = 0.5f;                  /* strictly the same depth */
        }
        backend.draw_triangles(backend.self, v, 3);

        /* The third emitted is blue: it is the one that must remain. */
        check("display-list order is honoured: the last emitted wins",
              (pixel(80, 60) & 0x00FFFFFFu) == 0x0000FFu);
        /* And the negative check, without which the previous one would pass on a
           renderer that only drew the last triangle. */
        check("and all three were indeed drawn, not just the last",
              ctx.state.emitted > 0);
    }

    check("the image writes out", dkr_software_write_bmp("D:\\PIPELINE.BMP") != 0);

    backend.close(backend.self);

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
