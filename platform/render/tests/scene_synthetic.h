/* The synthetic scene, written once.
 *
 * Two tests use it: `test_pipeline.c`, which checks that the chain is
 * continuous, and `test_compare.c`, which runs the *same* input through the
 * reference rasteriser and through the Voodoo to compare the two images.
 *
 * **It is that second test which forces the code to be shared.** A comparator
 * whose two sides each build their own scene does not measure a rendering
 * difference: it first measures the drift between two copies. The divergence
 * would appear later, on an innocuous change to one of the two, and would be
 * blamed on the hardware.
 *
 * The scene is built by hand in a fake RDRAM: we know the answer in advance, so
 * we can check it pixel by pixel rather than by eye, and the test stands without
 * the ROM.
 */
#ifndef DKR_SCENE_SYNTHETIC_H
#define DKR_SCENE_SYNTHETIC_H

#include "render/f3ddkr.h"

#include <string.h>

#define DKR_SCENE_RAM 8192u

/* --- Writing into RDRAM, big-endian like the real thing --------------------- */

static void scene_put32(unsigned char *r, unsigned int a, unsigned int v)
{
    r[a] = (unsigned char)(v >> 24); r[a + 1] = (unsigned char)(v >> 16);
    r[a + 2] = (unsigned char)(v >> 8); r[a + 3] = (unsigned char)v;
}

static void scene_put16(unsigned char *r, unsigned int a, int v)
{
    r[a] = (unsigned char)((unsigned)v >> 8); r[a + 1] = (unsigned char)v;
}

static unsigned int scene_cmd(unsigned char *r, unsigned int a,
                              unsigned int w0, unsigned int w1)
{
    scene_put32(r, a, w0); scene_put32(r, a + 4, w1); return a + 8;
}

/* A matrix in the N64's format: sixteen integer parts then sixteen fractional
   ones, each on sixteen bits — the microcode's 16.16, cut into two halves. */
static void scene_put_matrix(unsigned char *r, unsigned int at,
                             const float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const int k = i * 4 + j;
            const int fixed = (int)(m[i][j] * 65536.0f);
            scene_put16(r, at + (unsigned)k * 2,      (fixed >> 16) & 0xFFFF);
            scene_put16(r, at + 32 + (unsigned)k * 2,  fixed        & 0xFFFF);
        }
    }
}

/* --- The scene --------------------------------------------------------------- *
 *
 * A quad facing us at z = 200, plus a third triangle that **crosses the near
 * plane** — it is what exercises clipping, and without it the chain would only
 * be half tested. It partially covers the quad, which also brings the depth
 * buffer into play: that is where the reference rasteriser, which sorts on z in
 * [0,1], and the card, which sorts on a w buffer, can most easily diverge. */
static void scene_build(unsigned char *ram)
{
    const unsigned int MATRIX_AT   = 0x0400u;
    const unsigned int VERTEX_AT   = 0x0600u;
    const unsigned int TRIANGLE_AT = 0x0700u;
    unsigned int at;

    memset(ram, 0, DKR_SCENE_RAM);

    {
        /* Identity: the projection does all the work. */
        float ident[4][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1} };
        scene_put_matrix(ram, MATRIX_AT, ident);
    }

    {
        const short v[6][3] = {
            { -60, -60, 200 }, {  60, -60, 200 },
            {  60,  60, 200 }, { -60,  60, 200 },
            { -30,   0,  -50 },   /* behind the camera */
            {  90,   0, 300 },
        };
        const unsigned char col[6][3] = {
            { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 },
            { 255, 0, 255 }, { 0, 255, 255 },
        };
        int i;
        for (i = 0; i < 6; i++) {
            const unsigned int a = VERTEX_AT + (unsigned)i * 10u;
            scene_put16(ram, a + 0, v[i][0]);
            scene_put16(ram, a + 2, v[i][1]);
            scene_put16(ram, a + 4, v[i][2]);
            ram[a + 6] = col[i][0]; ram[a + 7] = col[i][1];
            ram[a + 8] = col[i][2]; ram[a + 9] = 255;
        }
    }

    {
        /* Bit 0x40 disables culling: the scene has no consistent winding. */
        const unsigned char tris[3][3] = { {0,1,2}, {0,2,3}, {4,5,1} };
        int i;
        for (i = 0; i < 3; i++) {
            const unsigned int a = TRIANGLE_AT + (unsigned)i * 16u;
            ram[a + 0] = 0x40;
            ram[a + 1] = tris[i][0];
            ram[a + 2] = tris[i][1];
            ram[a + 3] = tris[i][2];
        }
    }

    at = 0;
    at = scene_cmd(ram, at, 0xBF000000u, 0x00000000u);   /* DMAOffsets, zero bases */
    at = scene_cmd(ram, at, 0x01000040u, MATRIX_AT);     /* Matrix, slot 0 */
    at = scene_cmd(ram, at, 0x04000000u | (5u << 19) | (0u << 9), VERTEX_AT);
    at = scene_cmd(ram, at, 0x05200000u, TRIANGLE_AT);   /* 3 triangles */
    (void)scene_cmd(ram, at, 0xB8000000u, 0u);           /* end */
}

/* Prepares the context and the projection for a `w` x `h` viewport.
 *
 * The projection is such that `w_clip = z` and `z_clip = z / 2`, so `z/w` is
 * 0.5. **The factor 0.5 is not decorative**: with `z_clip = z`, the division
 * gives 1.0 everywhere, which is exactly the depth buffer's clear value. The
 * test then fails across the whole screen and **nothing is painted**, with no
 * stage reporting anything at all — every module declares itself satisfied. It
 * was the `emitted` counter that settled it. */
static void scene_setup(dkr_f3d_context *ctx, unsigned char *ram,
                        dkr_render_backend *bk, int w, int h)
{
    dkr_matrix proj;

    dkr_f3d_init(ctx, ram, DKR_SCENE_RAM, bk);
    dkr_transform_set_viewport(&ctx->transform,
                               (float)w * 0.5f, -(float)h * 0.5f,
                               (float)w * 0.5f,  (float)h * 0.5f);

    memset(&proj, 0, sizeof(proj));
    proj.m[0][0] = 1.0f; proj.m[1][1] = 1.0f; proj.m[2][2] = 0.5f;
    proj.m[2][3] = 1.0f;
    /* **A constant term on z, without which the scene does not test depth.**
     *
     * With `z_clip = 0.5 z` and `w = z`, the ratio `z/w` is 0.5 for *every*
     * vertex: the quad and the clipped triangle end up at exactly the same
     * depth, and their overlap produces a sorting conflict. The software
     * rasteriser answered it with a stipple, the card with a clean edge — two
     * equally arbitrary answers to a badly posed question, and the comparison
     * measured that ambiguity rather than the rendering.
     *
     * The term makes `z/w = 0.5 - 20/z`, which varies with distance. The scene
     * then genuinely separates the two surfaces. */
    proj.m[3][2] = -20.0f;
    dkr_transform_set_projection(&ctx->transform, &proj);
}

/* The shared render state. No texture and no fog: what the comparison measures
   here is geometry, interpolated colour and depth — the three things both
   backends can already do. */
static void scene_state(dkr_render_state *st)
{
    memset(st, 0, sizeof(*st));
    st->combine = DKR_COMBINE_SHADE;
    st->blend   = DKR_BLEND_OPAQUE;
    st->depth   = DKR_DEPTH_TEST_AND_WRITE;
    st->cull    = DKR_CULL_NONE;   /* the decoder decides, not the state */
}

#endif /* DKR_SCENE_SYNTHETIC_H */
