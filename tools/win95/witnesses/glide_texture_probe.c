/* La texture arrive-t-elle vraiment sur la carte, et au bon endroit ?
 *
 * `grTexDownloadMipMap` ne rend aucun code d'erreur. `grTexSource` non plus.
 * Toute la chaîne de texture — allocation, transfert, liaison, combineur — est
 * donc **silencieuse de bout en bout** : elle ne peut être vérifiée qu'en
 * regardant l'image. La relecture du tampon (`win95-glide-amorcage.md`) rend
 * cette vérification possible, et c'est la seule qui existe.
 *
 * ## L'épreuve est construite pour distinguer les fautes, pas pour réussir
 *
 * On dessine un quadrilatère plein écran portant un damier 4x4 de couleurs
 * **toutes différentes et choisies exprès**. Chacune identifie une case, donc
 * une plage de coordonnées de texture. Lire quatre points, c'est vérifier d'un
 * coup :
 *
 *   - que la texture est arrivée (sinon : du blanc, ou du bruit) ;
 *   - qu'elle est lue **dans le bon sens** — une inversion de s et t est
 *     invisible sur un damier symétrique et fausse tout le jeu ;
 *   - que le rapport d'aspect est juste — un LOD faux étire la texture, et les
 *     couleurs des quatre points se décalent ensemble ;
 *   - que le combineur prend bien le texel et non la couleur du sommet.
 *
 * Un damier noir et blanc aurait passé toutes ces fautes sans en signaler une.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/tmu.h"

#include <stdarg.h>
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

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (!ok) { g_fails++; }
}

/* Le damier. ARGB 1555 : un bit d'alpha, cinq par composante. */
#define TW 64
#define TH 64
static unsigned short g_texture[TW * TH];

/* Seize couleurs distinctes, une par case. Les quatre qu'on ira lire sont aux
   coins, et sont volontairement très éloignées les unes des autres : rouge,
   vert, bleu, jaune. Une lecture de travers les échangerait de façon
   parfaitement lisible. */
static unsigned short cell_color(int cx, int cy)
{
    static const unsigned short COINS[4] = {
        0x8000 | (31 << 10),                 /* rouge   : coin haut-gauche */
        0x8000 | (31 <<  5),                 /* vert    : coin haut-droit  */
        0x8000 | (31),                       /* bleu    : coin bas-gauche  */
        0x8000 | (31 << 10) | (31 << 5)      /* jaune   : coin bas-droit   */
    };
    if (cx == 0 && cy == 0) { return COINS[0]; }
    if (cx == 3 && cy == 0) { return COINS[1]; }
    if (cx == 0 && cy == 3) { return COINS[2]; }
    if (cx == 3 && cy == 3) { return COINS[3]; }
    /* Les cases intérieures portent des gris échelonnés : elles ne servent pas
       au verdict mais rendent l'image lisible si on la regarde. */
    {
        const unsigned short g = (unsigned short)(4 + (cx + cy * 4) % 24);
        return (unsigned short)(0x8000 | (g << 10) | (g << 5) | g);
    }
}

static void build_texture(void)
{
    int x, y;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            g_texture[y * TW + x] = cell_color(x / (TW / 4), y / (TH / 4));
        }
    }
}

/* Un quadrilatère plein écran, s et t couvrant exactement [0, 64].
 *
 * **Glide veut `s/w` et `t/w`, et son échelle est en texels, pas en unités.**
 * Avec `oow = 1`, `s` va donc de 0 à 64 et non de 0 à 1. Se tromper là-dessus ne
 * plante pas : la texture se répète 64 fois ou n'occupe qu'un texel, ce qui
 * ressemble à un défaut de coordonnées dans le décodeur. */
static void draw_quad(dkr_render_backend *bk, int w, int h, float scale)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, (float)w, (float)w, 0.0f, (float)w, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, (float)h, 0.0f, (float)h, (float)h };
    const float ss[6] = { 0.0f, scale, scale, 0.0f, scale, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, scale, 0.0f, scale, scale };
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

static unsigned g_pixels[640 * 480];

