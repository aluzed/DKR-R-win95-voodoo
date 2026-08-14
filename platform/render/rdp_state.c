/* E04-S06 — mise en œuvre. Le contrat est dans `rdp_state.h`. */
#include "rdp_state.h"

#include <string.h>

/* --- Décodage du combineur -------------------------------------------------- *
 *
 * Les décalages viennent des macros `GCCc0w0`, `GCCc1w0`, `GCCc0w1` et `GCCc1w1`
 * de `gbi.h`, **lues et non récitées**. Ils sont entrelacés au point qu'aucune
 * mémoire ne les rend correctement :
 *
 *   w0 (24 bits bas)   a0 << 20 (4)   c0 << 15 (5)   Aa0 << 12 (3)   Ac0 << 9 (3)
 *                      a1 <<  5 (4)   c1 <<  0 (5)
 *
 *   w1                 b0 << 28 (4)   b1 << 24 (4)   Aa1 << 21 (3)   Ac1 << 18 (3)
 *                      d0 << 15 (3)   Ab0 << 12 (3)  Ad0 <<  9 (3)   d1 <<  6 (3)
 *                      Ab1 << 3 (3)   Ad1 <<  0 (3)
 *
 * Noter que les champs RGB `a` et `b` font quatre bits, `c` cinq, `d` trois —
 * et que les champs alpha en font trois. Une largeur uniforme supposée est
 * l'erreur qui fait décoder juste les cas simples et faux les autres.
 */
static unsigned field(unsigned word, int shift, int bits)
{
    return (word >> shift) & ((1u << bits) - 1u);
}

void dkr_rdp_decode_combine(unsigned int w0, unsigned int w1, dkr_combiner *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->rgb[0].a   = (unsigned char)field(w0, 20, 4);
    out->rgb[0].c   = (unsigned char)field(w0, 15, 5);
    out->alpha[0].a = (unsigned char)field(w0, 12, 3);
    out->alpha[0].c = (unsigned char)field(w0,  9, 3);
    out->rgb[1].a   = (unsigned char)field(w0,  5, 4);
    out->rgb[1].c   = (unsigned char)field(w0,  0, 5);

    out->rgb[0].b   = (unsigned char)field(w1, 28, 4);
    out->rgb[1].b   = (unsigned char)field(w1, 24, 4);
    out->alpha[1].a = (unsigned char)field(w1, 21, 3);
    out->alpha[1].c = (unsigned char)field(w1, 18, 3);
    out->rgb[0].d   = (unsigned char)field(w1, 15, 3);
    out->alpha[0].b = (unsigned char)field(w1, 12, 3);
    out->alpha[0].d = (unsigned char)field(w1,  9, 3);
    out->rgb[1].d   = (unsigned char)field(w1,  6, 3);
    out->alpha[1].b = (unsigned char)field(w1,  3, 3);
    out->alpha[1].d = (unsigned char)field(w1,  0, 3);
}

/* --- Décodage des autres modes ---------------------------------------------- *
 *
 * Décalages de `G_MDSFT_*`, également lus dans `gbi.h` :
 *
 *   mode_h   TEXTFILT 12 (2)   TEXTLOD 16 (1)   TEXTPERSP 19 (1)
 *            CYCLETYPE 20 (2)  TEXTDETAIL 17 (2)
 *   mode_l   ALPHACOMPARE 0 (2)  ZSRCSEL 2 (1)  RENDERMODE 3 (29)
 */
void dkr_rdp_decode_othermode(unsigned int mode_h, unsigned int mode_l,
                              dkr_rdp_state *out)
{
    unsigned filt;

    if (!out) {
        return;
    }
    out->cycle = (dkr_cycle_type)field(mode_h, 20, 2);

    /* G_TF_POINT vaut 0, G_TF_BILERP 2, G_TF_AVERAGE 3 — et **il n'y a pas de
       valeur 1**. Traiter le champ comme un booléen donnerait le filtrage
       bilinéaire pour `AVERAGE`, ce qui est presque juste et donc difficile à
       voir. On distingue, et `AVERAGE` tombe sur bilinéaire faute de mieux côté
       Glide, ce que la traduction signale. */
    filt = field(mode_h, 12, 2);
    out->filter = (filt == 0) ? DKR_FILTER_POINT : DKR_FILTER_BILINEAR;

    out->texture_lod    = (unsigned char)field(mode_h, 16, 1);
    out->texture_detail = (unsigned char)field(mode_h, 17, 2);
    out->texture_persp  = (unsigned char)field(mode_h, 19, 1);

    out->alpha_compare = (unsigned char)field(mode_l, 0, 2);
    out->z_source      = (unsigned char)field(mode_l, 2, 1);
    out->render_mode   = mode_l >> 3;

    /* Les bits du blender. `Z_CMP` et `Z_UPD` sont à 4 et 5 du mot complet,
       donc à 1 et 2 une fois `RENDERMODE` décalé. `G_RM_FOG_SHADE_A` se
       reconnaît à sa source de mélange, que l'on ne décode pas ici : le
       brouillard est signalé par le bit `G_FOG` du mode géométrique du RSP, et
       c'est le décodeur qui le porte. */
    out->z_test  = (unsigned char)((mode_l >> 4) & 1u);
    out->z_write = (unsigned char)((mode_l >> 5) & 1u);
    out->fog     = 0;
}

