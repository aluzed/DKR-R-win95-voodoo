/* E05-S07 — les rectangles 2D, au pixel près.
 *
 * Le ticket est explicite sur la méthode : le décalage de demi-texel « se
 * détermine par l'expérience — dessiner une grille de un pixel sur un fond
 * contrasté et vérifier son alignement — pas par le raisonnement ».
 *
 * Il a raison, et pour une raison qui dépasse Glide : le décalage dépend de la
 * convention d'échantillonnage de la carte, du centre de pixel qu'elle suppose,
 * et de l'arrondi de son interpolateur. Aucune de ces trois choses n'est
 * documentée sur cette machine, et leur composition ne se déduit pas.
 *
 * ## Pourquoi une grille d'un pixel
 *
 * Une texture à traits d'un pixel est l'épreuve la plus sévère qui existe pour
 * l'alignement : au moindre décalage, un trait tombe entre deux pixels et
 * disparaît ou se dédouble. Une texture à motifs larges pardonnerait un demi-
 * texel sans le montrer, et c'est précisément ce qu'il ne faut pas.
 *
 * L'interface est aussi ce que le joueur regarde le plus longtemps. Un décalage
 * d'un demi-texel est le genre de défaut qu'on cesse de voir après quelques
 * heures et qui saute aux yeux de toute personne découvrant le portage.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/clip.h"

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

#define TW 64
#define TH 64
static unsigned short g_grille[TW * TH];
static unsigned g_px[640 * 480];

/* Une grille : un trait blanc d'un texel toutes les quatre colonnes et lignes,
   sur fond noir. Contrastée au maximum, et d'un texel de large. */
static void build_grille(void)
{
    int x, y;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            const int trait = (x % 4 == 0) || (y % 4 == 0);
            g_grille[y * TW + x] = (unsigned short)(0x8000u | (trait ? 0x7FFFu : 0u));
        }
    }
}

/* Un rectangle en coordonnées écran, sans transformation — c'est ce que le RDP
 * fait de ses commandes de rectangle, et y faire passer le pipeline de
 * transformation introduirait une conversion inutile et une occasion d'erreur.
 *
 * `decalage` est le demi-texel à l'essai, en texels de la texture. */
