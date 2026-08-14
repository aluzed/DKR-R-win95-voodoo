/* E04-S06 — décodage de l'état RDP, et sa traduction vers l'état abstrait.
 *
 * Le RDP est piloté par un état dense, encodé dans quelques mots de 64 bits aux
 * champs entrelacés. Le combineur de couleurs mérite une mention à part : c'est
 * une unité programmable qui calcule, pour chaque pixel, une combinaison de
 * texel, couleur de primitive, couleur d'environnement, couleur de shading et
 * constantes — sur un ou deux cycles.
 *
 * ## Ce qui rend le problème traitable
 *
 * Le combineur fixe de Glide est bien moins expressif, et la traduction est le
 * point dur de tout l'épic E05. Mais **DKR déclare ses réglages de rendu dans
 * des tables statiques** : l'ensemble des combiners employés est borné et connu.
 * L'inventaire du portage natif voisin en dénombre **33 configurations
 * distinctes**, dont **3 seulement lisent deux texels**.
 *
 * Il ne s'agit donc pas de traduire un combineur programmable en général, mais
 * de faire correspondre 33 cas énumérés. C'est ce qui donne sa forme à ce
 * fichier : le décodage produit une **forme canonique comparable**, et E05-S03
 * y fera correspondre un réglage Glide par simple recherche.
 *
 * ## Ce que ce fichier ne fait pas
 *
 * Il ne réalise rien. La traduction vers Glide est E05-S03 à E05-S06 ; ici on
 * décode et l'on range.
 */
#ifndef DKR_RENDER_RDP_STATE_H
#define DKR_RENDER_RDP_STATE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Les entrées du combineur ---------------------------------------------- *
 *
 * Valeurs de `G_CCMUX_*` et `G_ACMUX_*` de `gbi.h`. Recopiées plutôt
 * qu'incluses : ce portage n'a pas les en-têtes de la décomposition — il
 * travaille depuis du MIPS recompilé — et une poignée de constantes vaut mieux
 * qu'une dépendance vers un dépôt voisin.
 *
 * Le piège de cette table est que **le même numéro ne désigne pas la même chose
 * selon la position**. `G_CCMUX_CENTER` et `G_CCMUX_SCALE` valent tous deux 6 ;
 * `1` signifie `TEXEL0` en entrée A mais `NOISE` nulle part ailleurs. Le
 * décodeur nomme donc les entrées par position, et non par un dictionnaire
 * unique.
 */
typedef enum {
    DKR_CC_COMBINED = 0,
    DKR_CC_TEXEL0,
    DKR_CC_TEXEL1,
    DKR_CC_PRIMITIVE,
    DKR_CC_SHADE,
    DKR_CC_ENVIRONMENT,
    DKR_CC_CENTER_SCALE,     /* 6 : `CENTER` ou `SCALE` selon la position */
    DKR_CC_COMBINED_ALPHA,   /* 7 */
    DKR_CC_TEXEL0_ALPHA,
    DKR_CC_TEXEL1_ALPHA,
    DKR_CC_PRIMITIVE_ALPHA,
    DKR_CC_SHADE_ALPHA,
    DKR_CC_ENV_ALPHA,
    DKR_CC_LOD_FRACTION,
    DKR_CC_PRIM_LOD_FRAC,
    DKR_CC_K5,
    DKR_CC_ZERO_OR_OTHER     /* >= 16 : zéro pour les champs de 4 bits */
} dkr_cc_input;

/* Un étage : `(a - b) * c + d`. C'est la forme du RDP, et la garder telle quelle
   évite de perdre en route l'information dont E05-S03 aura besoin. */
typedef struct {
    unsigned char a, b, c, d;
} dkr_cc_stage;

typedef struct {
    dkr_cc_stage rgb[2];     /* cycle 0, cycle 1 */
    dkr_cc_stage alpha[2];
} dkr_combiner;

