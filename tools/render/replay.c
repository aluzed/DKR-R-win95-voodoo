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
#include "render/combiner.h"
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
    unsigned long culled;
    unsigned long clipped;
    unsigned long textures;
    unsigned long resident, reused;
    /* The stride check: see `stride_mismatch` in `f3ddkr.h`. */
    unsigned long dxt_disagrees, dxt_disagrees_texels;
    unsigned short dxt_first[8][4];
    unsigned int  dxt_first_n;
    unsigned long image_wider, image_wider_texels;
    unsigned short image_wider_first[8][4];
    unsigned int  image_wider_first_n;
    unsigned long size_mismatch;
    unsigned short size_first[24][6];
    unsigned int  size_first_n;
    unsigned long tile_origin_nonzero;
    unsigned short tile_origin_first[8][4];
    unsigned int  tile_origin_first_n;
    /* Draws emitted with fog in force. Zero for the whole of this port's life,
       and nothing printed it - so "fog is off" and "fog is on and does nothing"
       were indistinguishable from the report. */
    unsigned long fogged, secondary;
    unsigned long geom_batches, geom_depth_disagrees, geom_cull_disagrees;
    unsigned long stride_checked, stride_mismatch, stride_mismatch_texels;
    unsigned short stride_first[8][4];
    unsigned int  stride_first_n;
    /* **The range of depths the decoder hands the backend.**
       The running game prints exactly this, every few hundred frames, from the
       same two fields of the same decoder state. Printing it here too is what
       makes the two comparable: a screen that renders on the card in replay and
       black in the game is either being handed different depths or it is not,
       and until both sides report the number that is a guess. */
    float oow_min, oow_max;
} replay_counts;

static void take_counts(const dkr_f3d_context *ctx, replay_counts *c)
{
    c->commands  = ctx->state.commands;
    c->triangles = ctx->state.triangles;
    c->emitted   = ctx->state.emitted;
    c->rejects   = ctx->state.rejects[0] + ctx->state.rejects[1] +
                   ctx->state.rejects[2] + ctx->state.rejects[3] +
                   ctx->state.rejects[4];
    c->culled    = ctx->state.culled;
    c->clipped   = ctx->state.clipped_away;
    c->textures  = ctx->state.textures_loaded;
    c->fogged    = ctx->state.emitted_fogged;
    c->oow_min   = ctx->state.oow_min;
    c->oow_max   = ctx->state.oow_max;
    c->secondary = ctx->state.emitted_secondary;
    c->geom_batches         = ctx->state.geom_batches;
    c->geom_depth_disagrees = ctx->state.geom_depth_disagrees;
    c->geom_cull_disagrees  = ctx->state.geom_cull_disagrees;
    c->resident  = ctx->state.textures_resident;
    c->reused    = ctx->state.textures_reused;
    c->dxt_disagrees         = ctx->state.dxt_disagrees;
    c->dxt_disagrees_texels  = ctx->state.dxt_disagrees_texels;
    c->dxt_first_n           = ctx->state.dxt_first_n;
    memcpy(c->dxt_first, ctx->state.dxt_first, sizeof(c->dxt_first));
    c->image_wider           = ctx->state.image_wider;
    c->image_wider_texels    = ctx->state.image_wider_texels;
    c->image_wider_first_n   = ctx->state.image_wider_first_n;
    memcpy(c->image_wider_first, ctx->state.image_wider_first,
           sizeof(c->image_wider_first));
    c->size_mismatch         = ctx->state.size_mismatch;
    c->size_first_n          = ctx->state.size_first_n;
    memcpy(c->size_first, ctx->state.size_first, sizeof(c->size_first));
    c->tile_origin_nonzero   = ctx->state.tile_origin_nonzero;
    c->tile_origin_first_n   = ctx->state.tile_origin_first_n;
    memcpy(c->tile_origin_first, ctx->state.tile_origin_first,
           sizeof(c->tile_origin_first));
    c->stride_checked        = ctx->state.stride_checked;
    c->stride_mismatch       = ctx->state.stride_mismatch;
    c->stride_mismatch_texels = ctx->state.stride_mismatch_texels;
    c->stride_first_n        = ctx->state.stride_first_n;
    memcpy(c->stride_first, ctx->state.stride_first, sizeof(c->stride_first));
}

static int g_want_trace;
static int g_no_odd_row_swap;
static int g_no_tile_texel_size;

/* One line of the decoder's trace. Straight to standard output: the trace is
   thousands of lines and the point of it is to be piped into `grep`. */
static void trace_line(void *user, const char *line)
{
    (void)user;
    fputs(line, stdout);
    fputc('\n', stdout);
}

