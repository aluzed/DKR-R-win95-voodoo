/* E05-S05 — trancher entre tampon en Z et tampon en W, par la mesure.
 *
 * Le ticket nomme le risque : « le combat de profondeur en 16 bits ne se
 * manifeste pas sur une scène de test : il se manifeste au loin, sur une piste
 * longue, en mouvement. Il faut le chercher activement, dans les conditions où
 * il apparaît, plutôt que d'attendre qu'il se signale. »
 *
 * Ce témoin le cherche activement. La scène n'est pas une scène de test au sens
 * habituel — elle est construite pour être **le pire cas** : deux surfaces
 * quasi coplanaires, très loin, séparées de deux pour mille. C'est exactement la
 * configuration d'une piste de course vue de loin, et c'est là que seize bits de
 * profondeur cèdent.
 *
 * ## Ce qui rend la comparaison honnête
 *
 * Les deux tampons ne lisent pas le même champ du sommet : le mode W consomme
 * `oow`, le mode Z consomme `ooz` sur [0, 65535]. Remplir les deux depuis la
 * même distance, avec une projection perspective réaliste, est la seule façon de
 * comparer les tampons plutôt que deux conventions différentes.
 *
 * La distance proche et la distance lointaine sont choisies pour ressembler à
 * une piste : 10 unités devant, 20000 au fond.
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

#define PROCHE  10.0f
#define LOINTAIN 20000.0f

static unsigned g_px[640 * 480];

/* La profondeur normalisée d'une distance, par une projection perspective
   classique. C'est ce que E04-S03 produit, et l'accord entre les deux est un
   critère à part entière : un désaccord donne un tri globalement faux. */
static float profondeur_ndc(float w)
{
    return (LOINTAIN / (LOINTAIN - PROCHE)) * (1.0f - PROCHE / w);
}

static void quad(dkr_render_backend *bk, float w, float r, float g, float b,
                 int largeur, int hauteur)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)largeur, (float)largeur,
                          0, (float)largeur, 0 };
    const float ys[6] = { 0, 0, (float)hauteur, 0, (float)hauteur, (float)hauteur };
    const float z = profondeur_ndc(w);
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = 255.0f;
        v[i].oow = 1.0f / w;
        v[i].z   = z;
        /* Le mode Z lit `ooz` sur [0, 65535], le mode W lit `oow`. Remplir les
           deux depuis la même distance est ce qui rend la comparaison honnête. */
        v[i].ooz = z * 65535.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

/* Compte les pixels où le lointain a gagné alors qu'il ne devait pas — le
   combat de profondeur, mesuré plutôt que regardé. */