/* --- Forme canonique -------------------------------------------------------- *
 *
 * Une clé de 64 bits. Les seize champs y sont rangés à des positions fixes,
 * sans compression : deux configurations différentes ne peuvent pas collisionner,
 * et l'on peut relire une clé à la main quand il le faut.
 *
 *   bits  0..3   rgb0.a      bits 32..35  rgb1.a
 *   bits  4..8   rgb0.c      bits 36..40  rgb1.c
 *   bits  9..12  rgb0.b      bits 41..44  rgb1.b
 *   bits 13..15  rgb0.d      bits 45..47  rgb1.d
 *   bits 16..18  a0.a        bits 48..50  a1.a
 *   bits 19..21  a0.b        bits 51..53  a1.b
 *   bits 22..24  a0.c        bits 54..56  a1.c
 *   bits 25..27  a0.d        bits 57..59  a1.d
 *   bits 28..29  le mode de cycle
 *
 * **Le mode de cycle en fait partie**, et ce n'est pas un détail : le même mot
 * de combineur en un cycle et en deux ne produit pas la même image, le second
 * étage n'étant pas évalué dans le premier cas. Les confondre donnerait une
 * table de correspondance qui rend la mauvaise image sans jamais se plaindre.
 */
unsigned long long dkr_rdp_combiner_key(const dkr_combiner *c,
                                        dkr_cycle_type cycle)
{
    unsigned long long k = 0;
    if (!c) {
        return 0;
    }
    k |= (unsigned long long)(c->rgb[0].a   & 0x0Fu) << 0;
    k |= (unsigned long long)(c->rgb[0].c   & 0x1Fu) << 4;
    k |= (unsigned long long)(c->rgb[0].b   & 0x0Fu) << 9;
    k |= (unsigned long long)(c->rgb[0].d   & 0x07u) << 13;
    k |= (unsigned long long)(c->alpha[0].a & 0x07u) << 16;
    k |= (unsigned long long)(c->alpha[0].b & 0x07u) << 19;
    k |= (unsigned long long)(c->alpha[0].c & 0x07u) << 22;
    k |= (unsigned long long)(c->alpha[0].d & 0x07u) << 25;
    k |= (unsigned long long)((unsigned)cycle & 0x03u) << 28;

    k |= (unsigned long long)(c->rgb[1].a   & 0x0Fu) << 32;
    k |= (unsigned long long)(c->rgb[1].c   & 0x1Fu) << 36;
    k |= (unsigned long long)(c->rgb[1].b   & 0x0Fu) << 41;
    k |= (unsigned long long)(c->rgb[1].d   & 0x07u) << 45;
    k |= (unsigned long long)(c->alpha[1].a & 0x07u) << 48;
    k |= (unsigned long long)(c->alpha[1].b & 0x07u) << 51;
    k |= (unsigned long long)(c->alpha[1].c & 0x07u) << 54;
    k |= (unsigned long long)(c->alpha[1].d & 0x07u) << 57;
    return k;
}

/* --- Les configurations répertoriées ---------------------------------------- *
 *
 * Cette table est **importée** de l'inventaire du portage natif voisin, et pas
 * encore vérifiée sur ce portage. La distinction est importante et le ticket la
 * pose : « l'inventaire est un point de départ solide, pas une vérité
 * importée ».
 *
 * Elle ne peut d'ailleurs pas être vérifiée ici par la même méthode. Le voisin
 * l'a dérivée des **sources C de la décomposition**, en résolvant les macros
 * `G_CC_*` des tables de réglages ; ce portage-ci n'a pas ces sources — il
 * travaille depuis du MIPS recompilé. Son équivalent est l'instrumentation à
 * l'exécution, qui demande une partie complète, donc la ROM.
 *
 * D'ici là, `dkr_rdp_combiner_name` rend `NULL` pour tout ce qu'elle ne connaît
 * pas, et c'est ce signalement qui compte : une configuration manquée ne se voit
 * pas au décodage mais à l'écran, sous forme d'une surface d'une couleur
 * inattendue, éventuellement dans un seul niveau.
 */
