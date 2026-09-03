/* E09-S02 — replay a captured display list, and compare the two renderings of it.
 *
 *   replay capture.bin out.bmp            the oracle (host and target)
 *   replay --card capture.bin out.bmp     the Voodoo (target only)
 *   replay --both capture.bin             both, on the target, and compare
 *
 * **What this is for.** Three questions about the image went unanswered in one
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
 * **One program, two backends, and that is the point.** The decoder, the
 * transform and the clipper are the same object code on both paths; only the
 * backend differs. Two programs would let a divergence hide in the difference
 * between them, and it would be attributed to the card. `COMPARE.EXE` already
 * establishes this on the synthetic scene; this establishes it on a real frame
 * of the game, which is what the synthetic scene cannot reach — it has no
 * multi-texturing, no multipass, no texture cache under pressure.
 *
 * **The oracle is E04-S08's**: it implements the RDP's combiner without Glide's
 * constraints, so when its image is right and the card's is wrong, the decoder is
 * out of the question and the fault is in the backend. That isolation is what
 * E05-S03 and E05-S04 have been missing.
 */

#include "render/backend.h"
#include "render/capture.h"
#include "render/f3ddkr.h"
#include "render/imagecmp.h"
#include "render/software.h"

#ifdef DKR_HAVE_GLIDE
#include "render/glide.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>       /* _commit */
#endif

/* --- Reporting ------------------------------------------------------------- *
 *
 * Everything this program says goes to standard output and, when `--log` names
 * one, to a file. On the Windows 95 machine that file is the only way the result
 * comes back: the console scrolls, the screen is captured as a picture, and a
 * measurement one has to read off a screenshot is a measurement one stops
 * taking. `COMPARE.EXE` writes `D:\COMPARE.TXT` for the same reason.
 *
 * The file is committed to the disk on every line, and `fflush` is not enough for
 * that. Measured on 3 September 2026: a run that faulted after printing its first
 * line left `D:\REPLAY.TXT` at **zero bytes**, the flushed data sitting in
 * Windows 95's write-behind cache with the directory entry never updated. So the
 * line goes out with `_commit`, which is `FlushFileBuffers`. A run that faults --
 * and this one drives a card that has faulted before -- must leave behind what it
 * had measured up to the fault, which is usually the interesting part. */
static FILE *g_log;

static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    fflush(stdout);
    if (g_log) {
        fputs(line, g_log);
        fflush(g_log);
#ifdef _WIN32
        _commit(_fileno(g_log));
#endif
    }
}

/* What the decoder did, on either path. Compared before the images are: if the
   two backends did not receive the same geometry, a pixel difference no longer
   says anything about rendering — it says the chain is not deterministic, which
   is the more serious fault and the one to see first. */
typedef struct {
    unsigned long commands;
    unsigned long triangles;
    unsigned long emitted;
    unsigned long rejects;
    unsigned long textures;
} replay_counts;

static void take_counts(const dkr_f3d_context *ctx, replay_counts *c)
{
    c->commands  = ctx->state.commands;
    c->triangles = ctx->state.triangles;
    c->emitted   = ctx->state.emitted;
    c->rejects   = ctx->state.rejects[0] + ctx->state.rejects[1] +
                   ctx->state.rejects[2] + ctx->state.rejects[3] +
                   ctx->state.rejects[4];
    c->textures  = ctx->state.textures_loaded;
}

static void say_counts(const char *who, const replay_counts *c)
{
    say("  %-8s cmd=%lu tri=%lu emitted=%lu rejects=%lu textures=%lu\n",
           who, c->commands, c->triangles, c->emitted, c->rejects, c->textures);
}

/* Runs the capture into `bk`. Every property of the replay is taken from the
   capture rather than assumed: assuming one would make the replay disagree with
   the run for a reason that has nothing to do with the renderer, which is the one
   thing this program exists not to do. */
/* The decoder's context is 138 KB and it lives here rather than on the stack.
   On the development machine either works; on Windows 95 a frame that large in a
   called function is a wager on how the prologue probes its pages, and losing it
   produces a general protection fault with no message — which is a bad way to
   learn where a replay stopped. Only one replay runs at a time, so a single
   instance costs nothing. */
static dkr_f3d_context g_ctx;

static void run_capture(dkr_render_backend *bk, const dkr_capture_header *h,
                        unsigned char *rdram, int tmus, replay_counts *out)
{
    dkr_f3d_init(&g_ctx, rdram, h->rdram_bytes, bk);
    g_ctx.rdram_native  = (unsigned char)(h->rdram_native ? 1 : 0);
    g_ctx.screen_width  = (short)h->screen_w;
    g_ctx.screen_height = (short)h->screen_h;
    g_ctx.tmu_count     = (unsigned char)tmus;

    /* Each step announces itself before it runs, and the line is flushed. This
       program opens a card that has faulted before and decodes eight mebibytes
       of somebody else's memory; when it stops, the only thing worth having is
       the name of what it was doing. */
    say("  ... clearing\n");
    bk->begin_frame(bk->self, 0x000000u);
    say("  ... decoding from 0x%06X\n", h->data_ptr);
    (void)dkr_f3d_run(&g_ctx, h->data_ptr);
    say("  ... presenting\n");
    bk->present(bk->self);

    take_counts(&g_ctx, out);
}

