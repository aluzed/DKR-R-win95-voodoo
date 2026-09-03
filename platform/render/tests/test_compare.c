/* E09-S02 — the same scene in both backends, compared pixel by pixel.
 *
 * This is the reason everything above it exists. The E04-S08 software rasteriser
 * does not exist to draw the game: it exists to say what the card *should have*
 * drawn. As long as the Voodoo's frame buffer could not be read, the oracle had
 * nothing to compare against and remained an intention.
 *
 * The input is single and shared: `scene_synthetic.h`. Two copies of the scene
 * would drift, and the divergence would be blamed on the hardware.
 *
 * ## What this comparison can and cannot establish
 *
 * It cannot prove that the rendering is *right* — the game will be needed. It
 * establishes that two independent implementations of the same specification
 * agree, which is the only check available without the ROM and which catches the
 * most expensive class of errors: those where a stage declares itself satisfied
 * while producing something other than what it announces.
 *
 * ## A divergence is expected, and one must know which
 *
 * Three gaps are structural and signal nothing:
 *
 *   - **the card's 565 quantisation**, five bits of red and blue, six of green,
 *     against the rasteriser's eight;
 *   - **depth sorting**, z over [0,1] on one side, an encoded w buffer on the
 *     other — the two order the same way but do not quantise the same way;
 *   - **edges**, where a fill rule differing by half a pixel shifts a whole
 *     column of pixels.
 *
 * The chosen threshold therefore bears on the proportion of *frankly* different
 * pixels, and edge pixels are counted separately. A single tight threshold would
 * make the test uninterpretable: it would always fail, for a good reason, and
 * one would end up not reading it any more.
 *
 * **The metric itself moved out**, to `render/imagecmp.c`. It used to live here
 * and serve only this scene; E09-S02's replay compares an image made on this
 * machine against one made on the development machine, so the same measurement
 * now has to run in two programs built by two compilers. A second copy of it
 * would drift, and the drift would read as the card disagreeing with the oracle
 * -- which is what this file already says about keeping one copy of the *scene*,
 * and had not applied to the measurement of it.
 */
#include "render/software.h"
#include "render/glide.h"
#include "render/imagecmp.h"
#include "scene_synthetic.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480

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
    fputs(line, stdout);
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) { g_fails++; }
}

static unsigned char g_ram[DKR_SCENE_RAM];
static unsigned      g_soft[W * H];
static unsigned      g_card[W * H];

