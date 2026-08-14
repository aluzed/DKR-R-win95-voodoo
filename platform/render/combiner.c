/* E05-S03 — mise en œuvre. Le contrat et l'analyse sont dans `combiner.h`. */
#include "combiner.h"
#include "combiner_table.h"

#include <string.h>

/* --- Les sources, lues position par position -------------------------------- *
 *
 * Une seule table de correspondance serait fausse. La valeur 6 signifie `1` en
 * position `a`, `CENTER` en `b`, `SCALE` en `c` et `1` en `d` ; la valeur 1
 * signifie `TEXEL0` en couleur mais `TEXEL0_ALPHA` en alpha. Chaque position a
 * donc sa fonction, et c'est délibérément verbeux : la version compacte de ce
 * code serait la version fausse.
 *
 * `out[3]` reçoit une couleur ; les sources scalaires remplissent les quatre
 * composantes, ce qui permet à l'appelant de multiplier sans se demander s'il
 * tient une couleur ou un scalaire. */

static void splat(float v, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = v;
}

static void copy4(const float in[4], float out[4])
{
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = in[3];
}

static void rgb_a(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    case 6:  splat(255.0f, out);          break;   /* 1 */
    /* 7 = NOISE. Le rastériseur n'en produit pas : une source aléatoire rendrait
       la comparaison avec la carte impossible, et DKR ne l'emploie pas. Zéro
       est le choix qui se remarque le moins si elle apparaissait un jour, et
       l'inventaire dirait qu'elle est apparue. */
    default: splat(0.0f, out);            break;
    }
}

static void rgb_b(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    /* 6 = CENTER, 7 = K4 : registres de conversion de chrominance, que DKR
       n'emploie pas. */
    default: splat(0.0f, out);            break;
    }
}

static void rgb_c(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    case 6:  splat(255.0f, out);          break;   /* SCALE, sans registre ici */
    case 7:  splat(in->combined[3], out);    break;
    case 8:  splat(in->texel0[3], out);      break;
    case 9:  splat(in->texel1[3], out);      break;
    case 10: splat(in->primitive[3], out);   break;
    case 11: splat(in->shade[3], out);       break;
    case 12: splat(in->environment[3], out); break;
    case 13: splat(in->lod_fraction, out);   break;
    case 14: splat(in->prim_lod_frac, out);  break;
    case 15: splat(in->k5, out);             break;
    default: splat(0.0f, out);               break;
    }
}

static void rgb_d(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    case 6:  splat(255.0f, out);          break;
    default: splat(0.0f, out);            break;
    }
}

/* Les termes d'alpha. `a`, `b` et `d` partagent une table ; `c` en a une autre,
   où la valeur 0 signifie `LOD_FRACTION` et non `COMBINED`. */
static float alpha_abd(const dkr_combiner_inputs *in, unsigned v)
{
    switch (v) {
    case 0:  return in->combined[3];
    case 1:  return in->texel0[3];
    case 2:  return in->texel1[3];
    case 3:  return in->primitive[3];
    case 4:  return in->shade[3];
    case 5:  return in->environment[3];
    case 6:  return 255.0f;
    default: return 0.0f;
    }
}

static float alpha_c(const dkr_combiner_inputs *in, unsigned v)
{
    switch (v) {
    case 0:  return in->lod_fraction;
    case 1:  return in->texel0[3];
    case 2:  return in->texel1[3];
    case 3:  return in->primitive[3];
    case 4:  return in->shade[3];
    case 5:  return in->environment[3];
    case 6:  return in->prim_lod_frac;
    default: return 0.0f;
    }
}

static float clamp255(float v)
{
    return (v < 0.0f) ? 0.0f : (v > 255.0f ? 255.0f : v);
}

