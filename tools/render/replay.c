/* E09-S02 — replay a captured display list through the software rasteriser.
 *
 *   replay capture.bin out.bmp
 *
 * **What this is for.** Three questions about the image went unanswered this
 * week — did the constant repacking change the sky, did the residency cache
 * change anything, is the second texture unit what stopped the character names
 * doubling — and every one failed the same way: the scene animates, two runs do
 * not reach a display list at the same moment of it, and comparing their pixels
 * measures the animation. The last was left explicitly unattributed for want of
 * this program.
 *
 * A capture is a frozen input. Replayed, the same bytes give the same image, so
 * a difference between two renderings is the renderer's and nothing else's.
 *
 * **It runs on the development machine, and that is the point.** The software
 * rasteriser is E04-S08's oracle: it implements the RDP's combiner without
 * Glide's constraints, so when its image is right and the card's is wrong, the
 * decoder is out of the question and the fault is in the backend. That
 * isolation is what E05-S03 and E05-S04 have been missing.
 */

#include "render/backend.h"
#include "render/capture.h"
#include "render/f3ddkr.h"
#include "render/software.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    dkr_capture_header h;
    unsigned char *rdram = 0;
    dkr_render_backend bk;
    dkr_f3d_context ctx;
    const unsigned int *pixels;
    int w = 0, hgt = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s capture.bin out.bmp\n", argv[0]);
        return 2;
    }
    if (!dkr_capture_read(argv[1], &h, &rdram)) { return 1; }

    printf("capture: list %u, list at 0x%06X, %u bytes of RDRAM, %s, %dx%d\n",
           h.list_index, h.data_ptr, h.rdram_bytes,
           h.rdram_native ? "interleaved" : "plain",
           (int)h.screen_w, (int)h.screen_h);

    dkr_render_backend_software(&bk);
    if (!bk.open(bk.self, (int)h.screen_w, (int)h.screen_h)) {
        fprintf(stderr, "replay: the software rasteriser refused %ux%u\n",
                h.screen_w, h.screen_h);
        free(rdram);
        return 1;
    }

    dkr_f3d_init(&ctx, rdram, h.rdram_bytes, &bk);
    /* Every property of the capture, taken from the capture. Assuming any of
       them here would make the replay disagree with the run for a reason that
       has nothing to do with the renderer -- which is the one thing this
       program exists not to do. */
    ctx.rdram_native = (unsigned char)(h.rdram_native ? 1 : 0);
    ctx.screen_width  = (short)h.screen_w;
    ctx.screen_height = (short)h.screen_h;
    /* One texture unit: this is the oracle, and the oracle renders without
       Glide's constraints rather than with a second copy of them. */
    ctx.tmu_count = 1;

    bk.begin_frame(bk.self, 0x000000u);
    (void)dkr_f3d_run(&ctx, h.data_ptr);
    bk.present(bk.self);

    printf("decoded: cmd=%lu tri=%lu emitted=%lu rejects=%lu textures=%lu\n",
           ctx.state.commands, ctx.state.triangles, ctx.state.emitted,
           ctx.state.rejects[0] + ctx.state.rejects[1] + ctx.state.rejects[2] +
           ctx.state.rejects[3] + ctx.state.rejects[4],
           ctx.state.textures_loaded);

    /* The rasteriser writes its own BMP -- same format as the target's frame
       dump, which is what lets the two be compared without a conversion in
       between, and one fewer implementation of a format that is easy to get
       subtly wrong. */
    pixels = dkr_software_framebuffer(&w, &hgt);
    if (!pixels || w <= 0 || hgt <= 0) {
        fprintf(stderr, "replay: no image came back\n");
        bk.close(bk.self);
        free(rdram);
        return 1;
    }
    if (!dkr_software_write_bmp(argv[2])) {
        fprintf(stderr, "replay: could not write %s\n", argv[2]);
        bk.close(bk.self);
        free(rdram);
        return 1;
    }
    printf("wrote %s (%dx%d)\n", argv[2], w, hgt);

    bk.close(bk.self);
    free(rdram);
    return 0;
}
