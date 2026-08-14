/* E04-S03 — pile de matrices et transformation des sommets.
 *
 * Sur la N64 c'est le RSP qui transforme. Ici cela revient au processeur hôte —
 * comme sur toute carte 3dfx, qui ne transforme rien. **C'est le poste de calcul
 * graphique le plus lourd du portage**, et il tombe intégralement dans le budget
 * de E00-S03.
 *
 * ## Les matrices de la N64 ne sont pas des matrices ordinaires
 *
 * Elles sont en virgule fixe 16.16, et **stockées en deux moitiés séparées** :
 * les seize parties entières d'abord, les seize parties fractionnaires ensuite.
 * `gbi.h` le dit en une phrase — « First 8 words are integer portion of the 4x4
 * matrix, last 8 words are the fraction portion » — et s'en écarter ne produit
 * pas une erreur mais une géométrie fausse.
 *
 * ## La profondeur de pile est relevée, pas supposée
 *
 * `f3ddkr_rt64.cpp` borne l'index de matrice à 2 dans `Matrix` comme dans
 * `MoveWord` : **DKR emploie trois emplacements**. En prévoir seize par prudence
 * coûterait de la mémoire sur une machine qui n'en a pas, et masquerait une
 * commande mal décodée qui viserait un emplacement inexistant.
 */
#ifndef DKR_RENDER_TRANSFORM_H
#define DKR_RENDER_TRANSFORM_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DKR_MATRIX_SLOTS 3

typedef struct {
    /* Rangée par colonne majeure, comme le microcode : `m[colonne][ligne]`. */
    float m[4][4];
} dkr_matrix;

/* --- Conversion 16.16 ------------------------------------------------------- *
 *
 * `data` fait 64 octets : 32 pour les parties entières, 32 pour les
 * fractionnaires. La lecture est **gros-boutiste**, comme la RDRAM.
 *
 * Rend 0 si `data` est nul. Aucune autre façon d'échouer : toutes les
 * combinaisons de 64 octets décrivent une matrice, même absurde. */
int dkr_matrix_from_fixed(const unsigned char *data, dkr_matrix *out);

/* --- La pile ---------------------------------------------------------------- */
typedef struct {
    dkr_matrix slot[DKR_MATRIX_SLOTS];
    dkr_matrix projection;
    dkr_matrix mvp;              /* produit, recalculé à la demande */
    int        selected;         /* 0..2 */
    int        mvp_valid;

    /* Fenêtre d'affichage. L'échelle en x porte le signe qui décide du sens de
       culling — c'est ainsi que le microcode l'exprime, et le décodeur en
       dépend. */
    float viewport_scale_x, viewport_scale_y;
    float viewport_trans_x, viewport_trans_y;
} dkr_transform;

void dkr_transform_init(dkr_transform *t);
void dkr_transform_set_matrix(dkr_transform *t, int slot, const dkr_matrix *m);
void dkr_transform_select(dkr_transform *t, int slot);
void dkr_transform_set_projection(dkr_transform *t, const dkr_matrix *m);
void dkr_transform_set_viewport(dkr_transform *t, float sx, float sy,
                                float tx, float ty);

/* --- Le sommet -------------------------------------------------------------- *
 *
 * Le sommet DKR tel qu'il est en RDRAM : dix octets, position en entiers 16 bits
 * signés puis couleur en octets. **Aucune coordonnée de texture** — elles
 * arrivent par coin au moment du triangle. */
typedef struct {
    short         x, y, z;
    unsigned char r, g, b, a;
} dkr_source_vertex;

/* Transforme et **écrit directement le format du backend**, sans recopie
   intermédiaire : `dkr_render_vertex` a la disposition de `GrVertex`, et une
   conversion par sommet coûterait cher sur un Pentium II qui en voit des
   dizaines de milliers par image.
 *
 * Rend 0 si le sommet est derrière le plan de projection — `w <= 0` — auquel cas
 * `out` n'est pas écrit. Le découpage proprement dit est E04-S05 ; ici on se
 * contente de ne pas diviser par une valeur qui n'a pas de sens. */
int dkr_transform_vertex(dkr_transform *t, const dkr_source_vertex *in,
                         dkr_render_vertex *out);

/* La matrice modèle-vue-projection courante, recalculée si nécessaire. Exposée
   pour les épreuves et pour E08-S03, qui voudra la traiter par lots. */
const dkr_matrix *dkr_transform_mvp(dkr_transform *t);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_TRANSFORM_H */
