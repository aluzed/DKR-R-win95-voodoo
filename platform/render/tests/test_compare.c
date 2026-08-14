/* E09-S02 — la même scène dans les deux backends, comparée au pixel.
 *
 * C'est la raison d'être de tout ce qui précède. Le rastériseur logiciel de
 * E04-S08 n'existe pas pour dessiner le jeu : il existe pour dire ce que la
 * carte *aurait dû* dessiner. Tant qu'on ne pouvait pas lire le tampon d'image
 * de la Voodoo, l'oracle n'avait rien à comparer et restait une intention.
 *
 * L'entrée est unique et partagée : `scene_synthetic.h`. Deux copies de la scène
 * dériveraient, et la divergence serait attribuée au matériel.
 *
 * ## Ce que cette comparaison peut et ne peut pas établir
 *
 * Elle ne peut pas prouver que le rendu est *juste* — il faudra le jeu. Elle
 * établit que deux implémentations indépendantes de la même spécification
 * tombent d'accord, ce qui est la seule vérification disponible sans ROM et qui
 * attrape la classe d'erreurs la plus coûteuse : celles où un étage se déclare
 * satisfait en produisant autre chose que ce qu'il annonce.
 *
 * ## Une divergence est attendue, et il faut savoir laquelle
 *
 * Trois écarts sont structurels et ne signalent rien :
 *
 *   - **la quantification 565** de la carte, cinq bits de rouge et de bleu, six
 *     de vert, contre les huit du rastériseur ;
 *   - **le tri en profondeur**, z sur [0,1] d'un côté, tampon w encodé de
 *     l'autre — les deux ordonnent pareil mais ne quantifient pas pareil ;
 *   - **les bords**, où une règle de remplissage qui diffère d'un demi-pixel
 *     déplace une colonne entière de pixels.
 *
 * Le seuil retenu porte donc sur la proportion de pixels *franchement*
 * différents, et les pixels de bord sont comptés à part. Un seuil unique et
 * serré rendrait l'épreuve ininterprétable : elle échouerait toujours, pour la
 * bonne raison, et l'on finirait par ne plus la lire.
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
    say("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (!ok) { g_fails++; }
}

static unsigned char g_ram[DKR_SCENE_RAM];
static unsigned      g_soft[W * H];
static unsigned      g_card[W * H];

/* Quantifie une couleur 24 bits comme le ferait la carte, pour que la
   comparaison ne compte pas la quantification comme une divergence. La
   réplication des bits de poids fort est la même que dans la relecture — sans
   quoi le blanc du rastériseur et celui de la carte ne coïncideraient pas. */
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

/* Un pixel est « de bord » si l'un de ses quatre voisins diffère nettement dans
   l'image de référence. Les compter à part n'est pas une indulgence : un écart
   d'un demi-pixel dans la règle de remplissage déplace une colonne entière, et
   noyer cela dans le total masquerait une vraie divergence de surface. */
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
    say("la meme scene, deux rendus, compares au pixel\n\n");

    /* --- Le rastériseur de référence ---------------------------------------- */
    dkr_render_backend_software(&soft);
    if (!soft.open(soft.self, W, H)) {
        say("ECHEC : le rasteriseur ne s'ouvre pas\n");
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

    /* --- La carte ------------------------------------------------------------ */
    dkr_render_backend_glide(&card);
    if (!card.open(card.self, W, H)) {
        say("ECHEC : la carte ne s'ouvre pas\n");
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
            say("ECHEC : relecture impossible\n");
            card.close(card.self);
            if (g_out) { fclose(g_out); }
            return 1;
        }
    }
    card.close(card.self);

    /* --- Ce que la chaîne a fait des deux côtés ------------------------------ *
     *
     * Avant de comparer les images, comparer les comptes. Si les deux backends
     * n'ont pas reçu le même nombre de triangles, la différence d'image ne dit
     * plus rien du rendu : elle dit que la chaîne n'est pas déterministe, ce qui
     * est un problème bien plus grave et qu'on veut voir en premier. */
    say("  triangles emis : logiciel %lu, carte %lu\n", soft_emitted, card_emitted);
    check("la chaine a emis autant de triangles des deux cotes",
          soft_emitted == card_emitted && soft_emitted > 0);

    /* --- La comparaison ------------------------------------------------------ */
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

        say("  surface peinte : logiciel %ld, carte %ld (%ld%% d'ecart)\n",
            painted_soft, painted_card,
            painted_soft ? (100 * (painted_card - painted_soft) / painted_soft) : 0);
        say("  pixels franchement differents : %ld sur %ld (%ld pour mille)\n",
            differ, total, total ? (1000 * differ / total) : 0);
        say("  dont sur un bord, comptes a part : %ld\n", differ_edge);
        say("  pire ecart par canal sur toute l'image : %d\n", max_gap);
        if (worst) {
            say("  pire ecart hors bord : %d a (%d,%d)  logiciel 0x%06X  carte 0x%06X\n",
                worst, worst_x, worst_y,
                to565(g_soft[(size_t)worst_y * W + (size_t)worst_x] & 0x00FFFFFFu),
                g_card[(size_t)worst_y * W + (size_t)worst_x] & 0x00FFFFFFu);
        }

        /* La surface peinte est le contrôle le plus robuste : elle ne dépend ni
           de la quantification ni des bords, et une géométrie fausse d'un côté
           la fait bouger tout de suite. */
        check("les deux backends peignent la meme surface a 2 pour cent pres",
              painted_soft > 0 &&
              (painted_card - painted_soft) * 50 <  painted_soft &&
              (painted_soft - painted_card) * 50 <  painted_soft);
        check("moins d'un pixel sur cent differe franchement hors des bords",
              total > 0 && differ * 100 < total);

        /* **Le controle serre, et c'est celui qui vaut.**
         *
         * Le seuil de 24 par canal ci-dessus a servi a defricher : il permettait
         * de voir une divergence de tri sans etre noye par la quantification.
         * Une fois les vraies divergences corrigees — couleur iteree comme sur le
         * materiel, profondeur triee sur 1/w — la mesure a montre que **le pire
         * ecart sur les 307200 pixels vaut 9**, soit un pas de quantification du
         * rouge plus un du vert. Le seuil large ne peut donc plus rien attraper :
         * il passerait sur n'importe quelle regression inferieure a un dixieme de
         * l'echelle.
         *
         * On borne a 16, deux pas de quantification. C'est au-dessus du bruit
         * mesure et tres en dessous de tout ecart qui aurait un sens visuel. */
        check("aucun pixel ne s'ecarte de plus de deux pas de quantification",
              max_gap <= 16);
    }

    /* L'image de la carte est ramenée aussi, pour qu'un écart puisse être
       regardé et non seulement compté. */
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
            say("  images ecrites : D:\\CMPSOFT.BMP et D:\\CMPCARD.BMP\n");
        }
    }

    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
