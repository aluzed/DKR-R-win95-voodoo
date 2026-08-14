/* E04-S08 — épreuve du rastériseur de référence.
 *
 * Un oracle qu'on vérifie à l'œil n'est pas un oracle : sa valeur entière tient
 * dans la confiance qu'on lui accorde, et « ça a l'air juste » ne se transmet
 * pas. Chaque contrôle ci-dessous compare donc un pixel relu à une valeur
 * **calculée analytiquement**.
 *
 * Le contrôle central est celui de la correction perspective, parce que c'est
 * celui qu'un rastériseur naïf rate en silence : interpoler les coordonnées de
 * texture linéairement en espace écran fait onduler les textures sur toute
 * surface vue en oblique. L'artefact est discret, et c'est précisément le genre
 * de chose qu'on chercherait plus tard à imputer à Glide.
 *
 * Une seule source pour l'hôte et la cible, comme les autres suites du dépôt.
 */
#include "render/software.h"

#include <stdio.h>
#include <stdlib.h>
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
    printf("  %s %-46s attendu %.4f, obtenu %.4f\n",
           ok ? "ok   " : "ECHEC", what, want, got);
    if (g_out) {
        fprintf(g_out, "  %s %-46s attendu %.4f, obtenu %.4f\n",
                ok ? "ok   " : "ECHEC", what, want, got);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

/* Une texture d'intensité de 256 texels, où le texel `i` vaut `i`. Relire un
   pixel donne donc directement la coordonnée `s` qui a servi à l'échantillonner,
   à la quantification près. C'est ce qui rend la correction perspective
   mesurable plutôt que visible. */
static unsigned char g_ramp[256];

static dkr_texture_handle upload_ramp(dkr_render_backend *b)
{
    dkr_texture_desc d;
    int i;
    for (i = 0; i < 256; i++) { g_ramp[i] = (unsigned char)i; }
    memset(&d, 0, sizeof(d));
    d.key        = 0x1234ull;
    d.format     = DKR_TEXFMT_INTENSITY8;
    d.width      = 256;
    d.height     = 1;
    d.pixels     = g_ramp;
    d.size_bytes = sizeof(g_ramp);
    return b->texture_upload(b->self, &d);
}

static unsigned pixel_at(int x, int y)
{
    int w, h;
    const unsigned *fb = dkr_software_framebuffer(&w, &h);
    if (!fb || x < 0 || y < 0 || x >= w || y >= h) { return 0; }
    return fb[(size_t)y * (size_t)w + (size_t)x];
}

int main(void)
{
    dkr_render_backend b;
    dkr_render_state   st;
    dkr_render_vertex  v[6];
    dkr_texture_handle tex;

    g_out = fopen("D:\\SOFTRAS.TXT", "w");

    dkr_render_backend_software(&b);
    check("le backend s'ouvre", b.open(b.self, 128, 64) != 0);

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;

    /* --- Un triangle plein, couleur du sommet ------------------------------ */
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    v[0].x =  10.0f; v[0].y = 10.0f; v[0].r = 255.0f; v[0].a = 255.0f; v[0].oow = 1.0f;
    v[1].x = 110.0f; v[1].y = 10.0f; v[1].r = 255.0f; v[1].a = 255.0f; v[1].oow = 1.0f;
    v[2].x =  60.0f; v[2].y = 54.0f; v[2].r = 255.0f; v[2].a = 255.0f; v[2].oow = 1.0f;
    b.draw_triangles(b.self, v, 1);
    check("un triangle rouge couvre son interieur", (pixel_at(60, 25) & 0x00FF0000u) != 0);
    check("et ne deborde pas au-dehors",            (pixel_at(2, 60)  & 0x00FFFFFFu) == 0);

    /* --- La correction perspective ----------------------------------------- *
     *
     * Un triangle dont les sommets gauche et droit ont des `w` très différents —
     * 1 et 4 — ce qui est le cas d'une surface vue en oblique. Au milieu de
     * l'arête :
     *
     *     1/w = (1-a)/w0 + a/w1       = 0,5 * 1 + 0,5 * 0,25 = 0,625
     *     s/w = (1-a)*s0/w0 + a*s1/w1 = 0       + 0,5 * 0,25 = 0,125
     *     s   = 0,125 / 0,625                                = 0,20
     *
     * **Il y a deux façons de se tromper, et elles ne donnent pas la même
     * valeur.** L'auto-test l'a montré, et l'a montré contre une première
     * version de ce contrôle qui n'en visait qu'une :
     *
     *     0,20    correct — on divise s/w par 1/w
     *     0,125   on interpole s/w et on oublie de diviser
     *     0,50    on range `s` au lieu de `s/w` et on interpole en espace ecran
     *
     * Le contrôle vise donc 0,20 avec une tolérance serrée, et rejette
     * explicitement les deux autres. Une tolérance large aurait accepté 0,125. */
    tex = upload_ramp(&b);
    check("la texture se charge", tex != 0);
    st.combine = DKR_COMBINE_TEXTURE;
    st.texture = tex;
    st.wrap_s  = DKR_WRAP_CLAMP;
    st.wrap_t  = DKR_WRAP_CLAMP;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);

    memset(v, 0, sizeof(v));
    /* Bande horizontale de y=20 à y=40, de x=0 à x=100. */
    v[0].x =   0.0f; v[0].y = 20.0f; v[0].oow = 1.0f;    /* w = 1 */
    v[1].x = 100.0f; v[1].y = 20.0f; v[1].oow = 0.25f;   /* w = 4 */
    v[2].x =   0.0f; v[2].y = 40.0f; v[2].oow = 1.0f;
    v[3] = v[1];
    v[4].x = 100.0f; v[4].y = 40.0f; v[4].oow = 0.25f;
    v[5] = v[2];
    /* `s/w` et non `s` : c'est ce que porte un sommet, ici comme dans Glide. */
    v[0].tmu[0][DKR_TMU_SOW] = 0.0f  * 1.0f;
    v[1].tmu[0][DKR_TMU_SOW] = 1.0f  * 0.25f;
    v[2].tmu[0][DKR_TMU_SOW] = 0.0f  * 1.0f;
    v[3].tmu[0][DKR_TMU_SOW] = 1.0f  * 0.25f;
    v[4].tmu[0][DKR_TMU_SOW] = 1.0f  * 0.25f;
    v[5].tmu[0][DKR_TMU_SOW] = 0.0f  * 1.0f;
    { int i; for (i = 0; i < 6; i++) { v[i].a = 255.0f; } }
    b.draw_triangles(b.self, v, 2);

    {
        /* Au milieu de la bande. L'intensité relue donne `s` directement. */
        const unsigned c = pixel_at(50, 30);
        const double s = (double)(c & 0xFFu) / 255.0;
        check_near("s au milieu, correction perspective", s, 0.20, 0.03);
        /* Les deux erreurs nommees, pour que l'echec se lise sans relire le
           commentaire ci-dessus. */
        check("s n'est pas 0,125 — division oubliee",  s > 0.16);
        check("s n'est pas 0,50 — s range au lieu de s/w", s < 0.35);
    }

    /* --- Enveloppement ------------------------------------------------------ */
    {
        struct { dkr_wrap_mode mode; float s; double want; const char *name; } cases[] = {
            { DKR_WRAP_CLAMP,  1.50f, 1.00, "bornage au-dela de 1" },
            { DKR_WRAP_CLAMP, -0.50f, 0.00, "bornage en deca de 0" },
            { DKR_WRAP_REPEAT, 1.25f, 0.25, "repetition a 1,25" },
            { DKR_WRAP_MIRROR, 1.25f, 0.75, "miroir a 1,25" },
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            st.wrap_s = cases[i].mode;
            b.begin_frame(b.self, 0x000000);
            b.set_state(b.self, &st);
            memset(v, 0, sizeof(v));
            v[0].x =  0.0f; v[0].y = 10.0f;
            v[1].x = 60.0f; v[1].y = 10.0f;
            v[2].x =  0.0f; v[2].y = 50.0f;
            { int k; for (k = 0; k < 3; k++) {
                v[k].oow = 1.0f; v[k].a = 255.0f;
                v[k].tmu[0][DKR_TMU_SOW] = cases[i].s;
            } }
            b.draw_triangles(b.self, v, 1);
            {
                const double s = (double)(pixel_at(10, 20) & 0xFFu) / 255.0;
                check_near(cases[i].name, s, cases[i].want, 0.02);
            }
        }
        st.wrap_s = DKR_WRAP_CLAMP;
    }

    /* --- Filtrage bilineaire ------------------------------------------------ *
     *
     * Entre deux texels voisins de la rampe, l'echantillon a mi-chemin doit
     * valoir leur moyenne. Le controle vise le **demi-texel** : sans lui l'image
     * est decalee d'une demi-largeur de texel, ce qui ne se voit pas sur une
     * mire et se voit parfaitement sur une comparaison d'images. */
    st.filter  = DKR_FILTER_BILINEAR;
    st.combine = DKR_COMBINE_TEXTURE;
    st.texture = tex;
    st.wrap_s  = DKR_WRAP_CLAMP;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    v[0].x =  0.0f; v[0].y = 10.0f;
    v[1].x = 60.0f; v[1].y = 10.0f;
    v[2].x =  0.0f; v[2].y = 50.0f;
    { int k; for (k = 0; k < 3; k++) {
        v[k].oow = 1.0f; v[k].a = 255.0f;
        /* Pile entre le texel 100 et le texel 101 : (100,5)/256. */
        v[k].tmu[0][DKR_TMU_SOW] = 100.5f / 256.0f;
    } }
    b.draw_triangles(b.self, v, 1);
    check_near("bilineaire : la moyenne de deux texels voisins",
               (double)(pixel_at(10, 20) & 0xFFu), 100.5, 1.0);
    st.filter = DKR_FILTER_POINT;

    /* --- Profondeur --------------------------------------------------------- */
    st.combine = DKR_COMBINE_SHADE;
    st.texture = 0;
    st.depth   = DKR_DEPTH_TEST_AND_WRITE;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    /* Un triangle vert loin, puis un rouge pres : le rouge doit gagner. */
    v[0].x = 10.0f; v[0].y = 10.0f; v[0].z = 0.8f; v[0].g = 255.0f;
    v[1].x = 90.0f; v[1].y = 10.0f; v[1].z = 0.8f; v[1].g = 255.0f;
    v[2].x = 10.0f; v[2].y = 50.0f; v[2].z = 0.8f; v[2].g = 255.0f;
    v[3].x = 10.0f; v[3].y = 10.0f; v[3].z = 0.2f; v[3].r = 255.0f;
    v[4].x = 90.0f; v[4].y = 10.0f; v[4].z = 0.2f; v[4].r = 255.0f;
    v[5].x = 10.0f; v[5].y = 50.0f; v[5].z = 0.2f; v[5].r = 255.0f;
    { int i; for (i = 0; i < 6; i++) { v[i].oow = 1.0f; v[i].a = 255.0f; } }
    b.draw_triangles(b.self, v, 2);
    check("le plus proche masque le plus lointain",
          (pixel_at(30, 20) & 0x00FF0000u) != 0 &&
          (pixel_at(30, 20) & 0x0000FF00u) == 0);

    /* L'ordre inverse doit donner le meme resultat : c'est ce que le tampon de
       profondeur promet, et l'oublier donne un rendu qui depend de l'ordre
       d'emission — un defaut tres penible a diagnostiquer plus tard. */
    b.begin_frame(b.self, 0x000000);
    { dkr_render_vertex tmp[3]; memcpy(tmp, v, sizeof(tmp));
      memcpy(v, v + 3, sizeof(tmp)); memcpy(v + 3, tmp, sizeof(tmp)); }
    b.draw_triangles(b.self, v, 2);
    check("et le resultat ne depend pas de l'ordre d'emission",
          (pixel_at(30, 20) & 0x00FF0000u) != 0 &&
          (pixel_at(30, 20) & 0x0000FF00u) == 0);

    /* --- Test alpha --------------------------------------------------------- */
    st.depth = DKR_DEPTH_DISABLED;
    st.alpha_test = 1;
    st.alpha_reference = 128;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    memset(v, 0, sizeof(v));
    v[0].x = 10.0f; v[0].y = 10.0f;
    v[1].x = 90.0f; v[1].y = 10.0f;
    v[2].x = 10.0f; v[2].y = 50.0f;
    { int i; for (i = 0; i < 3; i++) {
        v[i].oow = 1.0f; v[i].b = 255.0f; v[i].a = 64.0f; } }
    b.draw_triangles(b.self, v, 1);
    check("le test alpha rejette sous le seuil", (pixel_at(30, 20) & 0x00FFFFFFu) == 0);
    { int i; for (i = 0; i < 3; i++) { v[i].a = 200.0f; } }
    b.draw_triangles(b.self, v, 1);
    check("et laisse passer au-dessus", (pixel_at(30, 20) & 0x000000FFu) != 0);
    st.alpha_test = 0;

    /* --- Melange ------------------------------------------------------------ */
    st.blend = DKR_BLEND_ALPHA;
    b.begin_frame(b.self, 0x000000);
    b.set_state(b.self, &st);
    { int i; for (i = 0; i < 3; i++) {
        v[i].r = 255.0f; v[i].g = 0.0f; v[i].b = 0.0f; v[i].a = 128.0f; } }
    b.draw_triangles(b.self, v, 1);
    {
        /* Rouge a moitie sur du noir : environ 128. */
        const unsigned red = (pixel_at(30, 20) >> 16) & 0xFFu;
        check_near("melange alpha a moitie", (double)red, 128.0, 4.0);
    }
    st.blend = DKR_BLEND_OPAQUE;

    /* --- Fenetre de ciseaux -------------------------------------------------- */
    b.begin_frame(b.self, 0x000000);
    b.set_scissor(b.self, 40, 0, 60, 64);
    b.set_state(b.self, &st);
    { int i; for (i = 0; i < 3; i++) { v[i].a = 255.0f; } }
    v[0].x = 0.0f; v[0].y = 0.0f; v[1].x = 120.0f; v[1].y = 0.0f;
    v[2].x = 0.0f; v[2].y = 60.0f;
    b.draw_triangles(b.self, v, 1);
    check("la fenetre de ciseaux coupe a gauche",  (pixel_at(20, 10) & 0x00FFFFFFu) == 0);
    check("elle laisse passer au milieu",          (pixel_at(50, 10) & 0x00FF0000u) != 0);
    check("et coupe a droite",                     (pixel_at(80, 10) & 0x00FFFFFFu) == 0);
    b.set_scissor(b.self, 0, 0, 128, 64);

    /* --- Rectangle plein ----------------------------------------------------- */
    b.begin_frame(b.self, 0x000000);
    b.fill_rect(b.self, 10, 10, 30, 30, 0x00FF00);
    check("le rectangle est rempli",     (pixel_at(20, 20) & 0x0000FF00u) != 0);
    check("et borne a droite exclue",    (pixel_at(30, 20) & 0x00FFFFFFu) == 0);

    /* --- Sortie en fichier ---------------------------------------------------- */
    {
        const char *path = "D:\\SOFTRAS.BMP";
        FILE *f;
        check("l'image s'ecrit en BMP", dkr_software_write_bmp(path) != 0);
        f = fopen(path, "rb");
        if (f) {
            unsigned char head[26];
            const size_t got = fread(head, 1, sizeof(head), f);
            fclose(f);
            check("le fichier porte une entete BMP",
                  got == sizeof(head) && head[0] == 'B' && head[1] == 'M');
            check("et les dimensions du tampon",
                  got == sizeof(head) &&
                  *(const int *)&head[18] == 128 && *(const int *)&head[22] == 64);
        } else {
            check("le fichier BMP se relit", 0);
        }
    }

    b.close(b.self);
    check("la fermeture libere sans planter", 1);

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