static void usage(const char *me)
{
    fprintf(stderr,
            "usage: %s [--card|--both] [--single-tmu] [--log file]\n"
            "          capture.bin [out.bmp]\n",
            me);
}

int main(int argc, char **argv)
{
    dkr_capture_header h;
    unsigned char *rdram = 0;
    const char *cap_path = 0, *out_path = 0, *log_path = 0;
    int want_card = 0, want_both = 0, single_tmu = 0;
    int oracle_tmus = 1;
    int i, status = 0;

    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--card") == 0)       { want_card = 1; }
        else if (strcmp(argv[i], "--both") == 0)       { want_both = 1; }
        else if (strcmp(argv[i], "--single-tmu") == 0) { single_tmu = 1; }
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        }
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "replay: unknown option %s\n", argv[i]);
            return 2;
        }
        else if (!cap_path) { cap_path = argv[i]; }
        else if (!out_path) { out_path = argv[i]; }
        else { usage(argv[0]); return 2; }
    }
    if (!cap_path || (!want_both && !out_path)) { usage(argv[0]); return 2; }

    if (log_path) {
        g_log = fopen(log_path, "w");
        if (!g_log) {
            /* Named and not fatal. A run that measured everything and could not
               write its log is still a run; a run that stopped because of its log
               is a wasted trip to the machine. */
            fprintf(stderr, "replay: cannot write the log %s\n", log_path);
        }
    }

#ifndef DKR_HAVE_GLIDE
    (void)single_tmu;   /* the host has no card to force to one unit */
    if (want_card || want_both) {
        /* Named rather than ignored: a host build silently rendering the oracle
           when the card was asked for would produce a file that looks like the
           answer to a question it never asked. */
        fprintf(stderr, "replay: this build has no Glide backend —"
                        " --card and --both need the Windows 95 build\n");
        return 2;
    }
