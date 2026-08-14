/* E04-S08 — le rastériseur logiciel de référence.
 *
 * Sa raison d'être tient en une phrase : **quand une image sera fausse en Glide,
 * il faudra savoir si l'erreur vient du décodeur ou du backend.** Sans oracle
 * intermédiaire, un pixel faux peut venir de dix étages — transformation,
 * découpage, décodage de texture, traduction de combineur, réglage Glide,
 * pilote. Avec un backend logiciel implémentant la **même interface**
 * (E04-S01), la question se tranche en une exécution.
 *
 * ## Il a le droit d'être lent, et pas celui d'être compliqué
 *
 * Sa valeur entière tient dans la confiance qu'on lui accorde comme référence.
 * Un rastériseur optimisé est un rastériseur dont il faut à son tour vérifier la
 * justesse, et l'oracle disparaît. Le code qui suit choisit donc systématiquement
 * la forme la plus évidente : flottants partout, pas de découpage en tuiles, pas
 * de table précalculée, une boucle par pixel de la boîte englobante.
 *
 * ## Ce qu'il n'emprunte pas à Glide
 *
 * Rien. C'est le but. Là où le backend Glide devra plier le combineur du RDP aux
 * modes de la carte, celui-ci calcule ce que l'image **devrait** être, et
 * E05-S03 mesurera son écart à cette référence.
 */
#ifndef DKR_RENDER_SOFTWARE_H
#define DKR_RENDER_SOFTWARE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Remplit `out` avec le backend logiciel. `open` alloue les tampons.
 *
 * Un seul exemplaire à la fois, comme pour l'implémentation vide : deux oracles
 * simultanés n'ont pas de sens, et un allocateur d'instances serait du code que
 * personne n'appelle. */
void dkr_render_backend_software(dkr_render_backend *out);

/* --- Ce qu'un oracle doit rendre observable -------------------------------- *
 *
 * Les trois fonctions ci-dessous n'ont pas d'équivalent dans l'interface, et
 * c'est normal : elles ne servent pas au rendu mais à la **comparaison**, qui
 * est le rôle de ce backend. Le harnais de E09-S02 en est le consommateur.
 */

/* L'image, en ARGB 32 bits, lignes du haut vers le bas. Rendue directement
   plutôt que copiée : le harnais compare, il ne modifie pas. */
const unsigned *dkr_software_framebuffer(int *width, int *height);

/* Écrit l'image en BMP 24 bits. Ce format-là parce qu'il s'écrit en trente
   lignes sans bibliothèque, et que Windows 95 comme l'hôte le lisent — un PNG
   demanderait zlib des deux côtés pour un gain nul ici.

   Rend 0 en cas d'échec. */
int dkr_software_write_bmp(const char *path);

/* La profondeur, pour les cas où c'est elle qu'on soupçonne. `NULL` si aucun
   contexte n'est ouvert. */
const float *dkr_software_depthbuffer(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_SOFTWARE_H */