typedef struct {
    const char        *name;
    unsigned char      rgb0[4], alpha0[4], rgb1[4], alpha1[4];
    dkr_cycle_type     cycle;
    int                texel_count;
} known_combiner;

/* Les entrées sont écrites sous la forme `(a, b, c, d)` du RDP, dans l'ordre où
   `gDPSetCombineLERP` les prend. Seules les configurations dont l'inventaire
   voisin donne la composition exacte figurent ici ; les autres attendent la
   vérification à l'exécution plutôt que d'être devinées. */
static const known_combiner KNOWN[] = {
    /* G_CC_SHADE : (0, 0, 0, SHADE), alpha (0, 0, 0, SHADE) */
    { "G_CC_SHADE",
      { 0, 0, 0, 4 }, { 0, 0, 0, 4 }, { 0, 0, 0, 4 }, { 0, 0, 0, 4 },
      DKR_CYCLE_1, 0 },
    /* G_CC_PRIMITIVE : (0, 0, 0, PRIMITIVE) */
    { "G_CC_PRIMITIVE",
      { 0, 0, 0, 3 }, { 0, 0, 0, 3 }, { 0, 0, 0, 3 }, { 0, 0, 0, 3 },
      DKR_CYCLE_1, 0 },
    /* G_CC_ENVIRONMENT : (0, 0, 0, ENVIRONMENT) */
    { "G_CC_ENVIRONMENT",
      { 0, 0, 0, 5 }, { 0, 0, 0, 5 }, { 0, 0, 0, 5 }, { 0, 0, 0, 5 },
      DKR_CYCLE_1, 0 },
    /* G_CC_DECALRGB : (0, 0, 0, TEXEL0), alpha (0, 0, 0, SHADE) */
    { "G_CC_DECALRGB",
      { 0, 0, 0, 1 }, { 0, 0, 0, 4 }, { 0, 0, 0, 1 }, { 0, 0, 0, 4 },
      DKR_CYCLE_1, 1 },
    /* G_CC_DECALRGBA : (0, 0, 0, TEXEL0), alpha (0, 0, 0, TEXEL0) */
    { "G_CC_DECALRGBA",
      { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 },
      DKR_CYCLE_1, 1 },
    /* G_CC_MODULATEIA : (TEXEL0, 0, SHADE, 0), alpha (TEXEL0, 0, SHADE, 0) */
    { "G_CC_MODULATEIA",
      { 1, 0, 4, 0 }, { 1, 0, 4, 0 }, { 1, 0, 4, 0 }, { 1, 0, 4, 0 },
      DKR_CYCLE_1, 1 },
    /* G_CC_MODULATEIDECALA : (TEXEL0, 0, SHADE, 0), alpha (0, 0, 0, TEXEL0) */
    { "G_CC_MODULATEIDECALA",
      { 1, 0, 4, 0 }, { 0, 0, 0, 1 }, { 1, 0, 4, 0 }, { 0, 0, 0, 1 },
      DKR_CYCLE_1, 1 },
    /* G_CC_MODULATERGBA : identique a MODULATEIA pour le RGB. */
    { "G_CC_MODULATERGBA",
      { 1, 0, 4, 0 }, { 1, 0, 4, 0 }, { 1, 0, 4, 0 }, { 1, 0, 4, 0 },
      DKR_CYCLE_2, 1 },
};

#define KNOWN_COUNT ((int)(sizeof(KNOWN) / sizeof(KNOWN[0])))

static unsigned long long key_of_known(const known_combiner *k)
{
    dkr_combiner c;
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = k->rgb0[0]; c.rgb[0].b = k->rgb0[1];
    c.rgb[0].c = k->rgb0[2]; c.rgb[0].d = k->rgb0[3];
    c.alpha[0].a = k->alpha0[0]; c.alpha[0].b = k->alpha0[1];
    c.alpha[0].c = k->alpha0[2]; c.alpha[0].d = k->alpha0[3];
    c.rgb[1].a = k->rgb1[0]; c.rgb[1].b = k->rgb1[1];
    c.rgb[1].c = k->rgb1[2]; c.rgb[1].d = k->rgb1[3];
    c.alpha[1].a = k->alpha1[0]; c.alpha[1].b = k->alpha1[1];
    c.alpha[1].c = k->alpha1[2]; c.alpha[1].d = k->alpha1[3];
    return dkr_rdp_combiner_key(&c, k->cycle);
}