#endif

    if (!dkr_capture_read(cap_path, &h, &rdram)) { return 1; }

    say("capture: list %u, list at 0x%06X, %u bytes of RDRAM, %s, %ux%u\n",
           h.list_index, h.data_ptr, h.rdram_bytes,
           h.rdram_native ? "interleaved" : "plain", h.screen_w, h.screen_h);

    /* --- The oracle ---------------------------------------------------------- *
     *
     * One texture unit unless asked otherwise: the oracle renders without Glide's
     * constraints rather than with a second copy of them, so it takes the
     * multipass path the way a single-TMU card would. When the card runs with two
     * units, an agreement between the two therefore says something stronger than
     * "the same code ran twice" — it says the two-texel chain computes what the
     * multipass reference computes, which is E05-S04's open question. */
    {
        dkr_render_backend soft;
        replay_counts sc;
        unsigned *soft_pixels = 0;
        int sw = 0, sh = 0;

        if (!want_card || want_both) {
            say("oracle: opening the rasteriser\n");
            dkr_render_backend_software(&soft);
            if (!soft.open(soft.self, (int)h.screen_w, (int)h.screen_h)) {
                fprintf(stderr, "replay: the rasteriser refused %ux%u\n",
                        h.screen_w, h.screen_h);
                free(rdram);
                return 1;
            }
            run_capture(&soft, &h, rdram, oracle_tmus, &sc);
            say_counts("oracle", &sc);

            {
                const unsigned *fb = dkr_software_framebuffer(&sw, &sh);
                if (!fb || sw <= 0 || sh <= 0) {
                    fprintf(stderr, "replay: no image came back\n");
                    soft.close(soft.self);
                    free(rdram);
                    return 1;
                }
                if (want_both) {
                    soft_pixels = (unsigned *)malloc((size_t)sw * (size_t)sh *
                                                     sizeof(unsigned));
                    if (soft_pixels) {
                        memcpy(soft_pixels, fb,
                               (size_t)sw * (size_t)sh * sizeof(unsigned));
                    }
                }
            }
            if (!dkr_image_write_bmp(want_both ? "D:\\RPLSOFT.BMP" : out_path,
                                     dkr_software_framebuffer(&sw, &sh),
                                     sw, sh)) {
                fprintf(stderr, "replay: cannot write the oracle's image\n");
                soft.close(soft.self);
                free(soft_pixels);
                free(rdram);
                return 1;
            }
            soft.close(soft.self);
            if (!want_both) {
                say("wrote %s (%dx%d)\n", out_path, sw, sh);
                free(rdram);
                return 0;
            }
            say("  wrote D:\\RPLSOFT.BMP\n");
        }

#ifdef DKR_HAVE_GLIDE
        /* --- The card -------------------------------------------------------- */
        {
            dkr_render_backend card;
            replay_counts cc;
            unsigned *card_pixels;
            int cw = 0, ch = 0;
            int card_tmus;

            card_pixels = (unsigned *)malloc((size_t)h.screen_w *
                                             (size_t)h.screen_h *
                                             sizeof(unsigned));
            if (!card_pixels) {
                fprintf(stderr, "replay: cannot allocate the card's image\n");
                free(soft_pixels);
                free(rdram);
                return 1;
            }

            dkr_render_backend_glide(&card);
            if (!card.open(card.self, (int)h.screen_w, (int)h.screen_h)) {
                fprintf(stderr, "replay: the card does not open\n");
                free(card_pixels); free(soft_pixels); free(rdram);
                return 1;
            }
            /* Forced *after* opening: the count is detected during the open, and
               forcing before it would be overwritten by the detection. */
            if (single_tmu) { dkr_glide_backend_force_single_tmu(1); }
            card_tmus = dkr_glide_backend_tmu_count();
            say("  card opened with %d texture unit(s)%s\n", card_tmus,
                   single_tmu ? " (forced to one)" : "");

            run_capture(&card, &h, rdram, card_tmus, &cc);
            say_counts("card", &cc);

            if (dkr_glide_read_framebuffer(card_pixels,
                                           (int)(h.screen_w * h.screen_h),
                                           &cw, &ch) <= 0) {
                fprintf(stderr, "replay: read-back impossible\n");
                card.close(card.self);
                free(card_pixels); free(soft_pixels); free(rdram);
                return 1;
            }
            card.close(card.self);

            if (!dkr_image_write_bmp(want_both ? "D:\\RPLCARD.BMP" : out_path,
                                     card_pixels, cw, ch)) {
                fprintf(stderr, "replay: cannot write the card's image\n");
            } else {
                say("  wrote %s\n",
                       want_both ? "D:\\RPLCARD.BMP" : out_path);
            }

            if (!want_both) {
                free(card_pixels); free(rdram);
                return 0;
            }

            /* --- The comparison ---------------------------------------------- */
            if (!soft_pixels) {
                fprintf(stderr, "replay: the oracle's image was not kept\n");
                status = 1;
            } else if (sw != cw || sh != ch) {
                fprintf(stderr, "replay: oracle %dx%d, card %dx%d\n",
                        sw, sh, cw, ch);
                status = 1;
            } else {
                dkr_image_metrics m;

                /* The counts first. An image difference between two chains that
                   did not emit the same geometry says nothing about rendering. */
                if (sc.emitted != cc.emitted || sc.triangles != cc.triangles) {
                    say("  DIVERGENCE: the two paths did not emit the same"
                           " geometry — the images below mean nothing\n");
                    status = 1;
                }

                dkr_image_compare(soft_pixels, card_pixels, sw, sh, &m);
                say("  painted surface: oracle %ld, card %ld (%ld%% gap)\n",
                       m.painted_ref, m.painted_got,
                       m.painted_ref
                         ? (100 * (m.painted_got - m.painted_ref) / m.painted_ref)
                         : 0L);
                say("  frankly different: %ld of %ld (%ld per million)\n",
                       m.differ, m.total, dkr_image_per_million(m.differ, m.total));
                say("  of which on an edge: %ld\n", m.differ_edge);
                say("  worst per-channel gap: %d\n", m.max_gap);
                if (m.worst) {
                    const size_t k = (size_t)m.worst_y * (size_t)sw +
                                     (size_t)m.worst_x;
                    say("  worst off-edge: %d at (%d,%d)"
                           "  oracle 0x%06X  card 0x%06X\n",
                           m.worst, m.worst_x, m.worst_y,
                           dkr_image_to565(soft_pixels[k] & 0x00FFFFFFu),
                           card_pixels[k] & 0x00FFFFFFu);
                }

                /* The map is written on the machine that made both images, so
                   that a divergence can be looked at without waiting for a
                   transfer — and it is 900 KiB, which the transfer disk has. */
                {
                    unsigned *diff = (unsigned *)malloc((size_t)sw * (size_t)sh *
                                                        sizeof(unsigned));
                    if (diff) {
                        dkr_image_diff_map(soft_pixels, card_pixels, sw, sh, diff);
                        if (dkr_image_write_bmp("D:\\RPLDIFF.BMP", diff, sw, sh)) {
                            say("  wrote D:\\RPLDIFF.BMP\n");
                        }
                        free(diff);
                    }
                }
            }
            free(card_pixels);
        }
#endif /* DKR_HAVE_GLIDE */
        free(soft_pixels);
    }

    free(rdram);
    if (g_log) { fclose(g_log); }
    return status;
}
