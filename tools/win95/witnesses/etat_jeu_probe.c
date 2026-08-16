/* Reproduire l'état que le jeu produit, et relire les pixels.
 *
 * L'écran du jeu est noir alors que ses six entrées ont été mesurées saines :
 * couleur de sommet à 255, textures non vides, texture liée, combineur qui lit
 * le texel, mélange opaque, profondeur hors de cause. Le défaut est donc dans
 * ce qui est programmé sur la carte, et l'instrumenter depuis le jeu ne peut
 * plus rien apprendre — on y voit ce qu'on envoie, jamais ce qui en ressort.
 *
 * Ce témoin pose exactement le même état, dessine un triangle connu, et **relit
 * le tampon d'image**. Un compte de pixels non noirs répond en un chiffre à ce
 * que six cycles d'instrumentation n'ont pas tranché.
 *
 * ## Ce qui fait sa valeur : la bissection
 *
 * Il ne pose pas l'état une fois mais **six fois, en le dégradant** — du plus
 * proche du jeu au plus simple. Le premier cas qui écrit des pixels nomme
 * l'élément fautif, puisque c'est le seul qui change entre lui et le précédent.
 * Un témoin qui ne testerait que l'état complet dirait « noir » et n'apprendrait
 * rien de plus que le jeu.
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

static unsigned short g_tex[32 * 32];
static unsigned       g_px[640 * 480];

/* Un damier franc : impossible de confondre « rien dessiné » avec « dessiné
   dans une couleur proche du fond ». */
static void build_tex(void)
{
    int x, y;
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            const int clair = ((x / 4) + (y / 4)) & 1;
            g_tex[y * 32 + x] = (unsigned short)(clair ? 0xFFFFu : 0x8421u);
        }
    }
}

/* Un triangle qui couvre largement l'écran, avec la couleur et les coordonnées
   que le jeu produit : shade à 255, s et t normalisées puis portées à l'échelle
   de 256 de Glide, oow dans la plage relevée sur la machine. */
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

/* **Deux points lus, plutot qu'un compte compare a un fond suppose.**
 *
 * La premiere version comptait les pixels differents de la couleur d'effacement
 * demandee. Elle a rendu 307 200 sur 307 200 — l'ecran entier — pour les six
 * cas, y compris ceux qui ne peuvent pas peindre la meme chose. Un compte
 * sature ne dit pas « tout est peint » : il dit que la couleur de reference est
 * fausse, la carte ne relisant pas dans le format ou on l'avait supposee.
 *
 * On lit donc deux points et l'on imprime leurs valeurs : un dedans le
 * triangle, un dehors. Deux couleurs concretes ne peuvent pas saturer, et leur
 * difference est exactement la question — le triangle a-t-il ete peint. */
static unsigned lire(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   d;
    dkr_texture_handle h;
    const int W = 640, H = 480;
    const unsigned FOND = 0x000040u;   /* un bleu sombre, distinct du noir */
    int rw = 0, rh = 0, got, i;
    int premier_peint = -1;

    g_out = fopen("D:\\ETATJEU.TXT", "w");
    say("l'etat du jeu, repose et relu\n\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }

    build_tex();
    memset(&d, 0, sizeof(d));
    d.key = 0x9001ull;
    d.format = DKR_TEXFMT_RGBA5551;
    d.width = 32; d.height = 32;
    d.pixels = g_tex;
    d.size_bytes = sizeof(g_tex);

    /* Mise en route : la lecon de E05-S05, deux images jetees. */
    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_SHADE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, FOND);
        bk.set_state(bk.self, &st);
        bk.present(bk.self);
    }

    bk.begin_frame(bk.self, FOND);
    h = bk.texture_upload(bk.self, &d);
    check("la texture d'epreuve se charge", h != 0);

    /* --- La bissection --------------------------------------------------------
     *
     * Du plus proche du jeu au plus simple. Le premier cas qui peint nomme
     * l'element fautif : c'est le seul qui change entre lui et le precedent. */
    {
        static const struct {
            const char       *nom;
            dkr_combine_mode  combine;
            dkr_blend_mode    blend;
            dkr_depth_mode    depth;
            int               avec_texture;
            float             oow;
        } CAS[] = {
            { "l'etat du jeu, tel quel",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_TEST_AND_WRITE, 1, 0.001f },
            { "sans profondeur",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 0.001f },
            { "avec un oow de 1 au lieu de 0,001",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 1.0f },
            { "combineur texel seul",
              DKR_COMBINE_TEXTURE, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 1, 1.0f },
            { "sans texture liee",
              DKR_COMBINE_TEXTURE_SHADE_ALPHA, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 0, 1.0f },
            { "couleur du sommet seule",
              DKR_COMBINE_SHADE, DKR_BLEND_OPAQUE,
              DKR_DEPTH_DISABLED, 0, 1.0f },
        };
        const int N = (int)(sizeof(CAS) / sizeof(CAS[0]));
        int c;

        say("\n%-40s %s\n", "cas", "couleurs lues");
        for (c = 0; c < N; c++) {
            memset(&st, 0, sizeof(st));
            st.combine = CAS[c].combine;
            st.blend   = CAS[c].blend;
            st.depth   = CAS[c].depth;
            st.cull    = DKR_CULL_NONE;
            st.filter  = DKR_FILTER_BILINEAR;
            st.wrap_s  = st.wrap_t = DKR_WRAP_REPEAT;
            st.texture = CAS[c].avec_texture ? h : 0;

            bk.begin_frame(bk.self, FOND);
            bk.set_state(bk.self, &st);
            triangle(&bk, CAS[c].oow);
            bk.present(bk.self);

            got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
            if (got > 0) {
                /* 200,200 est bien dans le triangle (40,40)-(600,40)-(40,440) ;
                   620,460 est hors de lui, dans le coin oppose. */
                const unsigned dedans = lire(200, 200, rw);
                const unsigned dehors = lire(620, 460, rw);
                say("%-40s dedans=0x%06X dehors=0x%06X %s\n",
                    CAS[c].nom, dedans, dehors,
                    (dedans != dehors) ? "<-- peint" : "");
                if (dedans != dehors && premier_peint < 0) { premier_peint = c; }
            } else {
                say("%-40s relecture impossible\n", CAS[c].nom);
            }
        }

        say("\n");
        if (premier_peint < 0) {
            say("  aucun cas ne peint : le defaut est en amont de l'etat\n");
        } else {
            say("  premier cas qui peint : %s\n", CAS[premier_peint].nom);
            if (premier_peint > 0) {
                say("  donc l'element fautif est celui que ce cas retire\n");
            }
        }
        /* Le controle qui empeche le temoin de passer a vide : au moins un cas
           doit peindre, sans quoi ce n'est pas l'etat qui est en cause mais
           l'ouverture, la fenetre de ciseaux ou la geometrie — et il faut le
           savoir avant de lire quoi que ce soit au-dessus. */
        check("au moins un cas peint quelque chose", premier_peint >= 0);
    }

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