int main(void)
{
    dkr_render_backend soft, card;
    dkr_f3d_context    ctx;
    dkr_render_state   st;
    unsigned long      soft_emitted, card_emitted;

    g_out = fopen("D:\\COMPARE.TXT", "w");
    say("the same scene, two renderings, compared pixel by pixel\n\n");

    /* --- The reference rasteriser ------------------------------------------- */
    dkr_render_backend_software(&soft);
    if (!soft.open(soft.self, W, H)) {
        say("FAIL: the rasteriser does not open\n");
        return 1;
    }
    soft.begin_frame(soft.self, 0x000000);
    scene_state(&st);
    soft.set_state(soft.self, &st);
    scene_build(g_ram);
    scene_setup(&ctx, g_ram, &soft, W, H);
    (void)dkr_f3d_run(&ctx, 0);
    soft_emitted = ctx.state.emitted;
    {
        int w = 0, h = 0;
        const unsigned *fb = dkr_software_framebuffer(&w, &h);
        memcpy(g_soft, fb, sizeof(g_soft));
    }
    dkr_software_write_bmp("D:\\CMPSOFT.BMP");
    soft.close(soft.self);

    /* --- The card ------------------------------------------------------------ */
    dkr_render_backend_glide(&card);
    if (!card.open(card.self, W, H)) {
        say("FAIL: the card does not open\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    card.begin_frame(card.self, 0x000000);
    scene_state(&st);
    card.set_state(card.self, &st);
    scene_build(g_ram);
    scene_setup(&ctx, g_ram, &card, W, H);
    (void)dkr_f3d_run(&ctx, 0);
    card_emitted = ctx.state.emitted;
    card.present(card.self);
    {
        int w = 0, h = 0;
        if (dkr_glide_read_framebuffer(g_card, W * H, &w, &h) <= 0) {
            say("FAIL: read-back impossible\n");
            card.close(card.self);
            if (g_out) { fclose(g_out); }
            return 1;
        }
    }
    card.close(card.self);

    /* --- What the chain did on both sides ------------------------------------ *
     *
     * Before comparing the images, compare the counts. If the two backends did
     * not receive the same number of triangles, the image difference says
     * nothing about rendering any more: it says the chain is not deterministic,
     * which is a far more serious problem and one we want to see first. */
    say("  triangles emitted: software %lu, card %lu\n", soft_emitted, card_emitted);
    check("the chain emitted the same number of triangles on both sides",
          soft_emitted == card_emitted && soft_emitted > 0);

    /* --- The comparison ------------------------------------------------------ */
    {
        dkr_image_metrics m;

        dkr_image_compare(g_soft, g_card, W, H, &m);

        say("  painted surface: software %ld, card %ld (%ld%% gap)\n",
            m.painted_ref, m.painted_got,
            m.painted_ref ? (100 * (m.painted_got - m.painted_ref) / m.painted_ref)
                          : 0L);
        say("  frankly different pixels: %ld out of %ld (%ld per thousand)\n",
            m.differ, m.total, m.total ? (1000 * m.differ / m.total) : 0L);
        say("  of which on an edge, counted separately: %ld\n", m.differ_edge);
        say("  worst per-channel gap over the whole image: %d\n", m.max_gap);
        if (m.worst) {
            const size_t k = (size_t)m.worst_y * W + (size_t)m.worst_x;
            say("  worst gap off-edge: %d at (%d,%d)  software 0x%06X  card 0x%06X\n",
                m.worst, m.worst_x, m.worst_y,
                dkr_image_to565(g_soft[k] & 0x00FFFFFFu),
                g_card[k] & 0x00FFFFFFu);
        }

        /* The painted surface is the most robust check: it depends on neither
           quantisation nor edges, and wrong geometry on one side moves it
           immediately. */
        check("both backends paint the same surface to within 2 per cent",
              m.painted_ref > 0 &&
              (m.painted_got - m.painted_ref) * 50 <  m.painted_ref &&
              (m.painted_ref - m.painted_got) * 50 <  m.painted_ref);
        check("fewer than one pixel in a hundred differs frankly off-edge",
              m.total > 0 && m.differ * 100 < m.total);

        /* **The tight check, and it is the one that counts.**
         *
         * The threshold of 24 per channel used inside the metric served to clear
         * the ground: it allowed a sorting divergence to be seen without being
         * drowned by quantisation. Once the real divergences were fixed -- colour
         * iterated as on the hardware, depth sorted on 1/w -- measurement showed
         * that **the worst gap over the 307200 pixels is 9**, that is, one
         * quantisation step of red plus one of green. The loose threshold can
         * therefore no longer catch anything: it would pass on any regression
         * smaller than a tenth of the scale.
         *
         * We bound at 16, two quantisation steps. That is above the measured
         * noise and far below any gap that would have visual meaning. */
        check("no pixel deviates by more than two quantisation steps",
              m.max_gap <= 16);
    }

    /* The card's image is brought back too, so that a gap can be looked at and
       not merely counted -- and, since 3 September 2026, a map of where the two
       disagree. Counting divergent pixels says how many; only a map says where,
       and where is what tells a column shifted by a fill rule from a texture
       fetched wrong. */
    if (dkr_image_write_bmp("D:\\CMPCARD.BMP", g_card, W, H)) {
        say("  images written: D:\\CMPSOFT.BMP and D:\\CMPCARD.BMP\n");
    }
    {
        static unsigned diff[W * H];
        dkr_image_diff_map(g_soft, g_card, W, H, diff);
        if (dkr_image_write_bmp("D:\\CMPDIFF.BMP", diff, W, H)) {
            say("  difference map: D:\\CMPDIFF.BMP\n");
        }
    }

    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
