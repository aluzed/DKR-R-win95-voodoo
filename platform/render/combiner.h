/* E05-S03 — du combineur programmable du RDP au combineur fixe de Glide.
 *
 * Le RDP calcule, par cycle et pour chaque terme, `(a - b) * c + d`, en
 * choisissant `a`, `b`, `c`, `d` parmi seize sources. Glide offre une liste
 * close de fonctions et de facteurs. Traduire l'un dans l'autre en général est
 * sans espoir ; traduire les configurations que DKR emploie réellement est un
 * problème fini, et l'inventaire les donne : **33 configurations distinctes**,
 * relevées non par instrumentation mais dans les tables statiques du jeu.
 *
 * ## La correspondance structurelle, et le mur
 *
 * Glide possède `GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL`, qui
 * calcule `f x (other - local) + local`. C'est **exactement** `(a - b) * c + d`
 * dès lors que `d == b`. La quasi-totalité des configurations de DKR vérifie
 * cette égalité — ce n'est pas une chance, c'est que les deux matériels
 * expriment la même intention : interpoler entre deux couleurs.
 *
 * Le mur est ailleurs, et il est net : **le RDP a deux registres de couleur
 * constante, `PRIMITIVE` et `ENVIRONMENT` ; Glide n'en a qu'un**
 * (`grConstantColorValue`). Toute configuration qui lit les deux à la fois est
 * hors d'atteinte en une passe, quelle que soit l'ingéniosité du réglage. C'est
 * ce critère, et non l'inspection cas par cas, qui range une configuration en
 * `MULTIPASSE` ou en `APPROCHEE`.
 *
 * Le second mur, plus attendu : `TEXEL1`. Trois configurations lisent deux
 * texels, et c'est E05-S04 qui les traitera par la seconde TMU.
 *
 * ## Pourquoi une table de données et non une cascade de conditions
 *
 * Le ticket l'impose, et il a raison au-delà de la lisibilité : une table
 * indexée par la forme canonique de E04-S06 se **parcourt**. On peut vérifier
 * qu'aucune clé n'est en double, que toutes les configurations de l'inventaire
 * y sont, et surtout mesurer chacune contre le rastériseur de référence sans
 * écrire une épreuve par cas. Une cascade de `if` ne se parcourt pas.
 *
 * ## Ce module contient aussi l'oracle
 *
 * Le ticket suppose que le rastériseur de E04-S08 « implémente le combineur
 * fidèlement ». **Ce n'était pas le cas** : il n'avait que quatre modes fixes,
 * et sans évaluation fidèle de `(a - b) * c + d` le critère de mesure de l'écart
 * n'a pas de sens. `dkr_combiner_eval` comble ce manque, et c'est lui qui donne
 * la réponse à laquelle Glide est comparée.
 */
#ifndef DKR_RENDER_COMBINER_H
#define DKR_RENDER_COMBINER_H

#include "rdp_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- L'oracle : évaluation fidèle ------------------------------------------- *
 *
 * Toutes les couleurs sont en 0..255 par composante, ce qui est l'échelle du
 * rastériseur. Le calcul se fait en flottant et n'est borné qu'à la fin : le RDP
 * borne de même, et borner à chaque étage donnerait un résultat différent sur
 * les configurations à deux cycles. */
typedef struct {
    float texel0[4];       /* r, g, b, a */
    float texel1[4];
    float primitive[4];
    float shade[4];
    float environment[4];
    float combined[4];     /* résultat du cycle précédent ; nul au cycle 0 */
    float lod_fraction;
    float prim_lod_frac;
    float k5;
} dkr_combiner_inputs;

/* Évalue un cycle. `cycle` vaut 0 ou 1. Écrit `out[4]` en 0..255, borné.
 *
 * **Les entrées ne se nomment pas de la même façon selon leur position.** La
 * valeur 6 signifie `1` en position `a`, `CENTER` en `b`, `SCALE` en `c` et `1`
 * en `d` ; la valeur 1 signifie `TEXEL0` partout en couleur mais `TEXEL0_ALPHA`
 * en alpha. Ce module lit donc chaque position avec sa propre table, comme le
 * décodeur de E04-S06. Un dictionnaire unique donnerait un rendu faux d'une
 * façon subtile et localisée. */