const char *dkr_rdp_combiner_name(unsigned long long key)
{
    int i;
    for (i = 0; i < KNOWN_COUNT; i++) {
        if (key_of_known(&KNOWN[i]) == key) {
            return KNOWN[i].name;
        }
    }
    return NULL;
}

int dkr_rdp_known_count(void) { return KNOWN_COUNT; }

int dkr_rdp_known_at(int index, unsigned long long *key, const char **name,
                     int *texel_count)
{
    if (index < 0 || index >= KNOWN_COUNT) {
        return 0;
    }
    if (key)         { *key = key_of_known(&KNOWN[index]); }
    if (name)        { *name = KNOWN[index].name; }
    if (texel_count) { *texel_count = KNOWN[index].texel_count; }
    return 1;
}

/* --- Traduction vers l'état abstrait ---------------------------------------- */

/* Un étage lit-il un texel, et lequel ? */
static int stage_reads(const dkr_cc_stage *s, unsigned char input)
{
    return s->a == input || s->b == input || s->c == input || s->d == input;
}

void dkr_rdp_to_render_state(const dkr_rdp_state *rdp, dkr_render_state *out,
                             int *exact)
{
    int uses_texel0, uses_texel1, uses_shade;
    int faithful = 1;

    if (!rdp || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    uses_texel0 = stage_reads(&rdp->combiner.rgb[0], DKR_CC_TEXEL0) ||
                  stage_reads(&rdp->combiner.rgb[1], DKR_CC_TEXEL0);
    uses_texel1 = stage_reads(&rdp->combiner.rgb[0], DKR_CC_TEXEL1) ||
                  stage_reads(&rdp->combiner.rgb[1], DKR_CC_TEXEL1);
    uses_shade  = stage_reads(&rdp->combiner.rgb[0], DKR_CC_SHADE) ||
                  stage_reads(&rdp->combiner.rgb[1], DKR_CC_SHADE);

    if (uses_texel0 && uses_shade) {
        /* L'alpha vient-il du texel ou du shading ? La distinction décide de la
           transparence des découpes, et se tromper donne des bords francs là où
           le jeu attend un dégradé. */
        const int alpha_from_texel =
            stage_reads(&rdp->combiner.alpha[0], DKR_CC_TEXEL0) ||
            stage_reads(&rdp->combiner.alpha[1], DKR_CC_TEXEL0);
        out->combine = alpha_from_texel ? DKR_COMBINE_TEXTURE_SHADE_ALPHA
                                        : DKR_COMBINE_TEXTURE_SHADE;
    } else if (uses_texel0) {
        out->combine = DKR_COMBINE_TEXTURE;
    } else {
        out->combine = DKR_COMBINE_SHADE;
    }

    /* **Ce que l'interface ne sait pas dire.** Deux texels demandent deux TMU ou
       une seconde passe (E05-S04) ; un second étage arbitraire n'a pas
       d'équivalent dans les quatre modes de E04-S01. On rend le mode le plus
       proche et l'on **prévient**, parce qu'une traduction approchée qui ne
       s'annonce pas produit une image plausible et fausse — le pire des
       résultats pour un portage dont l'oracle est l'image. */
    if (uses_texel1) {
        faithful = 0;
    }
    if (rdp->cycle == DKR_CYCLE_2) {
        faithful = 0;
    }

    out->filter = rdp->filter;
    out->depth  = rdp->z_test ? (rdp->z_write ? DKR_DEPTH_TEST_AND_WRITE
                                              : DKR_DEPTH_TEST_ONLY)
                              : DKR_DEPTH_DISABLED;
    out->alpha_test      = (unsigned char)(rdp->alpha_compare != 0);
    out->alpha_reference = 128;      /* le seuil réel vient de G_SETPRIMCOLOR */
    out->fog_enabled     = rdp->fog;
    out->blend           = DKR_BLEND_ALPHA;
    out->cull            = DKR_CULL_NONE;   /* porté par le mode géométrique */
    out->wrap_s = out->wrap_t = DKR_WRAP_REPEAT;

    if (exact) {
        *exact = faithful;
    }
}