/* --- Le mode de cycle ------------------------------------------------------ */
typedef enum {
    DKR_CYCLE_1 = 0,
    DKR_CYCLE_2,
    DKR_CYCLE_COPY,
    DKR_CYCLE_FILL
} dkr_cycle_type;

/* --- L'état décodé --------------------------------------------------------- */
typedef struct {
    dkr_cycle_type  cycle;
    dkr_combiner    combiner;

    /* Modes de texture, de `G_SETOTHERMODE_H`. */
    dkr_filter_mode filter;          /* G_TF_POINT / G_TF_BILERP / G_TF_AVERAGE */
    unsigned char   texture_lod;     /* G_TL_LOD */
    unsigned char   texture_persp;   /* G_TP_PERSP */
    unsigned char   texture_detail;  /* G_TD_* */

    /* Modes de rendu, de `G_SETOTHERMODE_L`. */
    unsigned char   alpha_compare;   /* G_AC_* : 0 aucun, 1 seuil, 2 tramage */
    unsigned char   z_source;        /* G_ZS_* */
    unsigned int    render_mode;     /* les bits du blender, bruts */

    /* Ce que le blender dit, une fois lu. Les bits du RDP sont entrelacés et
       l'on préfère les décoder une fois. */
    unsigned char   z_test;
    unsigned char   z_write;
    unsigned char   fog;
} dkr_rdp_state;

/* --- Décodage -------------------------------------------------------------- */

/* `G_SETCOMBINE` : deux mots de 32 bits. `w0` porte l'opcode en tête, dont le
   décodeur ne tient pas compte — il ne lit que les 24 bits bas. */
void dkr_rdp_decode_combine(unsigned int w0, unsigned int w1,
                            dkr_combiner *out);

/* `G_SETOTHERMODE_H` et `_L`, tels que le RSP les maintient. */
void dkr_rdp_decode_othermode(unsigned int mode_h, unsigned int mode_l,
                              dkr_rdp_state *out);

/* --- Forme canonique ------------------------------------------------------- *
 *
 * Une clé de 64 bits qui identifie une configuration de combineur. Deux
 * configurations sont la même si et seulement si leurs clés sont égales — c'est
 * ce qui permet à E05-S03 de chercher plutôt que de raisonner.
 *
 * Le mode de cycle **en fait partie** : le même mot de combineur en un cycle et
 * en deux cycles ne produit pas la même image, le second étage n'étant pas
 * évalué dans le premier cas. Les confondre serait une erreur silencieuse. */
unsigned long long dkr_rdp_combiner_key(const dkr_combiner *c,
                                        dkr_cycle_type cycle);

/* Rend le nom de la configuration si elle est répertoriée, `NULL` sinon.
 *
 * C'est le filet de sécurité de l'étape 6 du ticket : un cas non répertorié doit
 * **se signaler** plutôt que produire un rendu faux en silence. Une
 * configuration manquée ne se voit pas au décodage — elle se voit à l'écran,
 * sous forme d'une surface d'une couleur inattendue, éventuellement dans un seul
 * niveau. */
const char *dkr_rdp_combiner_name(unsigned long long key);

/* Nombre de configurations répertoriées, et accès par index — pour qu'un
   inventaire puisse être écrit sans dupliquer la table. */
int  dkr_rdp_known_count(void);
int  dkr_rdp_known_at(int index, unsigned long long *key, const char **name,
                      int *texel_count);

/* --- Traduction vers l'état abstrait --------------------------------------- *
 *
 * Remplit ce que E04-S01 définit. Ce qui n'a pas d'équivalent — un combineur à
 * deux étages arbitraires — est ramené au mode le plus proche, et
 * `*exact` reçoit 0 pour le dire. **Une traduction approchée qui ne s'annonce
 * pas est pire qu'un échec** : elle produit une image plausible et fausse. */
void dkr_rdp_to_render_state(const dkr_rdp_state *rdp,
                             dkr_render_state *out, int *exact);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_RDP_STATE_H */
