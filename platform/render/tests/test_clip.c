/* E04-S05 — épreuve du découpage.
 *
 * Le découpage est un classique des erreurs subtiles : un triangle mal découpé
 * produit un éclat de géométrie qui traverse l'écran, très visible et difficile
 * à reproduire parce qu'il dépend d'un angle de caméra précis.
 *
 * Les contrôles ci-dessous placent donc délibérément la caméra **dans** la
 * géométrie plutôt que devant elle, et vérifient chaque attribut interpolé
 * séparément — l'oubli d'un seul ne se verrait que sur les triangles découpés,
 * donc rarement, donc tard.
 */
#include "render/clip.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "ECHEC", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "ECHEC", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    const int ok = (got - want < tol) && (want - got < tol);
    printf("  %s %-40s attendu %.4f, obtenu %.4f\n",
           ok ? "ok   " : "ECHEC", what, want, got);
    if (g_out) {
        fprintf(g_out, "  %s %-40s attendu %.4f, obtenu %.4f\n",
                ok ? "ok   " : "ECHEC", what, want, got);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static dkr_clip_vertex vertex(float x, float y, float z, float w,
                              float r, float s, float t)
{
    dkr_clip_vertex v;
    memset(&v, 0, sizeof(v));
    v.x = x; v.y = y; v.z = z; v.w = w;
    v.r = r; v.g = 0.0f; v.b = 0.0f; v.a = 255.0f;
    v.s = s; v.t = t;
    return v;
}

int main(void)
{
    dkr_clip_vertex in[3], out[6];
    dkr_transform   t;

    g_out = fopen("D:\\CLIP.TXT", "w");

    /* --- Entierement devant : rien a faire --------------------------------- */
    in[0] = vertex(0.0f, 0.0f, 1.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    in[1] = vertex(1.0f, 0.0f, 1.0f, 1.0f, 255.0f, 1.0f, 0.0f);
    in[2] = vertex(0.0f, 1.0f, 1.0f, 1.0f, 255.0f, 0.0f, 1.0f);
    check("un triangle entierement devant sort intact",
          dkr_clip_near(in, out) == 1 &&
          out[0].w == 1.0f && out[1].w == 1.0f && out[2].w == 1.0f);

    /* --- Entierement derriere : rien ne sort ------------------------------- */
    in[0].w = in[1].w = in[2].w = -1.0f;
    check("un triangle entierement derriere disparait",
          dkr_clip_near(in, out) == 0);

    /* --- Un sommet derriere : **deux** triangles ---------------------------- *
     *
     * Le polygone restant est un quadrilatere. L'oublier ferait disparaitre la
     * moitie de la surface — un trou, sur les seuls triangles qui traversent le
     * plan, donc un defaut rare et deroutant. */
    in[0] = vertex(0.0f, 0.0f,  1.0f, -1.0f, 255.0f, 0.0f, 0.0f);  /* derriere */
    in[1] = vertex(1.0f, 0.0f,  1.0f,  1.0f, 255.0f, 1.0f, 0.0f);
    in[2] = vertex(0.0f, 1.0f,  1.0f,  1.0f, 255.0f, 0.0f, 1.0f);
    check("un sommet derriere produit deux triangles",
          dkr_clip_near(in, out) == 2);

    /* --- Deux sommets derriere : un seul triangle --------------------------- */
    in[1].w = -1.0f;
    check("deux sommets derriere produisent un triangle",
          dkr_clip_near(in, out) == 1);

    /* --- Tous les attributs sont interpoles --------------------------------- *
     *
     * C'est le controle qui compte. Un attribut oublie ne se voit que sur les
     * triangles decoupes ; le verifier separement pour chacun est le seul moyen
     * de ne pas laisser passer l'oubli.
     *
     * Le plan choisi est celui de la **bande de garde a droite**, et non le plan
     * proche : depuis que le decoupeur borne aussi les cotes, un sommet cree au
     * plan proche a `w` minuscule et se fait redecouper par la garde — ce qui est
     * l'effet voulu, mais rend l'interpolation malaisee a verifier a la main. Ici
     * tous les `w` valent 1, donc la garde tombe simplement a `x = 4`. */
    {
        int n, i, found = 0;
        in[0] = vertex(0.0f, 0.0f, 0.0f, 1.0f, 100.0f, 0.0f, 0.0f);
        in[1] = vertex(8.0f, 0.0f, 0.0f, 1.0f, 200.0f, 1.0f, 0.5f);  /* hors garde */
        in[2] = vertex(0.0f, 2.0f, 0.0f, 1.0f, 100.0f, 0.0f, 1.0f);
        n = dkr_clip_near(in, out);
        check("le triangle qui depasse la garde est decoupe", n >= 1);
        for (i = 0; i < n * 3; i++) {
            /* Le sommet cree sur l'arete 0-1 : x vaut 4, soit la moitie du
               chemin, donc chaque attribut doit valoir sa moyenne. */
            if (out[i].x > 3.9f && out[i].x < 4.1f && out[i].y < 0.5f) {
                found = 1;
                check_near("la couleur y est interpolee", out[i].r, 150.0, 1.0);
                check_near("la coordonnee s aussi",       out[i].s,   0.5, 0.01);
                check_near("et la coordonnee t",          out[i].t,  0.25, 0.01);
            }
        }
        check("le sommet cree a bien ete trouve", found);
    }

    /* --- L'invariant que la bande de garde apporte --------------------------- *
     *
     * **Aucune coordonnee projetee ne depasse la bande.** C'est la propriete pour
     * laquelle le decoupeur a ete generalise : sans elle, un sommet cree au plan
     * proche projetait a seize millions de pixels, et les fonctions d'arete du
     * rasteriseur perdaient toute precision — au point que l'hote et la cible ne
     * rendaient plus les memes pixels. */
    {
        dkr_transform vp;
        int n, i;
        dkr_transform_init(&vp);
        dkr_transform_set_viewport(&vp, 160.0f, -120.0f, 160.0f, 120.0f);

        /* Le cas qui posait probleme : un sommet derriere la camera. */
        in[0] = vertex(-30.0f, 0.0f, -25.0f, -50.0f, 255.0f, 0.0f, 0.0f);
        in[1] = vertex( 90.0f, 0.0f, 150.0f, 300.0f, 255.0f, 1.0f, 0.0f);
        in[2] = vertex( 60.0f, -60.0f, 100.0f, 200.0f, 255.0f, 0.0f, 1.0f);
        n = dkr_clip_near(in, out);
        check("le triangle traversant produit quelque chose", n >= 1);
        for (i = 0; i < n * 3; i++) {
            dkr_render_vertex rv;
            dkr_clip_project(&vp, &out[i], &rv);
            /* La garde vaut 4 demi-ecrans ; avec une echelle de 160 et une
               translation de 160, cela borne x a [-480, 800]. On verifie large :
               ce qui compte est qu'il n'y ait plus de millions. */
            check("les coordonnees projetees restent bornees",
                  rv.x > -2000.0f && rv.x < 3000.0f &&
                  rv.y > -2000.0f && rv.y < 3000.0f);
        }
    }

    /* --- La projection ------------------------------------------------------ */
    dkr_transform_init(&t);
    dkr_transform_set_viewport(&t, 320.0f, -240.0f, 320.0f, 240.0f);
    {
        dkr_clip_vertex  cv = vertex(50.0f, 25.0f, 10.0f, 100.0f, 255.0f, 2.0f, 4.0f);
        dkr_render_vertex rv;
        dkr_clip_project(&t, &cv, &rv);
        check_near("x ecran apres division", rv.x, 50.0 / 100.0 * 320.0 + 320.0, 0.01);
        check_near("1/w",                    rv.oow, 0.01, 0.00001);
        /* Le rasteriseur et Glide attendent s/w, pas s — et dans l'espace de
           256 texels que la carte impose. L'echelle est appliquee ici, une fois
           par sommet, plutot que par le backend une fois par triangle : voir
           `DKR_TEXCOORD_SCALE` dans `backend.h`, ou la mesure est rapportee.
           Cette epreuve verifiait la division sans l'echelle, et c'est le
           changement de contrat qui l'a fait echouer, non une regression. */
        check_near("s est divise par w, dans l'espace de 256 texels",
                   rv.tmu[0][DKR_TMU_SOW], 2.0 / 100.0 * 256.0, 0.001);
        check_near("t aussi",
                   rv.tmu[0][DKR_TMU_TOW], 4.0 / 100.0 * 256.0, 0.001);
    }

    /* --- Faces arriere ------------------------------------------------------ *
     *
     * La convention vient du microcode : le sens depend du **signe de l'echelle
     * en x**. Une fenetre miroir inverse l'orientation apparente, et eliminer le
     * mauvais cote viderait l'ecran. */
    check("echelle positive : on elimine l'arriere",
          dkr_cull_mode_for_viewport( 320.0f, 1) == DKR_CULL_BACK);
    check("echelle negative : on elimine l'avant",
          dkr_cull_mode_for_viewport(-320.0f, 1) == DKR_CULL_FRONT);
    check("culling desactive : on garde tout",
          dkr_cull_mode_for_viewport( 320.0f, 0) == DKR_CULL_NONE);
    {
        dkr_render_vertex tri[3];
        memset(tri, 0, sizeof(tri));
        /* Horaire a l'ecran, origine en haut a gauche : area positive. */
        tri[0].x =  0.0f; tri[0].y =  0.0f;
        tri[1].x = 10.0f; tri[1].y =  0.0f;
        tri[2].x =  0.0f; tri[2].y = 10.0f;
        check("un sens est accepte et l'autre rejete",
              dkr_cull_accept(tri, DKR_CULL_FRONT) !=
              dkr_cull_accept(tri, DKR_CULL_BACK));
        check("sans culling, tout passe", dkr_cull_accept(tri, DKR_CULL_NONE));
        /* Degenere : aucune surface, donc rien a dessiner quel que soit le sens. */
        tri[2] = tri[1];
        check("un triangle degenere est rejete",
              !dkr_cull_accept(tri, DKR_CULL_BACK) &&
              !dkr_cull_accept(tri, DKR_CULL_FRONT));
    }

    /* --- Rejet hors ecran --------------------------------------------------- */
    {
        dkr_render_vertex tri[3];
        memset(tri, 0, sizeof(tri));
        tri[0].x = -5000.0f; tri[1].x = -6000.0f; tri[2].x = -7000.0f;
        check("un triangle entierement a gauche est rejete",
              dkr_clip_reject_offscreen(tri, 640, 480, DKR_CLIP_DEFAULT_MARGIN));
        /* **Seulement si les trois sont du meme cote.** Un triangle disperse
           couvre peut-etre l'ecran, et le rejeter serait bien plus grave que de
           laisser passer un triangle inutile. */
        tri[2].x = 320.0f;
        check("mais pas s'il en reste un dans l'ecran",
              !dkr_clip_reject_offscreen(tri, 640, 480, DKR_CLIP_DEFAULT_MARGIN));
    }

    /* --- Ecran partage ------------------------------------------------------ */
    {
        dkr_scissor s;
        check("un joueur occupe tout l'ecran",
              dkr_scissor_for_player(1, 0, 640, 480, &s) &&
              s.x0 == 0 && s.y0 == 0 && s.x1 == 640 && s.y1 == 480);
        check("deux joueurs se partagent en hauteur",
              dkr_scissor_for_player(2, 0, 640, 480, &s) && s.y1 == 240 &&
              dkr_scissor_for_player(2, 1, 640, 480, &s) && s.y0 == 240);
        check("quatre joueurs occupent quatre quadrants",
              dkr_scissor_for_player(4, 0, 640, 480, &s) && s.x0 == 0   && s.y0 == 0   &&
              dkr_scissor_for_player(4, 1, 640, 480, &s) && s.x0 == 320 && s.y0 == 0   &&
              dkr_scissor_for_player(4, 2, 640, 480, &s) && s.x0 == 0   && s.y0 == 240 &&
              dkr_scissor_for_player(4, 3, 640, 480, &s) && s.x0 == 320 && s.y0 == 240);
        check("a trois joueurs, la grille est la meme",
              dkr_scissor_for_player(3, 2, 640, 480, &s) && s.x0 == 0 && s.y0 == 240);
        check("une demande absurde est refusee",
              !dkr_scissor_for_player(4, 9, 640, 480, &s) &&
              !dkr_scissor_for_player(0, 0, 640, 480, &s));
        /* Les quadrants ne doivent pas se chevaucher : un pixel dessine deux
           fois appartiendrait a deux joueurs. */
        {
            dkr_scissor a, b;
            dkr_scissor_for_player(4, 0, 640, 480, &a);
            dkr_scissor_for_player(4, 1, 640, 480, &b);
            check("et deux quadrants voisins ne se chevauchent pas", a.x1 <= b.x0);
        }
    }

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
