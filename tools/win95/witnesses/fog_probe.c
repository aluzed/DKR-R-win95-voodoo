/* E05-S06 — le brouillard de Glide, mesuré.
 *
 * Le decomp donne le modèle : `gSPFogPosition(min, max)` charge un multiplicateur
 * `128000/(max-min)` et un décalage `(500-min)*256/(max-min)`, dont le RSP tire
 * un facteur par sommet qu'il range dans **l'alpha du sommet**. Le mélangeur
 * l'applique ensuite par `G_RM_FOG_SHADE_A` — source `G_BL_CLR_FOG`, facteur
 * `G_BL_A_SHADE`.
 *
 * Glide offre exactement ce modèle : `GR_FOG_WITH_ITERATED_ALPHA`. Le ticket
 * demande de trancher entre cette voie et la table de 64 entrées, « en mesurant
 * si elle a un coût par sommet significatif avant de conclure ». C'est ce que
 * fait ce témoin.
 *
 * ## Ce qu'il vérifie qu'on ne lui a pas demandé
 *
 * Le facteur de brouillard occupe l'alpha du sommet. **Tout ce qui voudrait y
 * ranger autre chose entre en conflit avec lui**, et deux choses le voudraient :
 *
 *   - la translucidité d'une surface, quand `XLU_SURF` et le brouillard se
 *     rencontrent — 78 modes translucides contre 74 modes de brouillard dans la
 *     source du jeu, la rencontre est certaine ;
 *   - la seconde couleur constante que E05-S03 propose d'y faire voyager pour
 *     contourner l'unique registre de Glide.
 *
 * Le second point est une conséquence de ce ticket sur un autre, et il vaut
 * mieux la découvrir ici que sur un décor faux.
 */
#include "render/glide.h"
#include "render/backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char l[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(l, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(l, g_out); fflush(g_out); }
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (!ok) { g_fails++; }
}

static unsigned g_px[640 * 480];

#define FOG_R 0
#define FOG_G 255
#define FOG_B 0        /* vert franc : impossible a confondre avec la surface */
#define SURF_R 255
#define SURF_G 0
#define SURF_B 0       /* rouge franc */

/* Un quadrilatère dont l'alpha du sommet varie de gauche à droite. C'est le
   facteur de brouillard : à gauche zéro, à droite plein. La transition, et non
   une valeur isolée, est ce qui révèle une courbe fausse — le ticket insiste
   là-dessus, et c'est vrai à l'échelle d'une image comme d'une séquence. */