void dkr_combiner_eval(const dkr_combiner *c, int cycle,
                       const dkr_combiner_inputs *in, float out[4])
{
    float a[4], b[4], k[4], d[4];
    const dkr_cc_stage *rs, *as;
    int i;

    if (!c || !in || !out || cycle < 0 || cycle > 1) { return; }
    rs = &c->rgb[cycle];
    as = &c->alpha[cycle];

    rgb_a(in, rs->a, a);
    rgb_b(in, rs->b, b);
    rgb_c(in, rs->c, k);
    rgb_d(in, rs->d, d);

    /* `(a - b) * c + d`, en 0..255. Le facteur `c` est lui-même en 0..255 et
       doit donc être ramené : le RDP le traite comme une fraction. */
    for (i = 0; i < 3; i++) {
        out[i] = clamp255((a[i] - b[i]) * (k[i] / 255.0f) + d[i]);
    }
    {
        const float aa = alpha_abd(in, as->a);
        const float ab = alpha_abd(in, as->b);
        const float ak = alpha_c(in, as->c);
        const float ad = alpha_abd(in, as->d);
        out[3] = clamp255((aa - ab) * (ak / 255.0f) + ad);
    }
}

void dkr_combiner_eval_all(const dkr_combiner *c, dkr_cycle_type cycle_type,
                           const dkr_combiner_inputs *in, float out[4])
{
    dkr_combiner_inputs work;

    if (!c || !in || !out) { return; }
    work = *in;
    /* Au premier cycle, `COMBINED` n'a pas de valeur. Le RDP y lit le résultat
       du triangle précédent, ce qui est indéfini du point de vue du programme ;
       zéro est la seule valeur qui rende le rendu reproductible, et c'est ce
       que la comparaison avec la carte exige. */
    work.combined[0] = work.combined[1] = work.combined[2] = work.combined[3] = 0.0f;

    dkr_combiner_eval(c, 0, &work, out);
    if (cycle_type == DKR_CYCLE_2) {
        copy4(out, work.combined);
        dkr_combiner_eval(c, 1, &work, out);
    }
}

/* --- La table ---------------------------------------------------------------- */

const char *dkr_cc_categorie_texte(dkr_cc_categorie c)
{
    switch (c) {
    case DKR_CC_EXACTE:      return "exacte";
    case DKR_CC_MULTIPASSE:  return "multipasse";
    case DKR_CC_APPROCHEE:   return "approchee";
    default:                 return "deux texels (E05-S04)";
    }
}

#define CC_COUNT ((int)(sizeof(CC_TABLE) / sizeof(CC_TABLE[0])))

int dkr_cc_table_count(void) { return CC_COUNT; }

const dkr_cc_entree *dkr_cc_table_at(int index)
{
    if (index < 0 || index >= CC_COUNT) { return 0; }
    return &CC_TABLE[index];
}

unsigned long long dkr_cc_entree_key(const dkr_cc_entree *e)
{
    dkr_combiner c;
    if (!e) { return 0; }
    memset(&c, 0, sizeof(c));
    c.rgb[0]   = e->rgb[0];   c.rgb[1]   = e->rgb[1];
    c.alpha[0] = e->alpha[0]; c.alpha[1] = e->alpha[1];
    return dkr_rdp_combiner_key(&c, e->cycle);
}

const dkr_cc_entree *dkr_cc_lookup(unsigned long long key)
{
    int i;
    /* Une recherche linéaire sur vingt-neuf entrées, appelée au changement
       d'état et non par triangle. Une table de hachage ne gagnerait rien de
       mesurable et se relirait moins bien. */
    for (i = 0; i < CC_COUNT; i++) {
        if (dkr_cc_entree_key(&CC_TABLE[i]) == key) {
            return &CC_TABLE[i];
        }
    }
    return 0;
}

const dkr_cc_reglage *dkr_cc_repli(void)
{
    /* Texture modulée par la couleur du sommet : `SCALE_OTHER` avec la couleur
       locale en facteur. C'est le comportement le plus fréquent de l'inventaire,
       donc celui qui a le plus de chances d'être juste sur une configuration
       qu'on n'a pas prévue.
     *
     * Le choix se lit contre ses deux alternatives, écartées toutes deux :
     * ne rien dessiner ferait disparaître un décor sans laisser de trace, et
     * peindre en magenta vif rendrait le jeu injouable au premier combineur
     * oublié. « Visible mais non aberrant » est ce que le ticket demande. */
    static const dkr_cc_reglage repli = {
        3, 1, 0, 1,      /* couleur : SCALE_OTHER, facteur LOCAL, local itéré, other texture */
        3, 1, 0, 1,      /* alpha : idem */
        1, 0,            /* étage de texture : DECAL */
        1
    };
    return &repli;
}