static void rect(dkr_render_backend *bk, float x0, float y0, float x1, float y1,
                 float s0, float t0, float s1, float t1, float decalage,
                 unsigned argb)
{
    dkr_render_vertex v[6];
    const float xs[6] = { x0, x1, x1, x0, x1, x0 };
    const float ys[6] = { y0, y0, y1, y0, y1, y1 };
    const float ss[6] = { s0, s1, s1, s0, s1, s0 };
    const float ts[6] = { t0, t0, t1, t0, t1, t1 };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = (float)((argb >> 16) & 0xFF);
        v[i].g = (float)((argb >> 8) & 0xFF);
        v[i].b = (float)(argb & 0xFF);
        v[i].a = (float)((argb >> 24) & 0xFF);
        v[i].oow = 1.0f;
        /* L'échelle de 256 texels est celle de Glide, mesurée en E05-S02. */
        v[i].tmu[0][DKR_TMU_SOW] = (ss[i] + decalage) * (256.0f / (float)TW);
        v[i].tmu[0][DKR_TMU_TOW] = (ts[i] + decalage) * (256.0f / (float)TH);
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned lire(int x, int y, int w)
{
    return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

/* Combien de colonnes tombent **la ou elles doivent**.
 *
 * La premiere version comptait les colonnes « franches », c'est-a-dire ni
 * grises ni intermediaires. Elle rendait 64 sur 64 pour *tous* les decalages, et
 * ne discriminait rien : en echantillonnage au point il n'y a jamais de valeur
 * intermediaire, seulement des traits deplaces. Une metrique qui ne peut pas
 * echouer ne mesure pas.
 *
 * On compare donc a la grille attendue : a l'echelle un, le texel `x` doit
 * tomber sur le pixel `x`, donc un trait blanc toutes les quatre colonnes en
 * partant de zero. Le bon decalage est celui qui met les traits en face. */
static int colonnes_conformes(int x0, int largeur, int y, int w)
{
    int x, n = 0;
    for (x = x0; x < x0 + largeur; x++) {
        const unsigned r = (lire(x, y, w) >> 16) & 0xFF;
        const int attendu_blanc = ((x - x0) % 4) == 0;
        const int est_blanc = r > 128u;
        if (est_blanc == attendu_blanc) { n++; }
    }
    return n;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   d;
    dkr_texture_handle h;
    const int W = 640, H = 480;
    int rw = 0, rh = 0, i;
    float meilleur = 0.0f;
    int meilleur_score = -1;

    g_out = fopen("D:\\RECT.TXT", "w");
    say("rectangles 2D : demi-texel, jointures, ciseaux\n\n");

    build_grille();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }

    memset(&d, 0, sizeof(d));
    d.key = 0x9999ull; d.format = DKR_TEXFMT_RGBA5551;
    d.width = TW; d.height = TH;
    d.pixels = g_grille; d.size_bytes = sizeof(g_grille);
    bk.begin_frame(bk.self, 0x000000);
    h = bk.texture_upload(bk.self, &d);
    check("la grille se charge", h != 0);

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;     /* point : le bilineaire flouterait la mesure */
    st.wrap_s  = st.wrap_t = DKR_WRAP_CLAMP;
    st.texture = h;

    /* Deux images de mise en route — la lecon de E05-S05. */
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        rect(&bk, 0, 0, 64, 64, 0, 0, (float)TW, (float)TH, 0.0f, 0xFFFFFFFFu);
        bk.present(bk.self);
    }

    /* --- Le demi-texel, cherche plutot que raisonne -------------------------- *
     *
     * La grille est dessinee a l'echelle un — 64 texels sur 64 pixels — a
     * plusieurs decalages. Le bon est celui qui rend le plus de colonnes
     * franches : ni grises, ni manquantes. */
    say("\n-- le decalage de demi-texel --\n");
    say("  grille de 64 texels sur 64 pixels, un trait tous les quatre\n");
    say("%-10s %s\n", "decalage", "colonnes en face sur 64");
    {
        static const float ESSAIS[] = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (i = 0; i < 7; i++) {
            int n;
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            rect(&bk, 0, 0, 64, 64, 0, 0, (float)TW, (float)TH, ESSAIS[i],
                 0xFFFFFFFFu);
            bk.present(bk.self);
            if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
            n = colonnes_conformes(0, 64, 34, rw);
            say("%-10.2f %d\n", (double)ESSAIS[i], n);
            if (n > meilleur_score) { meilleur_score = n; meilleur = ESSAIS[i]; }
        }
        say("\n  meilleur decalage : %.2f texel (%d colonnes en face sur 64)\n",
            (double)meilleur, meilleur_score);
        check("un decalage met toutes les colonnes en face",
              meilleur_score == 64);
    }

    /* --- Les jointures ------------------------------------------------------- *
     *
     * Quatre rectangles adjacents, de couleurs differentes, posés bord a bord.
     * Une regle de remplissage fausse laisse une ligne de fond entre eux, ou les
     * fait se recouvrir. Les fonds composes de tuiles sont ou cela se voit le
     * plus, et l'interface du jeu en est faite. */
    say("\n-- les jointures entre rectangles adjacents --\n");
    {
        static const unsigned COULEURS[4] = {
            0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u
        };
        int noirs = 0, x;
        st.combine = DKR_COMBINE_SHADE;
        st.texture = 0;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        for (i = 0; i < 4; i++) {
            rect(&bk, (float)(100 + i * 50), 100.0f,
                      (float)(100 + (i + 1) * 50), 200.0f,
                 0, 0, 0, 0, 0.0f, COULEURS[i]);
        }
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
            for (x = 100; x < 300; x++) {
                if (lire(x, 150, rw) == 0u) { noirs++; }
            }
            say("  pixels de fond sur la ligne des quatre rectangles : %d sur 200\n",
                noirs);
            check("aucune jointure entre rectangles adjacents", noirs == 0);
            /* Et le controle negatif : les quatre couleurs doivent bien etre
               presentes, sans quoi un seul rectangle couvrant tout passerait. */
            check("et les quatre rectangles sont bien distincts",
                  lire(120, 150, rw) != lire(170, 150, rw) &&
                  lire(170, 150, rw) != lire(220, 150, rw) &&
                  lire(220, 150, rw) != lire(270, 150, rw));
        }
    }

    /* --- Le HUD en ecran partage --------------------------------------------- *
     *
     * Les rectangles d'interface sont contraints par la fenetre de ciseaux de
     * E04-S05. On verifie qu'un rectangle plein ecran, restreint au quadrant
     * d'un joueur, y reste. */
    say("\n-- le HUD en ecran partage --\n");
    {
        static const struct { int joueurs, joueur; const char *nom; } CAS[] = {
            { 2, 0, "2 joueurs, haut" },
            { 2, 1, "2 joueurs, bas" },
            { 4, 0, "4 joueurs, haut-gauche" },
            { 4, 3, "4 joueurs, bas-droit" },
        };
        for (i = 0; i < 4; i++) {
            dkr_scissor sc;
            long peints = 0;
            int j, attendu;
            if (!dkr_scissor_for_player(CAS[i].joueurs, CAS[i].joueur, W, H, &sc)) {
                continue;
            }
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            bk.set_scissor(bk.self, sc.x0, sc.y0, sc.x1, sc.y1);
            rect(&bk, 0, 0, (float)W, (float)H, 0, 0, 0, 0, 0.0f, 0xFFFFFFFFu);
            bk.present(bk.self);
            bk.set_scissor(bk.self, 0, 0, W, H);
            if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
            for (j = 0; j < rw * rh; j++) {
                if ((g_px[j] & 0x00FFFFFFu) != 0u) { peints++; }
            }
            attendu = (sc.x1 - sc.x0) * (sc.y1 - sc.y0);
            say("  %-24s peints %6ld, attendu %6d\n", CAS[i].nom, peints, attendu);
            check(CAS[i].nom, peints == attendu);
        }
    }

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
