/* Vérifier les constantes de Glide 2.x **par la mesure**, une par une.
 *
 * `glide_backend.c` traduit l'état de rendu en appels Glide, à l'aide de valeurs
 * d'énumération écrites de mémoire : il n'y a pas de `glide.h` sur cette machine.
 * Le danger n'est pas qu'une valeur fausse plante — Glide ne valide rien — mais
 * qu'elle programme un registre voisin. L'image sort différente, sans erreur, et
 * l'écart est ensuite attribué au décodeur de display list.
 *
 * Chaque épreuve ci-dessous exerce **un seul** mode, dessine, puis relit le
 * tampon d'image par `grLfbLock`. Le résultat attendu est calculé à la main et
 * écrit dans le code : ce n'est pas une capture qu'on regarde, c'est un pixel
 * qu'on compare.
 *
 * Ce qui passe ici est un fait daté ; ce qui échoue nomme la constante à revoir.
 */
#include "render/glide.h"
#include "render/backend.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

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
}

static void check(const char *what, int ok, unsigned got, unsigned expected)
{
    say("  %s %-46s  lu 0x%06X  attendu ~0x%06X\n",
        ok ? "ok   " : "ECHEC", what, got & 0x00FFFFFFu, expected & 0x00FFFFFFu);
    if (!ok) { g_fails++; }
}

/* La carte travaille en 565 : deux couleurs voisines de moins d'un pas de
   quantification sont la même couleur. Comparer à l'octet près rejetterait des
   résultats justes — et accepter trop large ne prouverait rien. Huit unités,
   soit un pas de rouge et de bleu, est la tolérance minimale honnête. */
static int near_color(unsigned a, unsigned b, int tol)
{
    int i;
    for (i = 0; i < 3; i++) {
        const int ca = (int)((a >> (i * 8)) & 0xFF);
        const int cb = (int)((b >> (i * 8)) & 0xFF);
        const int d  = ca - cb;
        if (d > tol || d < -tol) { return 0; }
    }
    return 1;
}

/* --- La scène d'épreuve ------------------------------------------------------ *
 *
 * Un triangle qui couvre largement le centre de l'écran, dont on ne lit qu'un
 * pixel : celui du centre exact. Tous les sommets portent la même couleur, de
 * sorte que l'interpolation ne puisse pas être confondue avec l'effet mesuré. */
static void triangle(dkr_render_backend *bk, unsigned char r, unsigned char g,
                     unsigned char bl, unsigned char a, float oow)
{
    dkr_render_vertex v[3];
    int i;
    const float xs[3] = { 320.0f,  600.0f,   40.0f };
    const float ys[3] = {  40.0f,  440.0f,  440.0f };

    memset(v, 0, sizeof(v));
    for (i = 0; i < 3; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = (float)r; v[i].g = (float)g; v[i].b = (float)bl;
        v[i].a = (float)a;
        v[i].oow = oow;
        v[i].ooz = 0.0f;
    }
    bk->draw_triangles(bk->self, v, 1);
}

static unsigned centre_pixel(void)
{
    static unsigned px[640 * 480];
    int w = 0, h = 0;
    if (dkr_glide_read_framebuffer(px, 640 * 480, &w, &h) <= 0 || w <= 0) {
        return 0xDEADBEEFu;
    }
    return px[(size_t)(h / 2) * (size_t)w + (size_t)(w / 2)];
}

/* Nombre de pixels non noirs, pour les épreuves où l'on veut savoir si quelque
   chose a été dessiné du tout — le test alpha, par exemple, dont l'effet est
   « rien » et non « une autre couleur ». */
static int painted_count(void)
{
    static unsigned px[640 * 480];
    int w = 0, h = 0, i, n = 0;
    const int got = dkr_glide_read_framebuffer(px, 640 * 480, &w, &h);
    for (i = 0; i < got; i++) {
        if ((px[i] & 0x00FFFFFFu) != 0) { n++; }
    }
    return n;
}