static int perdus(int got, unsigned attendu)
{
    int i, n = 0;
    for (i = 0; i < got; i++) {
        if ((g_px[i] & 0x00FFFFFFu) != attendu) { n++; }
    }
    return n;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    const int W = 640, H = 480;
    int rw = 0, rh = 0, got;
    int fight_w = -1, fight_z = -1;

    g_out = fopen("D:\\DEPTH.TXT", "w");
    say("tampon en Z ou en W : la mesure, sur le pire cas\n\n");
    say("  plan proche %.0f, plan lointain %.0f\n", (double)PROCHE, (double)LOINTAIN);

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_TEST_AND_WRITE;
    st.cull    = DKR_CULL_NONE;

    /* --- Le pire cas : deux surfaces quasi coplanaires, tres loin ------------ *
     *
     * 5000 et 5010 : deux pour mille d'ecart, a un quart de la distance
     * maximale. Une piste de course presente en permanence ce genre de couple —
     * la route et son marquage, un pont et son ombre. */
    {
        static const struct { float loin, pres; const char *nom; } CAS[] = {
            { 5010.0f, 5000.0f, "2 pour mille a 5000" },
            { 15030.0f, 15000.0f, "2 pour mille a 15000" },
            /* Un cas volontairement au-dela de ce que seize bits peuvent tenir :
               si aucun des deux tampons ne le resout, c'est que la limite est
               atteinte et non que l'un est meilleur. Une comparaison sans point
               de saturation ne dit pas ou est le mur. */
            { 15003.0f, 15000.0f, "0,2 pour mille a 15000" },
        };
        int c;

        /* **Deux images de mise en route, jetees.**
         *
         * La premiere mesure de ce temoin donnait un resultat impossible : le
         * tampon en W echouait a 5000 et reussissait a 10000 et 15000, alors que
         * la precision se degrade avec la distance et ne s'ameliore jamais.
         * L'anomalie ne frappait que le tout premier cas mesure, ce qui designe
         * l'etat de la carte a l'ouverture plutot que la profondeur.
         *
         * Mesurer la premiere image apres l'ouverture d'un contexte, c'est
         * mesurer une machine qui n'a pas fini de s'installer. */
        say("\n-- tampon en W --\n");
        dkr_glide_backend_depth_mode(1);       /* 1 = W */
        {
            int mise_en_route;
            for (mise_en_route = 0; mise_en_route < 2; mise_en_route++) {
                bk.begin_frame(bk.self, 0x000000);
                bk.set_state(bk.self, &st);
                quad(&bk, 5010.0f, 255.0f, 0.0f, 0.0f, W, H);
                quad(&bk, 5000.0f, 0.0f, 255.0f, 0.0f, W, H);
                bk.present(bk.self);
            }
        }
        for (c = 0; c < 3; c++) {
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            quad(&bk, CAS[c].loin, 255.0f, 0.0f, 0.0f, W, H);   /* rouge, loin */
            quad(&bk, CAS[c].pres, 0.0f, 255.0f, 0.0f, W, H);   /* vert, pres */
            bk.present(bk.self);
            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            {
                const int n = perdus(got, 0x00FF00u);
                say("  %-22s : %6d pixels en combat sur %d (%d pour mille)\n",
                    CAS[c].nom, n, got, got ? (1000 * n / got) : 0);
                if (c == 0) { fight_w = n; }
            }
        }

        say("\n-- tampon en Z --\n");
        dkr_glide_backend_depth_mode(0);       /* 0 = Z */
        {
            int mise_en_route;
            for (mise_en_route = 0; mise_en_route < 2; mise_en_route++) {
                bk.begin_frame(bk.self, 0x000000);
                bk.set_state(bk.self, &st);
                quad(&bk, 5010.0f, 255.0f, 0.0f, 0.0f, W, H);
                quad(&bk, 5000.0f, 0.0f, 255.0f, 0.0f, W, H);
                bk.present(bk.self);
            }
        }
        for (c = 0; c < 3; c++) {
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            quad(&bk, CAS[c].loin, 255.0f, 0.0f, 0.0f, W, H);
            quad(&bk, CAS[c].pres, 0.0f, 255.0f, 0.0f, W, H);
            bk.present(bk.self);
            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            {
                const int n = perdus(got, 0x00FF00u);
                say("  %-22s : %6d pixels en combat sur %d (%d pour mille)\n",
                    CAS[c].nom, n, got, got ? (1000 * n / got) : 0);
                if (c == 0) { fight_z = n; }
            }
        }

        say("\n  verdict : %s\n",
            (fight_w < fight_z) ? "le tampon en W separe mieux au loin" :
            (fight_z < fight_w) ? "le tampon en Z separe mieux au loin" :
                                  "les deux se valent sur ce cas");
        /* Le controle n'est pas « W gagne » — ce serait presumer du resultat.
           C'est que l'un des deux resolve reellement le cas le plus proche, sans
           quoi seize bits ne suffiraient a rien et il faudrait revoir la plage
           de profondeur plutot que le choix de tampon. */
        check("au moins un des deux tampons resout le cas a 5000",
              (fight_w >= 0 && fight_w * 100 < 307200) ||
              (fight_z >= 0 && fight_z * 100 < 307200));
    }

    /* --- L'accord avec E04-S03 ---------------------------------------------- *
     *
     * La plage de profondeur doit coincider avec ce que la chaine produit. Un
     * desaccord ne se voit pas sur une surface isolee : il donne un tri
     * globalement faux, donc un decor qui passe devant un autre — tres visible,
     * et attribue au decodeur plutot qu'a la plage. */
    dkr_glide_backend_depth_mode(1);
    {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, 15000.0f, 255.0f, 0.0f, 0.0f, W, H);
        quad(&bk, 20.0f,     0.0f, 0.0f, 255.0f, W, H);
        bk.present(bk.self);
        got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
        say("\n-- accord de la plage avec E04-S03 --\n");
        say("  profondeur normalisee : a 20 = %.5f, a 15000 = %.5f\n",
            (double)profondeur_ndc(20.0f), (double)profondeur_ndc(15000.0f));
        check("sur toute la plage, le proche masque le lointain",
              perdus(got, 0x0000FFu) * 100 < got);
    }

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
