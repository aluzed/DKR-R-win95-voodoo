/* Replay the render state the game produces, and read the pixels back.
 *
 * The game's screen is black even though its six inputs all measured healthy:
 * vertex colour at 255, non-empty textures, texture bound, a combiner that
 * reads the texel, opaque blending, depth ruled out. Whatever is wrong is in
 * what actually gets programmed on the card, and instrumenting the game cannot
 * reach it -- there you observe what you send, never what comes out.
 *
 * This probe sets the same state, draws a known triangle, and reads the frame
 * buffer back.
 *
 * ## What makes it useful: bisection
 *
 * It does not set the state once but six times, degrading it step by step from
 * closest-to-the-game down to the simplest possible draw. The first case that
 * paints names the culprit, because it is the only thing that changed between
 * it and the previous case. A probe that only tested the full state would say
 * "black" and teach nothing the game had not already said.
 *
 * ## The second pass, and why the first is not enough
 *
 * The bisection establishes that the depth test erases the triangle. It does
 * **not** say why, because it never varies depth and `oow` independently: case 1
 * is depth on at `oow = 0.001`, case 2 is depth off at the same `oow`, case 3 is
 * depth off at `oow = 1`. Two causes survive that shape, and they call for
 * opposite fixes:
 *
 *   - the depth test is misconfigured -- direction, mask or clear -- in which
 *     case it erases at every distance;
 *   - far vertices saturate against the clear value. The backend uses a w buffer
 *     with `GR_CMP_LESS`, and `grBufferClear` clears to
 *     `GR_WDEPTHVALUE_FARTHEST`. If a large `w` encodes to that same maximum,
 *     `LESS` rejects it -- everything far disappears while near geometry still
 *     paints.
 *
 * The second is the exact mirror of a trap already recorded in `apply_depth`:
 * `GR_CMP_GREATER` on a w buffer blackens the screen because nothing can exceed
 * the cleared maximum. Same reasoning, applied to saturation rather than to
 * direction.
 *
 * So the sweep below holds the depth test **enabled** and moves `oow` alone
 * across the range the game really produces -- measured on the machine as
 * `oow` in [0.000096, 1.0], that is w from 1 to about 10,400. The two causes
 * give different answers and cannot be confused:
 *
 *   near paints, far does not  -> saturation, and the crossing point locates it
 *   nothing paints at all      -> the test itself
 *   everything paints          -> neither; the culprit is the combination, and
 *                                 that would be a third result worth having
 *
 * ## What the machine answered, on 17 August 2026
 *
 * The third one. All eleven rows paint, from w = 0 to w = 10,417, so neither
 * depth cause holds -- and the sweep row at `oow = 0.001` is byte for byte the
 * state of the bisection's case 1, which does not paint. Identical state, one
 * paints and the other does not.
 *
 * The repeat block, which now runs first, names what actually happens: **the
 * first draw of a run does not rasterise**, whatever its state. Pass 1 black,
 * passes 2 to 4 painted; and with the repeat placed ahead of it, the bisection's
 * case 1 paints where it used to stay black.
 *
 * So "the depth test erases the triangle", concluded on 16 August, is wrong.
 * Depth was simply the state attached to the draw that happened to be first.
 * Three explanations have been tested and refuted since -- a misconfigured depth
 * test, far-w saturation against the cleared FARTHEST, and a read-back lagging
 * the retrace-scheduled swap. The last was ruled out by reading the back buffer
 * before presenting: it is black on pass 1 as well.
 *
 * What remains to be found is why the first draw produces nothing. The next cut
 * is one variable wide, and it needs two runs because a cold start only happens
 * once: `TEST.EXE` draws the first triangle textured, `TEST.EXE notex` draws it
 * with no texture bound and the shade-only combiner. If the untextured first
 * draw paints, the texture upload that precedes it is implicated; if it does
 * not, the cause is in the context or in the first frame itself.
 */
#include "render/glide.h"
#include "render/backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <io.h>       /* _commit, _fileno - see `say` */
#include <windows.h>

static FILE *g_out;
static int   g_fails;

