/* E05-S08 — enveloppement, bornage et filtrage, mesurés pour toutes les tailles.
 *
 * Deux prémisses du ticket tombent avant même de brancher la carte, et il vaut
 * mieux le dire que de mesurer ce qui ne sert pas :
 *
 *   - **DKR n'emploie pas de mipmaps.** `G_TL_TILE` apparaît quinze fois dans la
 *     source, `G_TL_LOD` aucune. Le tiers de mémoire de texture supplémentaire
 *     que le ticket redoutait pour E05-S02 n'existe pas, et les textures
 *     lointaines scintilleront exactement comme sur la console.
 *   - **DKR n'emploie pas le miroir.** Le ticket le décrit comme « très utilisé
 *     pour économiser de la mémoire de texture » ; la source ne contient pas une
 *     seule occurrence de `G_TX_MIRROR`. Dix-huit `G_TX_WRAP`, seize
 *     `G_TX_NOMIRROR`, quatre `G_TX_CLAMP`.
 *
 * Reste donc à vérifier enveloppement et bornage **pour toutes les tailles
 * utilisées** — c'est le critère, et il porte sur les tailles parce que Glide
 * impose des contraintes de dimensions que la N64 n'a pas.
 *
 * ## Comment distinguer les deux modes sans ambiguïté
 *
 * La texture porte une colonne rouge à gauche et rien d'autre. On la dessine sur
 * trois répétitions : en enveloppement, trois colonnes rouges apparaissent ; en
 * bornage, une seule. Le compte départage sans avoir à regarder.
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

static unsigned short g_tex[256 * 256];
static unsigned g_px[640 * 480];

/* Une colonne rouge en `x == 0`, le reste noir. Le motif le plus simple qui
   permette de compter les répétitions. */
static void build(int taille)
{
    int x, y;
    for (y = 0; y < taille; y++) {
        for (x = 0; x < taille; x++) {
            g_tex[y * taille + x] = (unsigned short)
                (0x8000u | ((x == 0) ? (31u << 10) : 0u));
        }
    }
}

static void quad(dkr_render_backend *bk, int w, int h, float repetitions)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)w, (float)w, 0, (float)w, 0 };
    const float ys[6] = { 0, 0, (float)h, 0, (float)h, (float)h };
    const float s1 = 256.0f * repetitions;   /* l'espace de 256 texels de Glide */
    const float ss[6] = { 0, s1, s1, 0, s1, 0 };
    const float ts[6] = { 0, 0, 8.0f, 0, 8.0f, 8.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
        v[i].oow = 1.0f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

/* Compte les groupes de colonnes rouges — pas les colonnes, les groupes : une
   colonne de texture etiree couvre plusieurs pixels, et compter les pixels
   donnerait un nombre qui depend de la taille de la texture plutot que du mode. */
static int groupes_rouges(int w, int y)
{
    int x, n = 0, dedans = 0;
    for (x = 0; x < w; x++) {
        const unsigned c = g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
        const int rouge = ((c >> 16) & 0xFF) > 128u && ((c >> 8) & 0xFF) < 100u;
        if (rouge && !dedans) { n++; }
        dedans = rouge;
    }
    return n;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    const int W = 640, H = 480;
    static const int TAILLES[] = { 4, 8, 16, 32, 64, 128, 256 };
    int rw = 0, rh = 0, i, t;
    unsigned long t_point, t_bilin;
    int wrap_ok = 0, clamp_ok = 0;

    g_out = fopen("D:\\SAMPLING.TXT", "w");
    say("enveloppement et bornage, pour toutes les tailles\n\n");
    say("  DKR n'emploie ni mipmap (G_TL_TILE x15, G_TL_LOD x0)\n");
    say("  ni miroir (G_TX_MIRROR x0 ; WRAP x18, NOMIRROR x16, CLAMP x4)\n");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;

    /* Mise en route — la lecon de E05-S05. */
    for (i = 0; i < 2; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        bk.present(bk.self);
    }

    say("\n%-8s %-14s %-14s\n", "taille", "enveloppement", "bornage");
    for (t = 0; t < 7; t++) {
        const int taille = TAILLES[t];
        dkr_texture_desc d;
        dkr_texture_handle h;
        int n_wrap = -1, n_clamp = -1;

        build(taille);
        memset(&d, 0, sizeof(d));
        d.key = 0x5000ull + (unsigned)taille;
        d.format = DKR_TEXFMT_RGBA5551;
        d.width = taille; d.height = taille;
        d.pixels = g_tex;
        d.size_bytes = (size_t)(taille * taille * 2);
        bk.begin_frame(bk.self, 0x000000);
        h = bk.texture_upload(bk.self, &d);
        if (!h) {
            say("%-8d  refusee au chargement\n", taille);
            g_fails++;
            continue;
        }
        st.texture = h;

        st.wrap_s = DKR_WRAP_REPEAT;
        st.wrap_t = DKR_WRAP_REPEAT;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
            n_wrap = groupes_rouges(rw, rh / 2);
        }

        st.wrap_s = DKR_WRAP_CLAMP;
        st.wrap_t = DKR_WRAP_CLAMP;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0) {
            n_clamp = groupes_rouges(rw, rh / 2);
        }

        say("%-8d %-14d %-14d %s\n", taille, n_wrap, n_clamp,
            (n_wrap == 3 && n_clamp == 1) ? "" : "<-- inattendu");
        if (n_wrap == 3)  { wrap_ok++; }
        if (n_clamp == 1) { clamp_ok++; }
    }

    /* Trois repetitions doivent donner trois colonnes en enveloppement, une
       seule en bornage. Un mode qui ne ferait rien donnerait le meme compte des
       deux cotes, et c'est ce que la comparaison des deux lignes attrape. */
    check("l'enveloppement repete pour les sept tailles", wrap_ok == 7);
    check("le bornage ne repete pas, pour les sept tailles", clamp_ok == 7);

    /* --- Le cout du filtrage --------------------------------------------------- *
     *
     * Le ticket annonce le bilineaire gratuit sur Voodoo. On le verifie plutot
     * que de le croire : c'est peu cher a mesurer, et une surprise ici pese sur
     * tout le budget de remplissage. */
    st.wrap_s = st.wrap_t = DKR_WRAP_REPEAT;
    st.filter = DKR_FILTER_POINT;
    t_point = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
    }
    t_point = GetTickCount() - t_point;

    st.filter = DKR_FILTER_BILINEAR;
    t_bilin = GetTickCount();
    for (i = 0; i < 100; i++) {
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        quad(&bk, W, H, 3.0f);
        bk.present(bk.self);
    }
    t_bilin = GetTickCount() - t_bilin;

    say("\n-- le cout du filtrage --\n");
    say("  100 images au point      : %lu ms\n", t_point);
    say("  100 images en bilineaire : %lu ms\n", t_bilin);
    /* Le seuil est large a dessein : la mesure est quantifiee par l'echange de
       tampons, comme E05-S06 l'a etabli. Ce qu'on cherche ici n'est pas un
       chiffre fin mais l'absence de surprise — un bilineaire qui couterait le
       double se verrait malgre la quantification. */
    check("le bilineaire ne coute pas significativement plus cher",
          t_point == 0 || t_bilin * 100u < t_point * 130u);

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
