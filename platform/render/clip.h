/* E04-S05 — découpage, faces arrière et fenêtre de ciseaux.
 *
 * ## Pourquoi il faut découper, et pourquoi seulement au plan proche
 *
 * Les cartes 3dfx ne découpent pas. Elles ont une fenêtre de ciseaux qui rejette
 * les fragments hors zone, ce qui suffit sur les côtés — mais un triangle dont
 * un sommet passe **derrière la caméra** ne peut pas être rejeté au niveau du
 * fragment : sa projection est mathématiquement absurde, et le sommet ressort de
 * l'autre côté de l'écran. C'est l'éclat de géométrie qui traverse l'image, très
 * visible et difficile à reproduire parce qu'il dépend d'un angle précis.
 *
 * **Seul le plan proche exige donc un vrai découpage.** Le découpage complet aux
 * six plans coûterait beaucoup et n'apporterait rien que la fenêtre de ciseaux
 * ne fasse déjà. Cette distinction est la clé du coût de cet étage.
 *
 * ## L'attribut qu'on oublie
 *
 * Découper produit de nouveaux sommets, et chacun doit porter **tous** les
 * attributs interpolés : position, couleur, coordonnées de texture. En oublier
 * un produit un artefact visible uniquement sur les triangles découpés — donc
 * rare, donc déroutant. L'épreuve vérifie chaque attribut séparément pour cette
 * raison.
 */
#ifndef DKR_RENDER_CLIP_H
#define DKR_RENDER_CLIP_H

#include "backend.h"
#include "transform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Un sommet **avant** division perspective : position homogène et attributs.
 * C'est là que le découpage doit avoir lieu — après projection il est trop tard,
 * la division ayant déjà produit des coordonnées sans signification. */
struct dkr_clip_vertex_ {
    float x, y, z, w;
    float r, g, b, a;
    float s, t;              /* coordonnées de texture, non divisées */
};
typedef struct dkr_clip_vertex_ dkr_clip_vertex;

/* Le plan proche n'est pas `w > 0` mais `w > epsilon`.
 *
 * Un sommet exactement sur le plan donne `1/w` infini ; un sommet juste devant
 * donne un `1/w` énorme qui sature en flottant et produit les mêmes éclats que
 * le sommet derrière. La marge est petite mais elle n'est pas nulle. */
#ifndef DKR_CLIP_NEAR_EPSILON
#define DKR_CLIP_NEAR_EPSILON 0.0001f
#endif

/* --- La bande de garde ------------------------------------------------------ *
 *
 * Le découpage au seul plan proche ne suffit pas, et la mesure l'a montré : un
 * sommet créé à `w = 0,0001` projette à **16 millions de pixels**. Les fonctions
 * d'arête du rastériseur soustraient alors des nombres de cet ordre pour obtenir
 * des unités — annulation catastrophique — et le résultat dépend de la précision
 * des intermédiaires. L'hôte calcule en SSE 32 bits, la cible en x87 80 bits :
 * les deux ne rendent alors pas les mêmes pixels.
 *
 * Découper aussi contre une **bande de garde** borne les coordonnées projetées
 * par construction, et rend l'oracle exact des deux côtés.
 *
 * Ce n'est pas le découpage complet aux six plans que E04-S05 écarte à juste
 * titre : la bande est bien plus large que l'écran, donc presque aucun triangle
 * ne la traverse, et ceux qui restent entièrement dedans sortent par un
 * court-circuit sans qu'aucune arête ne soit calculée.
 *
 * `DKR_CLIP_GUARD` est le rapport entre la bande et le demi-écran. À 4, un écran
 * de 640 pixels tolère des coordonnées de −960 à 1600. La valeur exacte que Glide
 * accepte reste à mesurer (E04-S05) ; celle-ci est choisie pour que les
 * coordonnées restent dans un domaine où la précision tient, ce qui est une
 * contrainte différente et indépendante. */
#ifndef DKR_CLIP_GUARD
#define DKR_CLIP_GUARD 4.0f
#endif

/* Découpe un triangle au plan proche.
 *
 * `out` reçoit 0, 1 ou 2 triangles — trois sommets chacun — et doit donc pouvoir
 * en contenir six. Rend le nombre de triangles produits.
 *
 * Deux triangles pour un : c'est le cas où **un seul** sommet est derrière. Le
 * polygone restant est alors un quadrilatère, qu'il faut retrianguler. L'oublier
 * fait disparaître la moitié de la surface, ce qui se voit comme un trou. */
int dkr_clip_near(const dkr_clip_vertex in[3], dkr_clip_vertex out[6]);

/* Projette un sommet découpé vers le format du backend. La division perspective
   est faite ici, une fois le découpage garanti. */
void dkr_clip_project(const dkr_transform *t, const dkr_clip_vertex *in,
                      dkr_render_vertex *out);

/* --- Faces arrière ---------------------------------------------------------- *
 *
 * La carte ne les élimine pas non plus. La convention vient du microcode :
 * `f3ddkr_rt64.cpp` choisit `G_CULL_BACK` ou `G_CULL_FRONT` selon le **signe de
 * l'échelle en x de la fenêtre d'affichage**, et le bit 0x40 de l'en-tête du
 * triangle désactive l'élimination.
 *
 * Rend 1 si le triangle doit être dessiné. */
int dkr_cull_accept(const dkr_render_vertex v[3], dkr_cull_mode mode);

/* Le sens que le microcode retient pour une fenêtre donnée. */
dkr_cull_mode dkr_cull_mode_for_viewport(float viewport_scale_x, int cull_enabled);

/* --- Rejet ------------------------------------------------------------------ *
 *
 * Un triangle entièrement d'un côté de l'écran ne sera dessiné par personne. Le
 * rejeter ici évite de l'envoyer, et chaque primitive non envoyée est un appel
 * de moins à travers une DLL.
 *
 * `margin` est la tolérance hors écran : Glide accepte des coordonnées
 * modérément dehors, et découper trop tôt coûterait plus que de laisser passer.
 * **Cette marge se mesure et ne se déduit pas** ; en attendant la mesure, la
 * valeur retenue est délibérément généreuse. */
int dkr_clip_reject_offscreen(const dkr_render_vertex v[3],
                              int width, int height, float margin);

#define DKR_CLIP_DEFAULT_MARGIN 2048.0f

/* --- Fenêtre de ciseaux ------------------------------------------------------ *
 *
 * DKR s'en sert pour l'écran partagé. Les quatre dispositions sont calculées ici
 * plutôt que codées en dur chez l'appelant, parce que c'est le genre de calcul
 * qu'on refait mal la seconde fois. */
typedef struct { int x0, y0, x1, y1; } dkr_scissor;

/* `player` va de 0 à `players - 1`. Rend 0 si la demande n'a pas de sens. */
int dkr_scissor_for_player(int players, int player, int width, int height,
                           dkr_scissor *out);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_CLIP_H */