static void base_state(dkr_render_state *st)
{
    memset(st, 0, sizeof(*st));
    st->combine = DKR_COMBINE_SHADE;
    st->blend   = DKR_BLEND_OPAQUE;
    st->depth   = DKR_DEPTH_DISABLED;
    st->cull    = DKR_CULL_NONE;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;

    g_out = fopen("D:\\GLSTATE.TXT", "w");
    say("verification des constantes Glide 2.x par relecture\n\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, 640, 480)) {
        say("ECHEC : la carte ne s'ouvre pas\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }

    /* --- Mélange : opaque --------------------------------------------------- */
    base_state(&st);
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000040);            /* fond bleu sombre */
    triangle(&bk, 255, 0, 0, 255, 1.0f);
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("melange opaque : le rouge couvre le fond",
              near_color(c, 0xFF0000u, 8), c, 0xFF0000u);
    }

    /* --- Mélange : alpha ----------------------------------------------------- *
     *
     * Rouge à alpha 128 sur fond noir : la moitié du rouge, soit 0x800000. Si
     * `GR_BLEND_SRC_ALPHA` désigne en réalité un autre facteur, on obtiendra du
     * rouge plein ou du noir — deux résultats très reconnaissables. */
    st.blend = DKR_BLEND_ALPHA;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 0, 0, 128, 1.0f);
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("melange alpha : moitie de rouge sur noir",
              near_color(c, 0x800000u, 12), c, 0x800000u);
    }

    /* --- Mélange : additif ---------------------------------------------------- *
     * Rouge plein ajouté à un fond bleu : les deux composantes coexistent. */
    st.blend = DKR_BLEND_ADDITIVE;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000040);
    triangle(&bk, 255, 0, 0, 255, 1.0f);
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("melange additif : rouge + bleu = magenta",
              near_color(c, 0xFF0040u, 12), c, 0xFF0040u);
    }

    /* --- Profondeur : le sens de la comparaison en mode w --------------------- *
     *
     * **C'est l'épreuve la plus importante du fichier.** En tampon w, un objet
     * proche a un `1/w` grand, donc la comparaison s'inverse par rapport au
     * tampon z. Se tromper de sens ne vide pas l'écran : cela peint la scène à
     * l'envers, ce qui passe inaperçu sur une scène simple et devient
     * incompréhensible sur le jeu.
     *
     * On dessine le lointain d'abord, puis le proche : le proche doit gagner. */
    st.blend = DKR_BLEND_OPAQUE;
    st.depth = DKR_DEPTH_TEST_AND_WRITE;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 0, 255, 0, 255, 0.001f);        /* lointain : 1/w petit */
    triangle(&bk, 255, 0, 0, 255, 1.0f);          /* proche   : 1/w grand */
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("profondeur : le proche couvre le lointain",
              near_color(c, 0xFF0000u, 8), c, 0xFF0000u);
    }

    /* Et l'ordre inverse — le proche d'abord. Le lointain doit être rejeté. Sans
       cette seconde moitié, un test de profondeur totalement désactivé passerait
       la première. */
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 0, 0, 255, 1.0f);          /* proche d'abord */
    triangle(&bk, 0, 255, 0, 255, 0.001f);        /* lointain ensuite */
    bk.present(bk.self);
    {
        const unsigned c = centre_pixel();
        check("profondeur : le lointain est rejete par le proche",
              near_color(c, 0xFF0000u, 8), c, 0xFF0000u);
    }

    /* --- Ciseaux -------------------------------------------------------------- *
     * Fenêtre limitée à la moitié droite : le centre exact est sur la frontière,
     * on lit donc par comptage plutôt que par pixel. Un triangle qui couvre
     * ~75000 pixels doit en perdre à peu près la moitié. */
    st.depth = DKR_DEPTH_DISABLED;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    bk.set_scissor(bk.self, 320, 0, 640, 480);
    triangle(&bk, 255, 255, 255, 255, 1.0f);
    bk.present(bk.self);
    {
        const int n = painted_count();
        say("  %s ciseaux : moitie droite seulement                 %d pixels peints\n",
            (n > 20000 && n < 60000) ? "ok   " : "ECHEC", n);
        if (!(n > 20000 && n < 60000)) { g_fails++; }
    }
    bk.set_scissor(bk.self, 0, 0, 640, 480);

    /* --- Test alpha ----------------------------------------------------------- *
     * Référence 128 : un triangle à alpha 64 doit disparaître entièrement. */
    st.alpha_test      = 1;
    st.alpha_reference = 128;
    bk.set_state(bk.self, &st);
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 255, 255, 64, 1.0f);
    bk.present(bk.self);
    {
        const int n = painted_count();
        say("  %s test alpha : 64 < 128, rien n'est peint            %d pixels peints\n",
            (n < 100) ? "ok   " : "ECHEC", n);
        if (n >= 100) { g_fails++; }
    }

    /* Et le complément : alpha 200 passe. Sans lui, un test alpha bloqué sur
       « jamais » passerait l'épreuve précédente. */
    bk.begin_frame(bk.self, 0x000000);
    triangle(&bk, 255, 255, 255, 200, 1.0f);
    bk.present(bk.self);
    {
        const int n = painted_count();
        say("  %s test alpha : 200 >= 128, le triangle passe         %d pixels peints\n",
            (n > 60000) ? "ok   " : "ECHEC", n);
        if (n <= 60000) { g_fails++; }
    }

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