/* **`fflush` is not enough on this target, and an interrupted run proves it.**
 *
 * `fflush` hands the bytes to the OS; Windows 95's write-behind cache keeps
 * them, and the directory entry -- size *and* first cluster -- is only written at
 * close. A run that does not reach `fclose` therefore leaves a file of zero bytes
 * with no cluster allocated at all: not a truncated trace, no trace whatever.
 *
 * Measured on 17 August 2026: this probe's log came back empty, start cluster
 * zero, after every line had been `fflush`ed, because the machine was stopped
 * while it was still running. I first read that empty file as a hang inside the
 * back-buffer read -- it was not, and the next run showed the read returning its
 * 307,200 pixels. An absence of output says nothing about the thing being
 * measured, only about the measuring.
 *
 * `_commit` calls `FlushFileBuffers`, which forces the data and the directory
 * entry both. It costs a disk write per line, which for a witness printing a few
 * dozen lines is nothing next to being able to see where it stopped.
 *
 * The same reasoning, and the same call, as `dkr_diag_commit` in the game. */
static void say(const char *fmt, ...)
{
    char l[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(l, fmt, ap);
    va_end(ap);
    if (g_out) {
        fputs(l, g_out);
        fflush(g_out);
        _commit(_fileno(g_out));
    }
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) { g_fails++; }
}

static unsigned short g_tex[32 * 32];
static unsigned       g_px[640 * 480];

/* A hard checkerboard: no way to confuse "nothing drawn" with "drawn in a
   colour close to the background". */
static void build_tex(void)
{
    int x, y;
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            const int light = ((x / 4) + (y / 4)) & 1;
            g_tex[y * 32 + x] = (unsigned short)(light ? 0xFFFFu : 0x8421u);
        }
    }
}

/* A triangle covering most of the screen, carrying the colour and coordinates
   the game produces: shade at 255, s and t normalised then scaled into Glide's
   256-texel space, and an oow taken from the range measured on the machine. */
static void triangle(dkr_render_backend *bk, float oow)
{
    dkr_render_vertex v[3];
    const float xs[3] = {  40.0f, 600.0f,  40.0f };
    const float ys[3] = {  40.0f,  40.0f, 440.0f };
    const float ss[3] = {   0.0f,   1.0f,   0.0f };
    const float ts[3] = {   0.0f,   0.0f,   1.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 3; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
        v[i].oow = oow;
        v[i].z = 0.5f;
        v[i].ooz = 0.5f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i] * DKR_TEXCOORD_SCALE * oow;
        v[i].tmu[0][DKR_TMU_TOW] = ts[i] * DKR_TEXCOORD_SCALE * oow;
        v[i].tmu[0][DKR_TMU_OOW] = oow;
    }
    bk->draw_triangles(bk->self, v, 1);
}

/* **Two sampled points, not a count against an assumed background.**
 *
 * The first version of this probe counted pixels differing from the clear
 * colour it had asked for. It returned 307200 out of 307200 -- the whole
 * screen -- for all six cases, including cases that cannot possibly paint the
 * same thing. A saturated count does not mean "everything is painted": it means
 * the reference colour is wrong, the card not reading back in the format it was
 * assumed to.
 *
 * So we read two points and print their values: one inside the triangle, one
 * outside. Two concrete colours cannot saturate, and their difference is
 * exactly the question -- was the triangle painted. */