static void say_counts(const char *who, const replay_counts *c)
{
    /* The triangles are **accounted for**, not merely counted. `tri` against
       `emitted` left 194 of 293 unexplained on the copyright screen, and an
       unexplained gap of two thirds is indistinguishable from geometry silently
       going missing. Culled and clipped are the two legitimate fates; `lost` is
       what remains, and it should be zero. */
    const unsigned long accounted = c->emitted + c->culled + c->clipped +
                                    c->rejects;
    const unsigned long lost = (c->triangles > accounted)
                                 ? c->triangles - accounted : 0UL;
    say("  %-8s cmd=%lu tri=%lu emitted=%lu culled=%lu clipped=%lu rejects=%lu"
        " lost=%lu textures=%lu (resident=%lu reused=%lu) fogged=%lu\n",
        who, c->commands, c->triangles, c->emitted, c->culled, c->clipped,
        c->rejects, lost, c->textures, c->resident, c->reused, c->fogged);
    /* Two sources for one fact. Silent when they agree, because a line of zeroes
       on every run is a line nobody reads. */
    /* Draws aimed at a colour image other than the first one named. The port
       redirects them all into the one frame buffer, having no render target;
       silent when the list only ever names one, as most do. */
    if (c->secondary) {
        say("           draws aimed at a second render target: %lu\n",
            c->secondary);
    }
    if (c->oow_max > c->oow_min) {
        say("           oow=[%d..%d]/1000000\n",
            (int)(c->oow_min * 1000000.0f), (int)(c->oow_max * 1000000.0f));
    }
    if (c->geom_depth_disagrees || c->geom_cull_disagrees) {
        say("           geometry mode disagrees with the derived state:"
            " depth on %lu of %lu batches, cull on %lu\n",
            c->geom_depth_disagrees, c->geom_batches, c->geom_cull_disagrees);
    } else {
        say("           geometry mode agrees with the derived state on all %lu"
            " batches\n", c->geom_batches);
    }
    /* **The tile's row stride against the one the conversion assumes.** Printed
       beside the counts and not behind a switch: a texture read at the wrong
       stride comes out sheared, and this is the number that says whether any
       is. Silent when nothing disagrees, because a line of zeroes on every
       scene is a line nobody reads. */
    if (c->dxt_disagrees != 0UL) {
        unsigned int i;
        say("  %-8s dxt: %lu of %lu conversions have a LoadBlock row length the"
            " conversion does not use (%lu texels)\n",
            "", c->dxt_disagrees, c->stride_checked, c->dxt_disagrees_texels);
        for (i = 0; i < c->dxt_first_n; i++) {
            say("             dxt says %u bytes a row, the conversion uses %u"
                " (%ux%u)\n",
                (unsigned)c->dxt_first[i][0], (unsigned)c->dxt_first[i][1],
                (unsigned)c->dxt_first[i][2], (unsigned)c->dxt_first[i][3]);
        }
    }
    if (c->image_wider != 0UL) {
        static const char *const sz2[4] = { "4b", "8b", "16b", "32b" };
        unsigned int i;
        say("  %-8s image width: %lu of %lu conversions read a tile narrower than"
            " its image (%lu texels)\n",
            "", c->image_wider, c->stride_checked, c->image_wider_texels);
        for (i = 0; i < c->image_wider_first_n; i++) {
            say("             image %u wide, tile %ux%u at %s\n",
                (unsigned)c->image_wider_first[i][0],
                (unsigned)c->image_wider_first[i][1],
                (unsigned)c->image_wider_first[i][2],
                sz2[c->image_wider_first[i][3] & 3u]);
        }
    }
    if (c->size_mismatch != 0UL) {
        static const char *const sz[4] = { "4b", "8b", "16b", "32b" };
        unsigned int i;
        say("  %-8s texel size: %lu of %lu conversions read a size the tile does"
            " not declare\n", "", c->size_mismatch, c->stride_checked);
        for (i = 0; i < c->size_first_n; i++) {
            {
                static const char *const fm[8] = { "RGBA","YUV","CI","IA","I","?","?","?" };
                say("             tile says %s/%s, the image says %s/%s, for %ux%u\n",
                    fm[c->size_first[i][4] & 7u], sz[c->size_first[i][0] & 3u],
                    fm[c->size_first[i][5] & 7u], sz[c->size_first[i][1] & 3u],
                    (unsigned)c->size_first[i][2], (unsigned)c->size_first[i][3]);
            }
        }
    }
    if (c->tile_origin_nonzero != 0UL) {
        unsigned int i;
        say("  %-8s tile origin: %lu of %lu conversions come from a tile that is"
            " not at the image's corner\n",
            "", c->tile_origin_nonzero, c->stride_checked);
        for (i = 0; i < c->tile_origin_first_n; i++) {
            say("             uls=%u ult=%u for a %ux%u tile\n",
                (unsigned)c->tile_origin_first[i][0],
                (unsigned)c->tile_origin_first[i][1],
                (unsigned)c->tile_origin_first[i][2],
                (unsigned)c->tile_origin_first[i][3]);
        }
    }
    if (c->stride_mismatch != 0UL) {
        unsigned int i;
        say("  %-8s stride: %lu of %lu textures disagree with their tile line"
            " (%lu texels)\n",
            "", c->stride_mismatch, c->stride_checked,
            c->stride_mismatch_texels);
        for (i = 0; i < c->stride_first_n; i++) {
            say("             line=%u bytes, a row of %u texels needs %u"
                " (%ux%u)\n",
                (unsigned)c->stride_first[i][0], (unsigned)c->stride_first[i][2],
                (unsigned)c->stride_first[i][1], (unsigned)c->stride_first[i][2],
                (unsigned)c->stride_first[i][3]);
        }
    }
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
                        unsigned char *rdram, int tmus, int no_cull,
                        int no_alpha, replay_counts *out)
{
    dkr_f3d_init(&g_ctx, rdram, h->rdram_bytes, bk);
    g_ctx.rdram_native  = (unsigned char)(h->rdram_native ? 1 : 0);
    g_ctx.screen_width  = (short)h->screen_w;
    g_ctx.screen_height = (short)h->screen_h;
    g_ctx.tmu_count     = (unsigned char)tmus;
    g_ctx.no_cull       = (unsigned char)(no_cull ? 1 : 0);
    g_ctx.no_alpha_test = (unsigned char)(no_alpha ? 1 : 0);
    g_ctx.no_odd_row_swap = (unsigned char)(g_no_odd_row_swap ? 1 : 0);
    g_ctx.no_tile_texel_size =
        (unsigned char)(g_no_tile_texel_size ? 1 : 0);
    /* **The command trace, on the bench.** The decoder has carried a trace hook
       since it was written and only the game ever wired it, behind
       `DKR_TRACE_LIST`. So a question about which `G_SETTILE` a conversion
       belongs to could be asked on the machine, four minutes a run, and not here
       on a capture. It is the same hook and three lines. */
    if (g_want_trace) {
        g_ctx.trace = trace_line;
        g_ctx.trace_user = 0;
    }

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

/* What the oracle recorded at the probed pixel. Printed in the terms the state
   is written in, not as a hex dump: the point of the probe is to turn a divergent
   pixel into something one can reason about. */
static const char *combine_name(int m)
{
    switch (m) {
    case 0: return "SHADE";
    case 1: return "TEXTURE";
    case 2: return "TEX*SHADE";
    case 3: return "TEX*SHADE+A";
    case 4: return "TEX*CONST";
    default: return "?";
    }
}

static const char *blend_name(int m)
{
    switch (m) {
    case 0: return "opaque";
    case 1: return "alpha";
    case 2: return "additive";
    default: return "?";
    }
}

/* The oracle refuses a fragment for one of three reasons, and which one it was
   is the first thing worth knowing when the card refuses a different number. */
static const char *reject_name(unsigned char why)
{
    switch (why) {
    case DKR_PROBE_KEPT:    return "painted";
    case DKR_PROBE_SCISSOR: return "scissor";
    case DKR_PROBE_ALPHA:   return "alpha";
    case DKR_PROBE_DEPTH:   return "depth";
    default:                return "?";
    }
}

#ifdef DKR_HAVE_GLIDE
/* Which physical pass a reading was taken after. A multipass configuration is
   four draws on the card and one in the oracle. */
static const char *card_pass_name(unsigned char p)
{
    switch (p) {
    case DKR_CARD_PASS_DRAW:    return "draw";
    case DKR_CARD_PASS_PRE_A:   return "pre-A";
    case DKR_CARD_PASS_PRE_B:   return "pre-B";
    case DKR_CARD_PASS_SHADE_A: return "shade-A";
    case DKR_CARD_PASS_SHADE_B: return "shade-B";
    case DKR_CARD_PASS_TEXEL_A: return "texel-A";
    case DKR_CARD_PASS_TEXEL_B: return "texel-B";
    case DKR_CARD_PASS_ENV:     return "env";
    default:                    return "?";
    }
}

/* The card's half of the same question. Printed in the same shape as
   `say_probe`, so that the two logs read side by side: which draw changed the
   pixel, from what to what, and whether the configuration's extra passes ran. */
static void say_card_watch(int x, int y)
{
    const dkr_card_watch_entry *log = 0;
    int kept = 0, i;
    const int seen = dkr_glide_backend_watch_result(&log, &kept);

    if (seen == 0) {
        say("  card probe (%d,%d): no draw changed this pixel\n", x, y);
        return;
    }
    say("  card probe (%d,%d): %d draw(s) changed it%s\n", x, y, seen,
        (kept < seen) ? ", the first few:" : ":");
    for (i = 0; i < kept; i++) {
        say("    batch %-5lu %-9s %-9s 0x%06X -> 0x%06X  recipe=%-3u"
            " blend=%u depth=%u alpha=%u%s%s\n",
            log[i].batch,
            log[i].covered ? "covers" : "elsewhere",
            card_pass_name(log[i].pass),
            log[i].before & 0x00FFFFFFu, log[i].after & 0x00FFFFFFu,
            (unsigned)log[i].recipe,
            (unsigned)log[i].blend, (unsigned)log[i].depth,
            (unsigned)log[i].alpha_test,
            (log[i].passes & 1u) ? "  pre-pass" : "",
            (log[i].passes & 2u) ? "  second pass" : "");
        say("                    clip %d,%d..%d,%d  %s\n",
            (int)log[i].clip[0], (int)log[i].clip[1],
            (int)log[i].clip[2], (int)log[i].clip[3],
            (x >= (int)log[i].clip[0] && x < (int)log[i].clip[2] &&
             y >= (int)log[i].clip[1] && y < (int)log[i].clip[3])
                ? "the point is inside it"
                : "** the point is OUTSIDE it **");
        if (log[i].covered && log[i].pass == DKR_CARD_PASS_DRAW) {
            int k;
            for (k = 0; k < 3; k++) {
                say("                    v%d  x=%.2f y=%.2f oow=%.6f"
                    "  rgba=%.0f,%.0f,%.0f,%.0f\n", k,
                    log[i].tri[k][0], log[i].tri[k][1], log[i].tri[k][2],
                    log[i].tri[k][3], log[i].tri[k][4],
                    log[i].tri[k][5], log[i].tri[k][6]);
            }
        }
    }
}
#endif /* DKR_HAVE_GLIDE */

static void say_probe(int x, int y)
{
    const dkr_probe_write *log = 0;
    int kept = 0, i;
    const int writes = dkr_software_probe_result(&log, &kept);

    if (writes == 0) {
        say("  probe (%d,%d): nothing drew this pixel\n", x, y);
        return;
    }
    say("  probe (%d,%d): %d draw(s)%s\n", x, y, writes,
        (kept < writes) ? ", the first few:" : ":");
    for (i = 0; i < kept; i++) {
        const dkr_render_state *st = &log[i].state;
        say("    %2d  %-7s 0x%06X -> 0x%06X  z=%.6f buf=%.6f  st=%.3f,%.3f  %-11s"
            " const=0x%08X env=0x%08X prim=0x%08X ascale=%-3u recipe=%d"
            "  blend=%-8s tex=%lu/%lu  alpha=%u/%u fog=%u depth=%u\n",
            i + 1, reject_name(log[i].rejected),
            log[i].before & 0x00FFFFFFu, log[i].after & 0x00FFFFFFu,
            log[i].z, log[i].depth,
            log[i].s, log[i].t,
            combine_name((int)st->combine), st->constant_color,
            st->env_color, st->prim_color,
            (unsigned)st->alpha_scale,
            (int)st->recipe, blend_name((int)st->blend),
            (unsigned long)st->texture, (unsigned long)st->texture1,
            (unsigned)st->alpha_test, (unsigned)st->alpha_reference,
            (unsigned)st->fog_enabled, (unsigned)st->depth);
    }
}

/* Writes every texture the oracle holds, as a BMP, named by its slot and its key.
   The question after "what drew this pixel" is "with what", and a quad that comes
   out one flat colour has either the wrong texture or degenerate coordinates --
   which only looking at the texture separates. */
static void dump_textures(const char *dir)
{
    int slot, written = 0, unpainted = 0;
    for (slot = 0; slot < 256; slot++) {
        int w = 0, h = 0;
        unsigned long long key = 0;
        const unsigned *texels = dkr_software_texture(slot, &w, &h, &key);
        char path[512];
        const unsigned long painted = dkr_software_texture_pixels(slot);
        const unsigned long tris = dkr_software_texture_triangles(slot);
        if (!texels || w <= 0 || h <= 0) { continue; }
        /* The painted count is in the name, so that a directory listing already
           answers "which of these reached the screen". A texture uploaded and
           never sampled is an object missing from the image, and no upload
           counter can say that. */
        /* **Written at the tile's true size, not the padded one.**
         *
         * What reaches the card is rounded up to a power of two and then rounded
         * again to stay inside its 8:1 aspect limit, and the padding *repeats*
         * the pattern rather than zeroing it. A 248x11 font atlas therefore
         * arrives as 256x32 showing its alphabet three times down and once and a
         * bit across — which looks precisely like a corrupt texture, and cost an
         * afternoon being read as one.
         *
         * The true dimensions are in the key, which `f3ddkr.c` composes as
         * `address<<24 ^ format<<20 ^ size<<18 ^ width<<9 ^ height`. Bits 0..17
         * carry width and height and nothing else overlaps them. The backend is
         * told the key is opaque and it keeps to that; this is a diagnostic
         * reading the decoder's own format on purpose, and it is spelt out here
         * so the next person to change that composition finds this.
         *
         * The crop is the top-left corner because that is where the unpadded
         * content sits, padding only ever having been appended. */
        {
            const int kw = (int)((key >> 9) & 0x1FFu);
            const int kh = (int)(key & 0x1FFu);
            const int cw = (kw > 0 && kw <= w) ? kw : w;
            const int ch = (kh > 0 && kh <= h) ? kh : h;
            /* The RDRAM address, not just the low half of the key: the key is
               `address<<24 ^ ...` and every other field sits below bit 24, so
               the shift recovers the address exactly. Without it a texture in
               the dump cannot be found again in the capture, which is the one
               thing one wants of a texture in a dump. */
            sprintf(path, "%s/tex%03d_%dx%d_of_%dx%d_at%06lX_%08lX_%lutri_%lupx.bmp",
                    dir, slot + 1, cw, ch, w, h,
                    (unsigned long)((key >> 24) & 0xFFFFFFu),
                    (unsigned long)(key & 0xFFFFFFFFu), tris, painted);
            if (cw == w && ch == h) {
                if (dkr_image_write_bmp(path, texels, w, h)) { written++; }
            } else {
                static unsigned crop[1024 * 1024];
                int y;
                if ((size_t)cw * (size_t)ch <= sizeof(crop) / sizeof(crop[0])) {
                    for (y = 0; y < ch; y++) {
                        memcpy(&crop[(size_t)y * (size_t)cw],
                               &texels[(size_t)y * (size_t)w],
                               (size_t)cw * sizeof(unsigned));
                    }
                    if (dkr_image_write_bmp(path, crop, cw, ch)) { written++; }
                }
            }
        }
        if (painted == 0u) { unpainted++; }
    }
    say("  textures: %d written to %s, %d of them painted nothing\n",
        written, dir, unpainted);
}

/* How much of the frame each category of combiner painted. The card renders
   everything that is not `DKR_CC_EXACT` through `apply_combine`'s four
   single-pass modes, so the three other columns are the share of the image it is
   approximating -- and, doubled, the fill a second pass would cost. */
static void say_categories(void)
{
    static const char *const name[5] = {
        "exact", "multipass", "approximate", "two-texel", "uncatalogued"
    };
    unsigned long total = 0;
    int i;
    for (i = 0; i < 5; i++) { total += dkr_software_category_pixels(i); }
    if (total == 0) { return; }
    /* **Writes, not distinct pixels**, and the difference is the point: a pixel
       written five times costs five times the fill, and fill is what limits a
       Voodoo 2 at 640x480. A count of distinct pixels would read like a coverage
       figure and be the wrong number for the only question it is asked. */
    say("  fill: %lu writes over %d pixels\n", total, 640 * 480);
    for (i = 0; i < 5; i++) {
        const unsigned long n = dkr_software_category_pixels(i);
        if (n == 0u) { continue; }
        say("    %-13s %8lu  %ld ppm\n", name[i], n,
            dkr_image_per_million((long)n, (long)total));
    }
}

/* The same fill, by catalogue entry, worst first. Three lines of this turn "90 %
   of the frame is multipass" into a list of configurations to work on. */
static void say_recipes(void)
{
    unsigned long n[65];
    unsigned long total = 0;
    int count, i, shown;

    count = dkr_cc_table_count();
    if (count > 64) { count = 64; }
    for (i = 0; i <= count; i++) {
        n[i] = dkr_software_recipe_pixels(i);
        total += n[i];
    }
    if (total == 0u) { return; }

    /* Copied out and struck off here rather than sorted: six passes over
       thirty entries is nothing, and it keeps the backend's counters read-only,
       which is what makes them safe to print twice. */
    {
        const unsigned long two = dkr_software_second_cycle_pixels(0);
        const unsigned long eff = dkr_software_second_cycle_pixels(1);
        if (two > 0u) {
            const unsigned long al = dkr_software_second_cycle_pixels(2);
            say("    two-cycle %8lu  of which the second cycle changes the "
                "colour: %lu (%ld ppm)  the alpha: %lu (%ld ppm)\n",
                two, eff, dkr_image_per_million((long)eff, (long)total),
                al, dkr_image_per_million((long)al, (long)total));
        }
    }
    {
        /* Which second cycles actually do something, worst first. The card
           reproduces one shape; this says what the next one would be worth. */
        unsigned long e[65];
        int j, shown2;
        int any = 0;
        for (j = 0; j <= count; j++) {
            e[j] = dkr_software_second_cycle_by_recipe(j);
            if (e[j] > 0u) { any = 1; }
        }
        if (any) {
            say("  second cycles that change a pixel, worst first:\n");
            for (shown2 = 0; shown2 < 4; shown2++) {
                unsigned long best = 0;
                int best_j = -1;
                for (j = 0; j <= count; j++) {
                    if (e[j] > best) { best = e[j]; best_j = j; }
                }
                if (best_j < 0) { break; }
                {
                    const dkr_cc_entry *ent =
                        (best_j > 0) ? dkr_cc_table_at(best_j - 1) : 0;
                    say("    %8lu  %6ld ppm  %s%s%s\n", best,
                        dkr_image_per_million((long)best, (long)total),
                        ent ? ent->name : "(no catalogue entry)",
                        (ent && ent->name_cycle2) ? " + " : "",
                        (ent && ent->name_cycle2) ? ent->name_cycle2 : "");
                }
                e[best_j] = 0u;
            }
        }
    }
    say("  fill by configuration, worst first:\n");
    for (shown = 0; shown < 6; shown++) {
        unsigned long best = 0;
        int best_i = -1;
        for (i = 0; i <= count; i++) {
            if (n[i] > best) { best = n[i]; best_i = i; }
        }
        if (best_i < 0) { break; }
        {
            const dkr_cc_entry *e = (best_i > 0) ? dkr_cc_table_at(best_i - 1) : 0;
            const unsigned long op = dkr_software_recipe_opaque_pixels(best_i);
            say("    %8lu  %6ld ppm  opaque %3ld%%  %s%s%s\n", best,
                dkr_image_per_million((long)best, (long)total),
                best ? (long)((op * 100u) / best) : 0L,
                e ? e->name : "(no catalogue entry)",
                (e && e->name_cycle2) ? " + " : "",
                (e && e->name_cycle2) ? e->name_cycle2 : "");
        }
        n[best_i] = 0u;
    }
}

/* Prints one catalogue entry in full: both cycles as arithmetic, the category and
   the Glide setup. It reads the mux with `dkr_cc_input_name`, which mirrors the
   evaluator's own position tables -- the point being that a reader and a
   rasteriser cannot disagree about what an entry says. */
static void say_recipe_entry(int index)
{
    const dkr_cc_entry *e;
    int cyc;

    if (index < 1 || index > dkr_cc_table_count()) {
        fprintf(stderr, "replay: recipe %d is outside 1..%d\n", index,
                dkr_cc_table_count());
        return;
    }
    e = dkr_cc_table_at(index - 1);
    say("recipe %d: %s%s%s\n", index, e->name,
        e->name_cycle2 ? " + " : "", e->name_cycle2 ? e->name_cycle2 : "");
    say("  cycles=%d  category=%s  constant=%d  uses_texture=%d\n",
        (e->cycle == DKR_CYCLE_2) ? 2 : 1, dkr_cc_category_text(e->category),
        (int)e->constant, (int)e->setup.uses_texture);
    for (cyc = 0; cyc <= ((e->cycle == DKR_CYCLE_2) ? 1 : 0); cyc++) {
        say("  cycle %d  rgb   = (%s - %s) * %s + %s\n", cyc + 1,
            dkr_cc_input_name(0, 0, e->rgb[cyc].a),
            dkr_cc_input_name(1, 0, e->rgb[cyc].b),
            dkr_cc_input_name(2, 0, e->rgb[cyc].c),
            dkr_cc_input_name(3, 0, e->rgb[cyc].d));
        say("           alpha = (%s - %s) * %s + %s\n",
            dkr_cc_input_name(0, 1, e->alpha[cyc].a),
            dkr_cc_input_name(1, 1, e->alpha[cyc].b),
            dkr_cc_input_name(2, 1, e->alpha[cyc].c),
            dkr_cc_input_name(3, 1, e->alpha[cyc].d));
    }
    say("  glide setup  cc=%d/%d/%d/%d  ac=%d/%d/%d/%d  tc=%d/%d\n",
        e->setup.cc_function, e->setup.cc_factor, e->setup.cc_local,
        e->setup.cc_other, e->setup.ac_function, e->setup.ac_factor,
        e->setup.ac_local, e->setup.ac_other, e->setup.tc_function,
        e->setup.tc_factor);
    say("  note: %s\n", e->note);
}

static void usage(const char *me)
{
    fprintf(stderr,
            "usage: %s [--card|--both] [--single-tmu] [--log file] [--trace]\n"
            "          [--no-odd-row-swap] [--no-tile-texel-size]\n"
            "          [--probe X,Y] [--dump-textures dir] [--no-cull]\n"
            "          [--recipe-map file] [--texel-factor-one] [--no-multipass]\n"
            "          capture.bin [out.bmp]\n"
            "       %s --recipe N          print one catalogue entry and stop\n",
            me, me);
}

int main(int argc, char **argv)
{
    dkr_capture_header h;
    unsigned char *rdram = 0;
    const char *cap_path = 0, *out_path = 0, *log_path = 0;
    const char *dump_dir = 0, *map_path = 0;
    int want_card = 0, want_both = 0, single_tmu = 0;
    int probe_on = 0, probe_x = -1, probe_y = -1;
    int no_cull = 0, factor_one = 0, no_alpha = 0, no_multipass = 0;
    int oracle_tmus = 1;
    int i, status = 0;

    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--card") == 0)       { want_card = 1; }
        else if (strcmp(argv[i], "--both") == 0)       { want_both = 1; }
        else if (strcmp(argv[i], "--single-tmu") == 0) { single_tmu = 1; }
        else if (strcmp(argv[i], "--no-cull") == 0)    { no_cull = 1; }
        else if (strcmp(argv[i], "--no-alpha-test") == 0) { no_alpha = 1; }
        else if (strcmp(argv[i], "--texel-factor-one") == 0) { factor_one = 1; }
        else if (strcmp(argv[i], "--no-multipass") == 0) { no_multipass = 1; }
        else if (strcmp(argv[i], "--trace") == 0) { g_want_trace = 1; }
        else if (strcmp(argv[i], "--no-odd-row-swap") == 0) { g_no_odd_row_swap = 1; }
        else if (strcmp(argv[i], "--no-tile-texel-size") == 0) {
            g_no_tile_texel_size = 1;
        }
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        }
        else if (strcmp(argv[i], "--recipe") == 0 && i + 1 < argc) {
            /* Reads no capture: it prints a table entry and stops. */
            say_recipe_entry(atoi(argv[++i]));
            return 0;
        }
        else if (strcmp(argv[i], "--recipe-map") == 0 && i + 1 < argc) {
            map_path = argv[++i];
        }
        else if (strcmp(argv[i], "--dump-textures") == 0 && i + 1 < argc) {
            dump_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--probe") == 0 && i + 1 < argc) {
            /* `--probe X,Y`. The comma keeps it one argument, so that a batch
               file on the target does not have to quote anything. */
            const char *v = argv[++i];
            char *end = 0;
            probe_x = (int)strtol(v, &end, 10);
            probe_y = (end && *end == ',') ? (int)strtol(end + 1, 0, 10) : -1;
            if (probe_x < 0 || probe_y < 0) {
                fprintf(stderr, "replay: --probe wants X,Y, got \"%s\"\n", v);
                return 2;
            }
            probe_on = 1;
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
    /* The host has no card to force to one unit, nor a combiner whose factor to
       choose. Both switches are the Windows 95 build's. */
    (void)single_tmu;
    (void)factor_one; (void)no_multipass;
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
            /* Armed before the frame, and only on the oracle: it is the one that
               rasterises in software and therefore the one that can say which
               state wrote a pixel. The card cannot be asked. */
            if (probe_on) { dkr_software_probe(probe_x, probe_y); }
            if (!soft.open(soft.self, (int)h.screen_w, (int)h.screen_h)) {
                fprintf(stderr, "replay: the rasteriser refused %ux%u\n",
                        h.screen_w, h.screen_h);
                free(rdram);
                return 1;
            }
            run_capture(&soft, &h, rdram, oracle_tmus, no_cull, no_alpha, &sc);
            say_counts("oracle", &sc);
            if (probe_on) { say_probe(probe_x, probe_y); }
            say_categories();
            say_recipes();
            /* Before the backend closes: it frees the textures on close. */
            if (dump_dir) { dump_textures(dump_dir); }
            if (map_path) {
                int mw = 0, mh = 0;
                const unsigned char *m = dkr_software_recipe_map(&mw, &mh);
                if (m && mw > 0 && mh > 0) {
                    /* Written as a plain byte per pixel, top row first. Not a BMP:
                       this is data for a script, and a viewer that made it look
                       like a picture would invite reading it as one. */
                    FILE *f = fopen(map_path, "wb");
                    if (f && fwrite(m, 1, (size_t)mw * (size_t)mh, f) ==
                              (size_t)mw * (size_t)mh) {
                        say("  recipe map: %s (%dx%d, one byte a pixel)\n",
                            map_path, mw, mh);
                    } else {
                        fprintf(stderr, "replay: cannot write %s\n", map_path);
                    }
                    if (f) { fclose(f); }
                }
            }

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
            if (factor_one) { dkr_glide_backend_texel_factor_one(1); }
            if (no_multipass) { dkr_glide_backend_extra_passes(0); }
            card_tmus = dkr_glide_backend_tmu_count();
            say("  card opened with %d texture unit(s)%s\n", card_tmus,
                   single_tmu ? " (forced to one)" : "");
            /* **Before anything is drawn**, because a Glide entry point that did
               not resolve makes every call through it a silent no-op, and every
               figure below it then describes a frame drawn with state nobody
               programmed. It is the first thing to read, not the last. */
            {
                unsigned long total = 0, missing = 0;
                const char *const *names = 0;
                unsigned int n = 0, i;
                dkr_glide_backend_symbols(&total, &missing, &names, &n);
                say("  glide entry points: %lu of %lu resolved\n",
                    total - missing, total);
                for (i = 0; i < n; i++) {
                    say("    MISSING %s - every call through it is skipped\n",
                        names[i]);
                }
                if (missing > n) {
                    say("    ... and %lu more\n", missing - n);
                }
            }

            if (probe_on) { dkr_glide_backend_watch(probe_x, probe_y); }
            run_capture(&card, &h, rdram, card_tmus, no_cull, no_alpha, &cc);
            say_counts("card", &cc);
            if (probe_on) { say_card_watch(probe_x, probe_y); }
            {
                unsigned long d = 0, id = 0, un = 0, bl = 0, sh = 0;
                dkr_glide_backend_pass2_stats(&d, &id, &un, &bl, &sh);
                unsigned long pd = 0, pa = 0, ta = 0;
                dkr_glide_backend_prepass_stats(&pd, &pa, &ta);
                say("  second pass: drawn=%lu (by-shade=%lu, approximate over a"
                    " blended first pass=%lu) skipped: identity=%lu"
                    " unsupported=%lu\n", d, sh, bl, id, un);
                say("  first cycle in two blends: drawn=%lu refused"
                    " (alpha test)=%lu | texel-alone: drawn=%lu"
                    " | whole cycle in three: drawn=%lu refused"
                    " (primitive not black)=%lu\n", pd, pa, ta,
                    dkr_glide_backend_shade_exact(),
                    dkr_glide_backend_shade_exact_prim());
                say("  alpha scale folded into the vertex: %lu draw(s)\n",
                    dkr_glide_backend_iterated_scaled());
                /* **What the card was actually asked to draw.** Every other
                   count on this line comes from the decoder, which is the same
                   code in both backends and therefore cannot disagree; this one
                   is the backend's own, and it is the only number that separates
                   "the draw was not made" from "the draw was made and the card
                   kept none of it". It existed and nothing printed it. */
                say("  triangles reaching the card: %lu\n",
                    dkr_glide_backend_triangle_count());
            }
            /* --- Why a texture did not make it ------------------------------- *
             *
             * The counts above say the two backends decoded a different number
             * of textures - 58 against 85 on the attract sequence - and a draw
             * whose texture is missing samples whatever the unit holds, which is
             * a wrong image with no message. Four causes end in the same
             * returned zero, and the backend has kept them apart all along; only
             * nothing printed them. */
            {
                const unsigned long aspect = dkr_glide_backend_upload_failure(0);
                const unsigned long zero   = dkr_glide_backend_upload_failure(1);
                const unsigned long table  = dkr_glide_backend_upload_failure(2);
                const unsigned long memory = dkr_glide_backend_upload_failure(3);
                const unsigned long back   = dkr_glide_backend_slots_reclaimed();
                if (aspect | zero | table | memory | back) {
                    say("  uploads refused: aspect=%lu zero=%lu table full=%lu"
                        " TMU memory=%lu | slots reclaimed=%lu\n",
                        aspect, zero, table, memory, back);
                } else {
                    say("  uploads refused: none\n");
                }
            }

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
