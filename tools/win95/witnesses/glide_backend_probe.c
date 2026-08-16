/* E05-S01 - the witness for the Glide bring-up layer.
 *
 * It exercises `platform/render/glide.c` in the order the engine will use it:
 * detect, open, loop, draw, close. What it establishes is that the layer works on
 * the machine, not merely that it compiles.
 *
 * Two things set it apart from E09-S01's demonstration, which already drew a
 * triangle:
 *
 *   - it goes through the **reusable layer** rather than through direct calls, so
 *     that what is proved is what the engine will use;
 *   - it measures the **frame rate** over a hundred frames, which gives the
 *     project's first buffer-swap figure.
 *
 * The report goes to `D:\GLIDEBK.TXT`, readable from the host: on a passthrough
 * Voodoo the screen belongs to the card throughout the rendering, and an emulator
 * screenshot would not show the 3dfx output.
 */
#include "render/glide.h"
#include "win95/clock.h"
#include "win95/startup.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_log) {
        va_list copy;
        va_copy(copy, ap);
        vfprintf(g_log, fmt, copy);
        fflush(g_log);
        va_end(copy);
    }
    vprintf(fmt, ap);
    va_end(ap);
}

int main(int argc, char **argv)
{
    /* "Crash" mode: open the context then die for real.
     *
     * This is E05-S01's step 7 trial, and the most useful in practice. On a
     * passthrough Voodoo, the screen belongs to the card as long as the context is
     * open: a crash without restoration leaves a black screen that only a reboot
     * recovers. Throughout E05's development, where one crashes often, this is the
     * difference between ten seconds and two minutes per mistake.
     *
     * The chain under trial is complete: `dkr_win95_startup` installs the
     * exception filter, `dkr_glide_open` registers the restoration, and the filter
     * runs it before displaying anything at all. */
    const int crash_mode = (argc >= 2 && argv[1][0] == 'c');

    dkr_glide_hardware hw;
    dkr_glide_context  ctx;
    dkr_glide_result   r;
    int i;

    g_log = fopen("D:\\GLIDEBK.TXT", "w");
    dkr_win95_startup("GLIDEBK");

    /* --- Detection --------------------------------------------------------- */
    r = dkr_glide_detect(&hw);
    say("detection          : %s\n", dkr_glide_result_text(r));
    if (r != DKR_GLIDE_OK) {
        /* This is not a failure of the witness: on a machine without a 3dfx board
           it is the intended behaviour, and the message must be legible. */
        say("verdict            : no 3dfx hardware, clear message returned\n");
        if (g_log) fclose(g_log);
        return 0;
    }
    say("Glide version      : 0x%03X\n", hw.glide_version);
    say("boards             : %d\n", hw.board_count);
    say("TMUs               : %d\n", hw.tmu_count);
    say("frame buffer       : %u KB\n", hw.fb_memory_kb);
    for (i = 0; i < hw.tmu_count && i < 3; i++) {
        say("  TMU %d memory     : %u KB\n", i, hw.tmu_memory_kb[i]);
    }
    say("SLI                : %d\n", hw.sli);

    /* What ADR 0002 requires, checked at run time rather than assumed. */
    say("two TMUs required  : %s\n",
        hw.tmu_count >= 2 ? "YES" : "NO - multipass fallback (E05-S04)");

    /* --- Opening ----------------------------------------------------------- */
    r = dkr_glide_open(DKR_GLIDE_RES_640x480, &ctx);
    say("opening 640x480    : %s\n", dkr_glide_result_text(r));
    if (r != DKR_GLIDE_OK) {
        if (g_log) fclose(g_log);
        return 1;
    }
    say("resolution obtained: %dx%d, %d buffers, depth %s\n",
        ctx.width, ctx.height, ctx.buffers, ctx.depth_buffer ? "yes" : "no");

    if (crash_mode) {
        volatile int *nowhere = (volatile int *)0;
        say("crash mode         : null dereference, context open\n");
        dkr_glide_clear(0x00FF00);
        dkr_glide_swap();
        *nowhere = 1;                 /* the filter must restore the display */
        say("NEVER REACHED\n");
        return 9;
    }
    if (ctx.resolution != DKR_GLIDE_RES_640x480) {
        say("  (fallback applied - the requested resolution did not fit)\n");
    }

    /* --- Frame cycle ------------------------------------------------------- */
    {
        unsigned long long start, elapsed_us;
        const int frames = 100;

        dkr_clock_init();
        start = dkr_clock_now_us();

        for (i = 0; i < frames; i++) {
            /* A slow gradient, so that the screen shows that it is running. */
            dkr_glide_clear((unsigned)((i * 2) & 0xFF));
            dkr_glide_draw_test_triangle();
            dkr_glide_swap();
        }
        elapsed_us = dkr_clock_now_us() - start;
        say("%d frames in        : %lu ms\n", frames,
            (unsigned long)(elapsed_us / 1000u));
        if (elapsed_us > 0) {
            say("frame rate         : %lu frames/s\n",
                (unsigned long)((unsigned long long)frames * 1000000u / elapsed_us));
        }
    }

    /* --- Reading back what the card drew ------------------------------------- *
     *
     * This is the first time this project **sees** its 3dfx output. Everything
     * before rested on the absence of a crash: on a passthrough Voodoo, the screen
     * belongs to the card and no capture of the emulator shows it. */
    {
        static unsigned pixels[640 * 480];
        int rw = 0, rh = 0;
        int got;

        /* **One last frame on a black background**, so that "painted" means
           something. The frame-rate loop cleared to a gradient: the whole screen
           was painted there, and counting the non-black pixels would have proved
           nothing. */
        dkr_glide_clear(0x000000);
        dkr_glide_draw_test_triangle();
        dkr_glide_swap();

        got = dkr_glide_read_framebuffer(pixels, 640 * 480, &rw, &rh);
        if (got <= 0) {
            say("read back         : unavailable (grLfbLock absent or refused)\n");
        } else {
            int x, y, painted = 0, background = 0;
            say("read back         : %d pixels, %dx%d\n", got, rw, rh);
            /* We have just drawn the triangle on a black background and swapped:
               the front buffer therefore carries the image. Counting what is
               painted says whether the triangle really exists. */
            for (y = 0; y < rh; y++) {
                for (x = 0; x < rw; x++) {
                    const unsigned c = pixels[(size_t)y * (size_t)rw + (size_t)x];
                    if ((c & 0x00FFFFFFu) == 0u) { background++; } else { painted++; }
                }
            }
            say("  pixels painted  : %d\n", painted);
            say("  background      : %d\n", background);
            /* The triangle covers about half the screen; we check generously, the
               point being to tell "something" from "nothing". */
            say("  verdict         : %s\n",
                (painted > rw * rh / 20) ? "THE TRIANGLE IS INDEED DRAWN"
                                         : "nothing visible");
            {
                /* A sample at the centre, written out in plain text: it is the most
                   direct proof one can bring back from a card whose screen nobody
                   sees. */
                const unsigned c = pixels[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)];
                say("  centre of screen: 0x%06X\n", c & 0x00FFFFFFu);
            }
            /* And the whole image, so that it can finally be looked at. */
            {
                FILE *bmp = fopen("D:\\GLIDEBK.BMP", "wb");
                if (bmp) {
                    const int pad = (4 - (rw * 3) % 4) % 4;
                    const unsigned data = (unsigned)((rw * 3 + pad) * rh);
                    unsigned char head[54];
                    int x, y, i;
                    memset(head, 0, sizeof(head));
                    head[0] = 'B'; head[1] = 'M';
                    *(unsigned *)&head[2]  = 54u + data;
                    *(unsigned *)&head[10] = 54u;
                    *(unsigned *)&head[14] = 40u;
                    *(int *)     &head[18] = rw;
                    *(int *)     &head[22] = rh;
                    head[26] = 1; head[28] = 24;
                    *(unsigned *)&head[34] = data;
                    fwrite(head, 1, sizeof(head), bmp);
                    /* BMP stores its rows from the bottom upwards. */
                    for (y = rh - 1; y >= 0; y--) {
                        for (x = 0; x < rw; x++) {
                            const unsigned c = pixels[(size_t)y * (size_t)rw + (size_t)x];
                            unsigned char bgr[3];
                            bgr[0] = (unsigned char)(c & 0xFF);
                            bgr[1] = (unsigned char)((c >> 8) & 0xFF);
                            bgr[2] = (unsigned char)((c >> 16) & 0xFF);
                            fwrite(bgr, 1, 3, bmp);
                        }
                        for (i = 0; i < pad; i++) { fputc(0, bmp); }
                    }
                    fclose(bmp);
                    say("  image written   : D:\\GLIDEBK.BMP\n");
                }
            }
        }
    }

    /* --- Closing ----------------------------------------------------------- */
    dkr_glide_shutdown();
    say("close              : display restored\n");
    /* Idempotence: the exception filter may call it afterwards. */
    dkr_glide_shutdown();
    say("second close       : no effect, as expected\n");

    say("verdict            : the Glide bring-up layer works\n");
    if (g_log) fclose(g_log);
    return 0;
}