static unsigned sample(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

int main(int argc, char **argv)
{
    /* **The cold start happens once per run**, so the two halves of the
       first-draw question need two runs. `TEST.EXE notex` makes the very first
       draw an untextured one; without the argument it is textured, as the game's
       is. If the untextured first draw paints and the textured one does not, the
       texture upload that precedes it is implicated; if neither paints, the
       cause is in the context or in the first frame itself. */
    const int cold_no_texture = (argc > 1 && strcmp(argv[1], "notex") == 0);
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   d;
    dkr_texture_handle h;
    const int W = 640, H = 480;
    const unsigned BACKGROUND = 0x000040u;   /* dark blue, distinct from black */
    int rw = 0, rh = 0, got, i;
    int first_painted = -1;

    g_out = fopen("D:\\TEST.TXT", "w");
    say("the game's render state, replayed and read back\n\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("FAIL: cannot open Glide\n"); return 1; }

    /* **Is the instrument connected?**
     *
     * `dkr_glide_read_framebuffer` now calls `grSstIdle` before locking, to stop
     * it reading the frame before the one presented. If that symbol does not
     * resolve, the call is a silent no-op and every conclusion drawn from "the
     * idle changed nothing" would be a conclusion about a wire that was never
     * plugged in. So the probe says which it is, rather than leaving the reader
     * to assume. */
    say("  grSstIdle resolves: %s\n",
        dkr_glide_symbol("_grSstIdle@0") ? "yes" : "NO - read-back may lag one frame");

    build_tex();
    memset(&d, 0, sizeof(d));
    d.key = 0x9001ull;
    d.format = DKR_TEXFMT_RGBA5551;
    d.width = 32; d.height = 32;
    d.pixels = g_tex;
    d.size_bytes = sizeof(g_tex);

    /* Warm-up: the lesson from E05-S05, two frames thrown away. Measuring the
       first frame after opening a context measures a card that has not finished
       settling. */
    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, BACKGROUND);
        bk.set_state(bk.self, &st);
        bk.present(bk.self);
    }

    bk.begin_frame(bk.self, BACKGROUND);
    h = bk.texture_upload(bk.self, &d);
    check("the probe texture uploads", h != 0);

    /* --- The repeat: the same state drawn four times in a row ---------------
     *
     * **This runs first, and it has to.** The sweep below answered "everything
     * paints", including at `oow = 0.001` -- which is byte for byte the state of
     * the bisection's case 1, the one that stays black. Same combiner, same
     * blend, same depth mode, same texture, same triangle, same depth value. One
     * paints and the other does not, so the cause is not in the state: it is in
     * **where the draw sits in the run**.
     *
     * Case 1 is the first draw of the run, straight after the texture upload and
     * after two warm-up frames that ran with the depth test disabled. Reproducing
     * that means being first, which is why this block precedes the bisection and
     * not follows it -- placed at the end, after depth has been exercised a dozen
     * times, every pass paints and the measurement says nothing.
     *
     * Two mechanisms would explain it, and they are not the same defect:
     *
     *   - the first draw after the depth test is *enabled* fails, because the
     *     clear that preceded it ran while depth was still disabled and did not
     *     touch the depth buffer -- the triangle is then tested against whatever
     *     the buffer happened to hold;
     *   - the first draw of the run fails whatever the state, two warm-up frames
     *     not being enough on this card.
     *
     * Pass 1 against passes 2..4 separates them: the first mechanism recovers as
     * soon as one frame has been cleared with depth enabled, the second would
     * fail once and never again for any reason to do with depth.
     *
     * **What this costs, and it is worth saying**: the bisection below now runs
     * warm, so its case 1 may well paint where it used to stay black. That is not
     * the reproduction being lost -- pass 1 here *is* the reproduction, at the
     * same position and with the same state. The bisection keeps its value as the
     * record of what a warm run does. */
    {
        const int REPEATS = 4;
        int painted_at = -1, i2, n_painted = 0, read_ok2 = 0;

        say("\nthe game's state, drawn %d times in a row, from a cold start%s\n",
            REPEATS, cold_no_texture ? " (no texture bound)" : "");

        for (i2 = 0; i2 < REPEATS; i2++) {
            memset(&st, 0, sizeof(st));
            st.combine = cold_no_texture ? DKR_COMBINE_SHADE
                                         : DKR_COMBINE_TEXTURE_SHADE_ALPHA;
            st.blend   = DKR_BLEND_OPAQUE;
            st.depth   = DKR_DEPTH_TEST_AND_WRITE;
            st.cull    = DKR_CULL_NONE;
            st.filter  = DKR_FILTER_BILINEAR;
            st.wrap_s  = st.wrap_t = DKR_WRAP_REPEAT;
            st.texture = cold_no_texture ? 0 : h;

            bk.begin_frame(bk.self, BACKGROUND);
            bk.set_state(bk.self, &st);
            triangle(&bk, 0.001f);

            /* **Read the back buffer, before presenting.** This is the pass that
               separates "the draw did not paint" from "the read came too early":
               the back buffer is where the triangle has just been rasterised, and
               no swap timing enters into it. The front-buffer read is done as
               well, straight after the present, so the two sit side by side in
               the report -- if they disagree, the disagreement *is* the finding. */
            /* Bracketed, because the previous run hung here and said nothing.
               With the log committed per line, an interrupted run now names the
               call it stopped in instead of coming back empty. */
            say("  pass %d: locking the back buffer...\n", i2 + 1);
            got = dkr_glide_read_backbuffer(g_px, W * H, &rw, &rh);
            say("  pass %d: back buffer read, %d pixels\n", i2 + 1, got);
            if (got > 0) {
                const unsigned in_back  = sample(200, 200, rw);
                const unsigned out_back = sample(620, 460, rw);
                const int      ok_back  = (in_back != out_back);
                unsigned in_front = 0, out_front = 0;
                int ok_front = 0;

                bk.present(bk.self);
                if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
                    in_front  = sample(200, 200, rw);
                    out_front = sample(620, 460, rw);
                    ok_front  = (in_front != out_front);
                }

                read_ok2++;
                say("  pass %d: back in=0x%06X out=0x%06X %-11s"
                    " front in=0x%06X out=0x%06X %s\n",
                    i2 + 1, in_back, out_back, ok_back ? "<-- painted" : "",
                    in_front, out_front, ok_front ? "<-- painted" : "");
                if (ok_back) { n_painted++; if (painted_at < 0) { painted_at = i2; } }
            } else {
                bk.present(bk.self);
                say("  pass %d: read-back failed\n", i2 + 1);
            }
        }

        say("\n");
        if (n_painted == REPEATS) {
            say("  the back buffer paints on every pass, the first included.\n"
                "  If the front column is black on pass 1 and painted after, the\n"
                "  draw was always correct and it is the front-buffer read that\n"
                "  lags a frame behind the retrace-scheduled swap.\n");
        } else if (n_painted == 0) {
            say("  the back buffer never paints: the draw itself does not happen,\n"
                "  and the read-back timing is not the explanation\n");
        } else {
            say("  the back buffer is black on pass %d and paints from pass %d:\n"
                "  the draw really does depend on what precedes it\n",
                1, painted_at + 1);
        }
        check("every pass was read back", read_ok2 == REPEATS);
    }

    /* --- The bisection ------------------------------------------------------
     *
     * From closest-to-the-game down to the simplest draw. The first case that
     * paints names the culprit: it is the only thing that changed.
     *
     * Since the repeat above now precedes it, this runs warm -- see that block's
     * note. */
    {
        static const struct {
            const char       *name;
            dkr_combine_mode  combine;
            dkr_blend_mode    blend;
            dkr_depth_mode    depth;
            int               with_texture;
            float             oow;
        } CASES[] = {
            { "the game's state, as is",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_TEST_AND_WRITE, 1, 0.001f },
            { "without depth",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 0.001f },
            { "oow of 1 instead of 0.001",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 1.0f },
            { "texel-only combiner",
              DKR_COMBINE_TEXTURE, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 1.0f },
            { "no texture bound",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 0, 1.0f },
            { "vertex colour only",
              DKR_COMBINE_SHADE, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 0, 1.0f },
        };
        const int N = (int)(sizeof(CASES) / sizeof(CASES[0]));
        int c;

        say("\n%-40s %s\n", "case", "sampled colours");
        for (c = 0; c < N; c++) {
            memset(&st, 0, sizeof(st));
            st.combine = CASES[c].combine;
            st.blend   = CASES[c].blend;
            st.depth   = CASES[c].depth;
            st.cull    = DKR_CULL_NONE;
            st.filter  = DKR_FILTER_BILINEAR;
            st.wrap_s  = st.wrap_t = DKR_WRAP_REPEAT;
            st.texture = CASES[c].with_texture ? h : 0;

            bk.begin_frame(bk.self, BACKGROUND);
            bk.set_state(bk.self, &st);
            triangle(&bk, CASES[c].oow);
            bk.present(bk.self);

            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            if (got > 0) {
                /* 200,200 lies inside the triangle (40,40)-(600,40)-(40,440);
                   620,460 lies outside it, in the opposite corner. */
                const unsigned inside  = sample(200, 200, rw);
                const unsigned outside = sample(620, 460, rw);
                say("%-40s in=0x%06X out=0x%06X %s\n",
                    CASES[c].name, inside, outside,
                    (inside != outside) ? "<-- painted" : "");
                if (inside != outside && first_painted < 0) { first_painted = c; }
            } else {
                say("%-40s read-back failed\n", CASES[c].name);
            }
        }

        say("\n");
        if (first_painted < 0) {
            say("  nothing painted: the defect is upstream of the state\n");
        } else {
            say("  first case that paints: %s\n", CASES[first_painted].name);
            if (first_painted > 0) {
                say("  so the culprit is what that case removes\n");
            }
        }
        /* The check that stops this probe passing vacuously: at least one case
           must paint. Otherwise the state is not what is at fault but the
           context, the scissor window or the geometry -- and that has to be
           known before reading anything above. */
        check("at least one case paints something", first_painted >= 0);
    }

    /* --- The sweep: depth enabled, oow alone varying ------------------------
     *
     * See the header. This is what separates a misconfigured test from far
     * vertices saturating against the clear value. */
    {
        /* Spanning the range measured in the game, `oow` in [0.000096, 1.0],
           plus one value above 1 -- w below unity is not something the game
           produces, but it bounds the near end, and a sweep whose every value
           fails teaches less than one that brackets the crossing. */
        static const float OOW[] = {
            2.0f, 1.0f, 0.5f, 0.1f, 0.05f, 0.01f, 0.005f, 0.001f,
            0.0005f, 0.0001f, 0.000096f
        };
        const int N = (int)(sizeof(OOW) / sizeof(OOW[0]));
        int i, painted = 0, first_black = -1, last_painted = -1, read_ok = 0;

        say("\ndepth enabled, oow swept over the game's range\n");
        say("%-14s %-8s %s\n", "oow", "w", "sampled colours");

        for (i = 0; i < N; i++) {
            memset(&st, 0, sizeof(st));
            st.combine = DKR_COMBINE_TEXTURE_SHADE_ALPHA;
            st.blend   = DKR_BLEND_OPAQUE;
            st.depth   = DKR_DEPTH_TEST_AND_WRITE;
            st.cull    = DKR_CULL_NONE;
            st.filter  = DKR_FILTER_BILINEAR;
            st.wrap_s  = st.wrap_t = DKR_WRAP_REPEAT;
            st.texture = h;

            bk.begin_frame(bk.self, BACKGROUND);
            bk.set_state(bk.self, &st);
            triangle(&bk, OOW[i]);
            bk.present(bk.self);

            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            if (got > 0) {
                const unsigned inside  = sample(200, 200, rw);
                const unsigned outside = sample(620, 460, rw);
                const int      ok      = (inside != outside);
                say("%-14.6f %-8.0f in=0x%06X out=0x%06X %s\n",
                    (double)OOW[i], (double)(1.0f / OOW[i]), inside, outside,
                    ok ? "<-- painted" : "");
                read_ok++;
                if (ok) { painted++; last_painted = i; }
                else if (first_black < 0) { first_black = i; }
            } else {
                say("%-14.6f read-back failed\n", (double)OOW[i]);
            }
        }

        say("\n");
        if (painted == 0) {
            say("  nothing paints at any depth: the test itself is at fault\n"
                "  -- direction, mask or clear value, not the vertex depth\n");
        } else if (painted == N) {
            say("  everything paints: neither cause holds alone, and the\n"
                "  bisection's case 1 differs from these by something else\n");
        } else {
            say("  near paints, far does not: the w value saturates against\n"
                "  the cleared GR_WDEPTHVALUE_FARTHEST\n");
            say("  crossing between oow=%.6f (paints) and oow=%.6f (black)\n",
                (double)OOW[last_painted],
                (double)OOW[first_black > last_painted ? first_black
                                                       : last_painted]);
        }

        /* What can make this sweep vacuous is **not** a uniform result: all
           three shapes above are answers, and the uniform ones are the two that
           name a cause outright. What would be vacuous is a sweep whose rows
           were never read back -- then every row prints the same thing because
           nothing was measured, and "nothing paints at any depth" would be
           read as a verdict on the depth test. Guard the read-back, not the
           shape of the answer. */
        check("every row of the sweep was read back", read_ok == N);
    }

    bk.close(bk.self);
    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