static void quad_degrade(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)w, (float)w, 0, (float)w, 0 };
    const float ys[6] = { 0, 0, (float)h, 0, (float)h, (float)h };
    const float as[6] = { 0.0f, 255.0f, 255.0f, 0.0f, 255.0f, 0.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = SURF_R; v[i].g = SURF_G; v[i].b = SURF_B;
        v[i].a = as[i];
        v[i].oow = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned lire(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    const int W = 640, H = 480;
    int rw = 0, rh = 0, i;
    unsigned long t_sans, t_avec;

    g_out = fopen("D:\\FOG.TXT", "w");
    say("le brouillard de Glide, par facteur de sommet\n\n");
    say("  couleur de brouillard : vert franc\n");
    say("  couleur de surface    : rouge franc\n");
    say("  alpha du sommet : 0 a gauche, 255 a droite\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;

    /* Deux images jetees. La lecon de E05-S05 : mesurer la premiere image apres
       l'ouverture d'un contexte, c'est mesurer une machine qui n'a pas fini de
       s'installer. */
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad_degrade(&bk, W, H);
        bk.present(bk.self);
    }

    /* --- Sans brouillard : la reference -------------------------------------- */
    bk.begin_frame(bk.self, 0x000000);
    st.fog_enabled = 0;
    bk.set_state(bk.self, &st);
    quad_degrade(&bk, W, H);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
        say("\n-- sans brouillard --\n");
        say("  gauche 0x%06X  milieu 0x%06X  droite 0x%06X\n",
            lire(rw / 8, rh / 2, rw), lire(rw / 2, rh / 2, rw),
            lire(rw * 7 / 8, rh / 2, rw));
        check("la surface est rouge partout, l'alpha du sommet n'y change rien",
              lire(rw / 8, rh / 2, rw) == lire(rw * 7 / 8, rh / 2, rw));
    }

    /* --- Avec brouillard ------------------------------------------------------ */
    bk.begin_frame(bk.self, 0x000000);
    st.fog_enabled = 1;
    st.fog_color   = (FOG_R << 16) | (FOG_G << 8) | FOG_B;
    bk.set_state(bk.self, &st);
    quad_degrade(&bk, W, H);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
        const unsigned g = lire(rw / 8, rh / 2, rw);
        const unsigned m = lire(rw / 2, rh / 2, rw);
        const unsigned d = lire(rw * 7 / 8, rh / 2, rw);
        say("\n-- avec brouillard --\n");
        say("  gauche 0x%06X  milieu 0x%06X  droite 0x%06X\n", g, m, d);

        check("les deux extremites different : le brouillard agit", g != d);
        /* Quel bout est le brouillard ? On le releve plutot que de le supposer :
           Glide peut interpreter l'alpha dans un sens ou dans l'autre, et se
           tromper donnerait un brouillard **inverse** — clair de pres, opaque au
           loin — ce qui est spectaculaire et facile a attribuer a la courbe
           plutot qu'au sens. */
        say("  sens : alpha 255 %s\n",
            (((d >> 8) & 0xFF) > ((d >> 16) & 0xFF)) ? "= plein brouillard"
                                                     : "= pas de brouillard");
        check("un des deux bouts est franchement vert",
              (((d >> 8) & 0xFF) > 200 && ((d >> 16) & 0xFF) < 60) ||
              (((g >> 8) & 0xFF) > 200 && ((g >> 16) & 0xFF) < 60));
        /* Et le milieu doit etre un melange des deux, sans quoi la transition
           serait abrupte — le defaut que le ticket veut eviter. */
        check("le milieu est un melange, pas l'un des deux extremes",
              m != g && m != d);
    }

    /* --- L'interaction avec le melange --------------------------------------- *
     *
     * L'alpha du sommet porte le facteur de brouillard. Une surface translucide
     * en a besoin pour sa propre transparence, et le jeu emploie 78 modes
     * translucides pour 74 modes de brouillard : la rencontre est certaine.
     * On mesure ce qui se passe plutot que de le deduire. */
    bk.begin_frame(bk.self, 0x0000FF);   /* fond bleu, pour voir a travers */
    st.blend = DKR_BLEND_ALPHA;
    st.fog_enabled = 1;
    bk.set_state(bk.self, &st);
    quad_degrade(&bk, W, H);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
        const unsigned g = lire(rw / 8, rh / 2, rw);
        const unsigned d = lire(rw * 7 / 8, rh / 2, rw);
        say("\n-- brouillard et melange alpha ensemble --\n");
        say("  gauche 0x%06X  droite 0x%06X\n", g, d);
        say("  (fond bleu : ce qui laisse voir du bleu est translucide)\n");
        /* Ce controle n'affirme pas un resultat : il **enregistre** lequel des
           deux usages de l'alpha l'emporte. Le savoir est ce qui permettra de
           decider, et l'ignorer donnerait un brouillard ou une transparence
           faux selon les surfaces. */
        check("les deux usages de l'alpha ne s'annulent pas mutuellement",
              g != d);
    }

    /* --- Le cout -------------------------------------------------------------- */
    st.blend = DKR_BLEND_OPAQUE;
    st.fog_enabled = 0;
    t_sans = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad_degrade(&bk, W, H);
        bk.present(bk.self);
    }
    t_sans = GetTickCount() - t_sans;

    st.fog_enabled = 1;
    t_avec = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad_degrade(&bk, W, H);
        bk.present(bk.self);
    }
    t_avec = GetTickCount() - t_avec;

    say("\n-- le cout --\n");
    say("  100 images sans brouillard : %lu ms\n", t_sans);
    say("  100 images avec            : %lu ms\n", t_avec);

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
