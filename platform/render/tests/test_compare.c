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
 */
#include "render/software.h"
#include "render/glide.h"
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

/* Quantises a 24-bit colour the way the card would, so that the comparison does
   not count quantisation as a divergence. The high-bit replication is the same
   as in the read-back — otherwise the rasteriser's white and the card's would
   not coincide. */
static unsigned to565(unsigned c)
{
    const unsigned r = ((c >> 16) & 0xFF) >> 3;
    const unsigned g = ((c >> 8)  & 0xFF) >> 2;
    const unsigned b = ( c        & 0xFF) >> 3;
    return (((r << 3) | (r >> 2)) << 16) |
           (((g << 2) | (g >> 4)) << 8)  |
            ((b << 3) | (b >> 2));
}

static int channel_gap(unsigned a, unsigned b)
{
    int worst = 0, i;
    for (i = 0; i < 3; i++) {
        int d = (int)((a >> (i * 8)) & 0xFF) - (int)((b >> (i * 8)) & 0xFF);
        if (d < 0) { d = -d; }
        if (d > worst) { worst = d; }
    }
    return worst;
}

/* A pixel is an "edge" pixel if one of its four neighbours differs markedly in
   the reference image. Counting them separately is not indulgence: a half-pixel
   difference in the fill rule shifts a whole column, and drowning that in the
   total would mask a real surface divergence. */
static int is_edge(const unsigned *img, int x, int y)
{
    const unsigned c = img[(size_t)y * W + (size_t)x];
    int dx, dy;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) { continue; }
            if (channel_gap(c, img[(size_t)ny * W + (size_t)nx]) > 24) {
                return 1;
            }
        }
    }
    return 0;
}

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
        long total = 0, differ = 0, differ_edge = 0, painted_soft = 0,
             painted_card = 0;
        int  worst = 0, worst_x = 0, worst_y = 0, max_gap = 0;
        int  x, y;

        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                const size_t i = (size_t)y * W + (size_t)x;
                const unsigned a = to565(g_soft[i] & 0x00FFFFFFu);
                const unsigned b = g_card[i] & 0x00FFFFFFu;
                const int gap = channel_gap(a, b);

                total++;
                if (gap > max_gap) { max_gap = gap; }
                if (a) { painted_soft++; }
                if (b) { painted_card++; }
                if (gap > 24) {
                    if (is_edge(g_soft, x, y)) {
                        differ_edge++;
                    } else {
                        differ++;
                        if (gap > worst) { worst = gap; worst_x = x; worst_y = y; }
                    }
                }
            }
        }

        say("  painted surface: software %ld, card %ld (%ld%% gap)\n",
            painted_soft, painted_card,
            painted_soft ? (100 * (painted_card - painted_soft) / painted_soft) : 0);
        say("  frankly different pixels: %ld out of %ld (%ld per thousand)\n",
            differ, total, total ? (1000 * differ / total) : 0);
        say("  of which on an edge, counted separately: %ld\n", differ_edge);
        say("  worst per-channel gap over the whole image: %d\n", max_gap);
        if (worst) {
            say("  worst gap off-edge: %d at (%d,%d)  software 0x%06X  card 0x%06X\n",
                worst, worst_x, worst_y,
                to565(g_soft[(size_t)worst_y * W + (size_t)worst_x] & 0x00FFFFFFu),
                g_card[(size_t)worst_y * W + (size_t)worst_x] & 0x00FFFFFFu);
        }

        /* The painted surface is the most robust check: it depends on neither
           quantisation nor edges, and wrong geometry on one side moves it
           immediately. */
        check("both backends paint the same surface to within 2 per cent",
              painted_soft > 0 &&
              (painted_card - painted_soft) * 50 <  painted_soft &&
              (painted_soft - painted_card) * 50 <  painted_soft);
        check("fewer than one pixel in a hundred differs frankly off-edge",
              total > 0 && differ * 100 < total);

        /* **The tight check, and it is the one that counts.**
         *
         * The threshold of 24 per channel above served to clear the ground: it
         * allowed a sorting divergence to be seen without being drowned by
         * quantisation. Once the real divergences were fixed — colour iterated as
         * on the hardware, depth sorted on 1/w — measurement showed that **the
         * worst gap over the 307200 pixels is 9**, that is, one quantisation step
         * of red plus one of green. The loose threshold can therefore no longer
         * catch anything: it would pass on any regression smaller than a tenth of
         * the scale.
         *
         * We bound at 16, two quantisation steps. That is above the measured
         * noise and far below any gap that would have visual meaning. */
        check("no pixel deviates by more than two quantisation steps",
              max_gap <= 16);
    }

    /* The card's image is brought back too, so that a gap can be looked at and
       not merely counted. */
    {
        FILE *f = fopen("D:\\CMPCARD.BMP", "wb");
        if (f) {
            const int pad = (4 - (W * 3) % 4) % 4;
            const unsigned data = (unsigned)((W * 3 + pad) * H);
            unsigned char head[54];
            int x, y, i;
            memset(head, 0, sizeof(head));
            head[0] = 'B'; head[1] = 'M';
            *(unsigned *)&head[2]  = 54u + data;
            *(unsigned *)&head[10] = 54u;
            *(unsigned *)&head[14] = 40u;
            *(int *)     &head[18] = W;
            *(int *)     &head[22] = H;
            head[26] = 1; head[28] = 24;
            *(unsigned *)&head[34] = data;
            fwrite(head, 1, sizeof(head), f);
            for (y = H - 1; y >= 0; y--) {
                for (x = 0; x < W; x++) {
                    const unsigned c = g_card[(size_t)y * W + (size_t)x];
                    unsigned char bgr[3];
                    bgr[0] = (unsigned char)(c & 0xFF);
                    bgr[1] = (unsigned char)((c >> 8) & 0xFF);
                    bgr[2] = (unsigned char)((c >> 16) & 0xFF);
                    fwrite(bgr, 1, 3, f);
                }
                for (i = 0; i < pad; i++) { fputc(0, f); }
            }
            fclose(f);
            say("  images written: D:\\CMPSOFT.BMP and D:\\CMPCARD.BMP\n");
        }
    }

    say("\n%d failure(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
