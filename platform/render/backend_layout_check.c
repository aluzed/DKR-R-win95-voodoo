/* E04-S01 — les deux promesses de l'interface, vérifiées à la compilation.
 *
 * `backend.h` affirme deux choses dont tout le reste dépend, et qu'un
 * commentaire ne protège pas :
 *
 *   1. `dkr_render_vertex` a la disposition de `GrVertex` de Glide 2.x, de sorte
 *      que le backend passe l'adresse du sommet à `grDrawTriangle` sans aucune
 *      conversion ;
 *   2. `dkr_render_state` est comparable par `memcmp`, de sorte que le backend
 *      n'émette que les différences.
 *
 * La première se casse en silence : réordonner deux champs compile parfaitement
 * et rend des couleurs permutées, Glide lisant les flottants aux mauvais
 * décalages — mesuré en E09-S01, où un sommet rouge sortait vert. La seconde se
 * casse en ajoutant un champ mal aligné, qui introduit un remplissage dont le
 * contenu est indéterminé : `memcmp` déclare alors des différences qui n'en sont
 * pas, et le suivi d'état réémet à chaque appel.
 *
 * Les deux sont donc vérifiées ici, à la compilation. Ce fichier ne produit
 * aucun code.
 */
#include "backend.h"

#include <stddef.h>

#if defined(__cplusplus)
#define DKR_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define DKR_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define DKR_STATIC_ASSERT(cond, msg) \
    typedef char dkr_assert_##__LINE__[(cond) ? 1 : -1]
#endif

/* --- 1. La disposition de `GrVertex` --------------------------------------- *
 *
 * Recopiée de `glide.h` de 3dfx. L'ordre n'est pas intuitif — `ooz` et `a`
 * s'intercalent entre les couleurs et `oow` — et c'est précisément pour cela
 * qu'il faut le vérifier plutôt que le supposer.
 */
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, x)   ==  0, "GrVertex.x");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, y)   ==  4, "GrVertex.y");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, z)   ==  8, "GrVertex.z");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, r)   == 12, "GrVertex.r");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, g)   == 16, "GrVertex.g");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, b)   == 20, "GrVertex.b");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, ooz) == 24, "GrVertex.ooz");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, a)   == 28, "GrVertex.a");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, oow) == 32, "GrVertex.oow");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, tmu) == 36, "GrVertex.tmuvtx");
DKR_STATIC_ASSERT(sizeof(dkr_render_vertex) == 36 + 3 * 4 * 4,
                  "GrVertex fait 84 octets ; toute difference casse grDrawTriangle");

/* --- 2. Le bloc d'état sans remplissage ------------------------------------ *
 *
 * La somme des champs doit valoir la taille de la structure. Si le compilateur
 * insère un octet de remplissage, l'égalité tombe et le contrôle échoue — ce qui
 * est le but : c'est ce remplissage-là, jamais initialisé, qui ferait mentir
 * `memcmp`.
 */
/* Chaque champ est compté **par son nom**, et c'est ce qui rend le contrôle
 * utile. Une première version additionnait des types — « quatre `unsigned char` »
 * — et l'auto-test l'a prise en défaut : retirer `pad_` laissait la somme
 * inchangée, le compilateur remettant exactement l'octet qu'on venait d'enlever.
 * Un contrôle qui compte des types compte aussi le remplissage qu'il cherche. */
#define DKR_FIELD_SIZE(f) sizeof(((dkr_render_state *)0)->f)

DKR_STATIC_ASSERT(
    sizeof(dkr_render_state) ==
        DKR_FIELD_SIZE(combine)   + DKR_FIELD_SIZE(blend) +
        DKR_FIELD_SIZE(depth)     + DKR_FIELD_SIZE(cull) +
        DKR_FIELD_SIZE(filter)    + DKR_FIELD_SIZE(wrap_s) +
        DKR_FIELD_SIZE(wrap_t)    + DKR_FIELD_SIZE(alpha_test) +
        DKR_FIELD_SIZE(alpha_reference) + DKR_FIELD_SIZE(fog_enabled) +
        DKR_FIELD_SIZE(pad_)      + DKR_FIELD_SIZE(fog_color) +
        DKR_FIELD_SIZE(texture),
    "dkr_render_state porte du remplissage : memcmp comparerait des octets "
    "indetermines et le suivi d'etat reemettrait a chaque appel");