static unsigned read_at(int x, int y, int w)
{
    return g_pixels[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu;
}

/* La carte travaille en 565 ; la texture est en 1555. Un rouge « plein » y vaut
   31/31, qui redevient 255 après réplication. On tolère largement : ce qui est
   testé est l'identité de la couleur, pas sa précision. */
static int dominant(unsigned c, int r, int g, int b)
{
    const int cr = (int)((c >> 16) & 0xFF);
    const int cg = (int)((c >> 8) & 0xFF);
    const int cb = (int)(c & 0xFF);
    const int seuil = 100;
    return ((cr > seuil) == (r != 0)) && ((cg > seuil) == (g != 0)) &&
           ((cb > seuil) == (b != 0));
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_texture_desc   desc;
    dkr_texture_handle handle;
    int rw = 0, rh = 0;
    const int W = 640, H = 480;

    g_out = fopen("D:\\GLTEX.TXT", "w");
    say("la texture arrive-t-elle sur la carte, et dans le bon sens ?\n\n");

    build_texture();

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) {
        say("ECHEC : la carte ne s'ouvre pas\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    bk.begin_frame(bk.self, 0x000000);

    memset(&desc, 0, sizeof(desc));
    desc.key        = 0x1234567890ABCDEFull;
    desc.format     = DKR_TEXFMT_RGBA5551;
    desc.width      = TW;
    desc.height     = TH;
    desc.pixels     = g_texture;
    desc.size_bytes = sizeof(g_texture);

    handle = bk.texture_upload(bk.self, &desc);
    check("la texture obtient un handle", handle != 0);
    say("  handle rendu : %u\n", (unsigned)handle);

    /* La même clé doit rendre le même handle sans retélécharger : c'est tout
       l'intérêt du cache, et l'oublier ferait passer le bus une fois par
       triangle. */
    check("la meme cle rend le meme handle",
          bk.texture_upload(bk.self, &desc) == handle);

    {
        const dkr_tmu *t = dkr_glide_backend_tmu(0);
        if (t) {
            char line[160];
            dkr_tmu_format_status(t, line, sizeof(line));
            say("  %s\n", line);
            check("un seul telechargement pour deux demandes",
                  t->stats.downloads == 1 && t->stats.hits == 1);
            check("l'occupation correspond a une texture 64x64 en 16 bits",
                  dkr_tmu_used(t) == 8192u);
        } else {
            check("l'etat de la TMU est lisible", 0);
        }
    }

    memset(&st, 0, sizeof(st));
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend   = DKR_BLEND_OPAQUE;
    st.depth   = DKR_DEPTH_DISABLED;
    st.cull    = DKR_CULL_NONE;
    st.filter  = DKR_FILTER_POINT;   /* point : le bilineaire melangerait les cases */
    st.wrap_s  = DKR_WRAP_CLAMP;
    st.wrap_t  = DKR_WRAP_CLAMP;
    st.texture = handle;
    bk.set_state(bk.self, &st);

    /* --- A quelle echelle Glide attend-elle s et t ? -------------------------- *
     *
     * La premiere version de ce temoin posait `s` de 0 a 64 pour une texture de
     * 64 texels, et tout l'ecran est sorti rouge — c'est-a-dire la case (0,0)
     * partout. La texture etait donc bien arrivee, mais echantillonnee sur une
     * fraction d'elle-meme.
     *
     * Plutot que de trancher entre les conventions possibles — texels reels,
     * espace normalise sur 256, espace sur 255 — on les essaie toutes et l'on
     * regarde. La bonne echelle est celle qui met les quatre couleurs aux quatre
     * coins ; les autres les melangent d'une facon qui dit dans quel sens on
     * s'est trompe. */
    {
        static const float ECHELLES[] = { 64.0f, 128.0f, 255.0f, 256.0f, 512.0f };
        const int n = (int)(sizeof(ECHELLES) / sizeof(ECHELLES[0]));
        int i, bonne = -1;

        say("\n-- l'echelle des coordonnees de texture --\n");
        for (i = 0; i < n; i++) {
            unsigned hg, hd, bg, bd;
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            draw_quad(&bk, W, H, ECHELLES[i]);
            bk.present(bk.self);
            if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) <= 0) {
                continue;
            }
            hg = read_at(rw / 8,     rh / 8,     rw);
            hd = read_at(rw * 7 / 8, rh / 8,     rw);
            bg = read_at(rw / 8,     rh * 7 / 8, rw);
            bd = read_at(rw * 7 / 8, rh * 7 / 8, rw);
            say("  s,t sur %6.1f : HG 0x%06X  HD 0x%06X  BG 0x%06X  BD 0x%06X %s\n",
                (double)ECHELLES[i], hg, hd, bg, bd,
                (dominant(hg,1,0,0) && dominant(hd,0,1,0) &&
                 dominant(bg,0,0,1) && dominant(bd,1,1,0)) ? "<-- les quatre coins" : "");
            if (dominant(hg,1,0,0) && dominant(hd,0,1,0) &&
                dominant(bg,0,0,1) && dominant(bd,1,1,0)) {
                bonne = i;
            }
        }
        check("une echelle place les quatre couleurs aux quatre coins", bonne >= 0);
        if (bonne >= 0) {
            say("\n  echelle retenue : %g pour une texture de %d texels\n",
                (double)ECHELLES[bonne], TW);
            say("  soit un facteur %g\n", (double)ECHELLES[bonne] / (double)TW);
        }
        /* **Le balayage a servi a trouver ; l'assertion doit porter sur le
           contrat.** Laisser la verification a « une echelle quelconque marche »
           accepterait n'importe quelle valeur future, y compris une qui ne
           correspondrait plus a ce que la chaine produit. C'est
           `DKR_TEXCOORD_SCALE` qui est employe par `clip.c`, donc c'est lui
           qu'il faut eprouver. */
        {
            unsigned hg, hd, bg, bd;
            bk.begin_frame(bk.self, 0x000000);
            bk.set_state(bk.self, &st);
            draw_quad(&bk, W, H, DKR_TEXCOORD_SCALE);
            bk.present(bk.self);
            if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
                hg = read_at(rw / 8,     rh / 8,     rw);
                hd = read_at(rw * 7 / 8, rh / 8,     rw);
                bg = read_at(rw / 8,     rh * 7 / 8, rw);
                bd = read_at(rw * 7 / 8, rh * 7 / 8, rw);
                check("a DKR_TEXCOORD_SCALE, le coin haut-gauche est rouge",
                      dominant(hg, 1, 0, 0));
                check("le coin haut-droit est vert : s croit vers la droite",
                      dominant(hd, 0, 1, 0));
                check("le coin bas-gauche est bleu : t croit vers le bas",
                      dominant(bg, 0, 0, 1));
                check("le coin bas-droit est jaune : ni s ni t ne sont inverses",
                      dominant(bd, 1, 1, 0));
            }
        }
    }

    /* Et le contrôle négatif, sans lequel les précédents ne prouvent rien :
       en mode couleur de sommet, la texture ne doit **pas** apparaître. Si elle
       apparaissait quand même, c'est que la TMU reste liée d'une image sur
       l'autre et que les quatre contrôles ci-dessus mesuraient un état hérité. */
    st.combine = DKR_COMBINE_SHADE;
    st.texture = 0;
    bk.begin_frame(bk.self, 0x000000);
    bk.set_state(bk.self, &st);
    draw_quad(&bk, W, H, 64.0f);
    bk.present(bk.self);
    if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) > 0) {
        const unsigned hg = read_at(rw / 8, rh / 8, rw);
        const unsigned hd = read_at(rw * 7 / 8, rh / 8, rw);
        say("\n  sans texture : haut-gauche 0x%06X  haut-droit 0x%06X\n", hg, hd);
        check("sans texture, l'ecran est uniformement blanc",
              dominant(hg, 1, 1, 1) && hg == hd);
    }

    bk.texture_release(bk.self, handle);
    bk.close(bk.self);

    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