void dkr_combiner_eval(const dkr_combiner *c, int cycle,
                       const dkr_combiner_inputs *in, float out[4]);

/* Évalue la configuration complète : un ou deux cycles selon `cycle_type`, en
   réinjectant le résultat du premier comme `COMBINED` du second. */
void dkr_combiner_eval_all(const dkr_combiner *c, dkr_cycle_type cycle_type,
                           const dkr_combiner_inputs *in, float out[4]);

/* --- La traduction vers Glide ------------------------------------------------ */

typedef enum {
    DKR_CC_EXACTE = 0,     /* un réglage Glide produit le même résultat */
    DKR_CC_MULTIPASSE,     /* plusieurs passes y parviennent */
    DKR_CC_APPROCHEE,      /* aucune combinaison n'y parvient */
    DKR_CC_DEUX_TEXELS     /* renvoyée à E05-S04 : seconde TMU */
} dkr_cc_categorie;

const char *dkr_cc_categorie_texte(dkr_cc_categorie c);

/* Quelle couleur constante charger dans l'unique registre de Glide.
 *
 * C'est ici que se lit le mur : le RDP en a deux, Glide un seul. Une
 * configuration qui a besoin des deux porte `DKR_CONST_LES_DEUX` et ne peut pas
 * être exacte. */
typedef enum {
    DKR_CONST_AUCUNE = 0,
    DKR_CONST_PRIMITIVE,
    DKR_CONST_ENVIRONMENT,
    DKR_CONST_LES_DEUX
} dkr_cc_constante;

/* Le réglage Glide, sous forme de données. Les valeurs sont celles des
   énumérations de Glide 2.x, telles que `glide_backend.c` les emploie. */
typedef struct dkr_cc_reglage {
    unsigned char cc_function, cc_factor, cc_local, cc_other;
    unsigned char ac_function, ac_factor, ac_local, ac_other;
    unsigned char tc_function, tc_factor;   /* étage de texture */
    unsigned char utilise_texture;
} dkr_cc_reglage;

typedef struct {
    const char        *nom;                 /* « G_CC_MODULATEIA », etc. */
    const char        *nom_cycle2;          /* NULL en un cycle */
    dkr_cc_stage       rgb[2], alpha[2];
    dkr_cycle_type     cycle;
    dkr_cc_categorie   categorie;
    dkr_cc_constante   constante;
    dkr_cc_reglage     reglage;
    const char        *note;                /* pourquoi cette catégorie */
    int                entrees;             /* poids dans l'inventaire */
} dkr_cc_entree;

int  dkr_cc_table_count(void);
const dkr_cc_entree *dkr_cc_table_at(int index);

/* Cherche par forme canonique. Rend NULL si inconnue — cas que l'appelant doit
   journaliser puis rendre par un repli non aberrant, ce qu'impose le ticket. */
const dkr_cc_entree *dkr_cc_lookup(unsigned long long key);

/* La clé canonique d'une entrée de la table, pour l'indexation et les épreuves. */
unsigned long long dkr_cc_entree_key(const dkr_cc_entree *e);

/* Le repli, employé quand la configuration est inconnue.
 *
 * **Visible mais non aberrant**, ce qui exclut les deux réflexes opposés : ne
 * rien dessiner, qui fait disparaître un décor sans laisser de trace, et
 * dessiner en magenta vif, qui rend le jeu injouable au premier combineur
 * oublié. Le repli module la texture par la couleur du sommet — le
 * comportement le plus fréquent dans l'inventaire, donc le moins souvent faux. */
const dkr_cc_reglage *dkr_cc_repli(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_COMBINER_H */
