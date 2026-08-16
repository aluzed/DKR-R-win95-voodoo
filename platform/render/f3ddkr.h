/* E04-S02 — décodeur de display list F3DDKR, indépendant de RT64.
 *
 * `F3DDKRRT64Bridge` sait déjà décoder le microcode de Rare, et ce travail est
 * précieux : il est validé par un portage qui tourne. Ce module en reprend la
 * logique — qui est propre au microcode et n'a rien à voir avec RT64 — et la
 * repose sur l'interface de rendu de E04-S01.
 *
 * La cartographie des commandes est dans `docs/research/f3ddkr-commands.md`.
 *
 * ## Ce qui devait survivre à l'extraction
 *
 * **La validation des plages.** Le décodeur d'origine vérifie chaque plage avant
 * de l'utiliser et rejette les données invalides par une erreur bornée, plutôt
 * que de laisser adresser la mémoire hôte. Une extraction qui perdrait cette
 * discipline échangerait un décodeur sûr contre un décodeur rapide à écrire.
 *
 * Elle protège contre deux choses différentes : une ROM modifiée, et un bug du
 * portage. La seconde est la plus probable.
 *
 * ## Ce que ce module ne fait pas
 *
 * Il ne transforme pas les sommets (E04-S03), ne découpe pas (E04-S05), ne
 * décode pas les textures (E04-S07). Il lit la display list, valide, tient
 * l'état du microcode, et appelle l'interface de rendu.
 *
 * Il **émet** en revanche, désormais que E04-S03 et E04-S05 existent : chaque
 * triangle traverse la transformation, le découpage au plan proche, la
 * projection et l'élimination des faces arrière avant d'atteindre le backend.
 * C'est la chaîne complète, et le seul assemblage qui prouve que les cinq
 * modules s'emboîtent.
 */
#ifndef DKR_RENDER_F3DDKR_H
#define DKR_RENDER_F3DDKR_H

#include "backend.h"
#include "clip.h"
#include "rdp_state.h"
#include "texture.h"
#include "transform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Les raisons de rejet -------------------------------------------------- *
 *
 * Distinguées parce qu'elles ne se diagnostiquent pas de la même façon : une
 * adresse hors RDRAM évoque une base de DMA fausse, un index de sommet hors
 * cache évoque une display list corrompue ou une commande manquée. */
typedef enum {
    DKR_F3D_REJECT_ADDRESS = 0,   /* plage hors des 8 Mio de RDRAM */
    DKR_F3D_REJECT_COUNT,         /* nombre nul ou au-delà de la limite */
    DKR_F3D_REJECT_INDEX,         /* index de sommet hors du cache de 32 */
    DKR_F3D_REJECT_DEPTH,         /* pile de listes imbriquées pleine */
    DKR_F3D_REJECT_OPCODE,        /* opcode inconnu */
    DKR_F3D_REJECT_COUNT_MAX
} dkr_f3d_reject;

const char *dkr_f3d_reject_text(dkr_f3d_reject r);

/* --- L'état du décodeur ---------------------------------------------------- */
typedef struct {
    /* Bases d'adressage de `DMAOffsets` — le mécanisme central du microcode de
       Rare. Une base fausse ne plante pas : elle produit une géométrie
       entièrement absurde, ce qui est bien plus dur à diagnostiquer. */
    unsigned int matrix_offset;
    unsigned int vertex_offset;

    unsigned int selected_matrix;    /* 0..2 */
    unsigned char billboard;
    /* Base d'adressage pour le chargement de texture, **et non un couple de
       decalages s et t** — relevé dans le portage voisin, voir `f3ddkr.c`. */
    unsigned int texture_offset;
    unsigned int texture_shift;
    unsigned int texture_count;

    /* Comptes, pour le mode trace et pour les épreuves. */
    unsigned long commands;
    unsigned long triangles;
    unsigned long vertices;
    unsigned long rejects[DKR_F3D_REJECT_COUNT_MAX];

    /* Ce que la chaîne a réellement remis au backend, par opposition à ce que la
       display list demandait. L'écart entre `triangles` et `emitted` est le
       nombre éliminé — par le découpage, la culling ou le rejet hors écran — et
       c'est un chiffre qu'on veut voir : un écran vide avec `triangles` élevé et
       `emitted` nul désigne immédiatement cet étage. */
    unsigned long emitted;
    unsigned long culled;
    unsigned long clipped_away;
    unsigned long clip_split;      /* triangles devenus deux */

    /* Commandes reconnues mais dont l'effet n'est pas encore branché — modes
       géométriques, état RDP, textures. Comptées à part de `commands` parce que
       ce chiffre répond à une question différente : non pas « la séquence est-
       elle juste » mais **quelle part de l'image est encore ignorée**. C'est la
       mesure qui manquera le plus quand le décor sortira faux plutôt
       qu'absent. */
    unsigned long deferred;

    /* --- L'état 2D, celui que la séquence de démarrage exerce ---------------- *
     *
     * Mesuré avant d'être écrit : sur les 47 000 commandes du démarrage, le seul
     * ordre de dessin émis est `FILLRECT`. Ces trois champs sont donc ce dont
     * dépend le premier pixel que ce portage affichera. */
    unsigned int  fill_color_raw;     /* le mot de SETFILLCOLOR, tel quel */
    unsigned int  fill_color_argb;    /* et sa conversion, pour le backend */
    unsigned int  color_image_width;  /* la largeur du tampon, lue et non supposée */
    unsigned long rects;              /* rectangles réellement remis au backend */

    /* --- L'état RDP, et ce qu'il coûte en fidélité -------------------------- */
    unsigned long etats_appliques;    /* traductions réellement remises au backend */
    /* Traductions **approchées**. `rdp_state.h` insiste : une approximation qui
       ne s'annonce pas est pire qu'un échec, parce qu'elle produit une image
       plausible et fausse. Ce compteur est ce filet. */
    unsigned long etats_approches;
    /* Remplissages survenus hors du mode `FILL`. Le RDP ne remplit qu'en mode
       FILL ; toute autre valeur accuse l'écriture partielle du mot de mode, donc
       le décalage — et le dit en chiffres plutôt qu'à l'écran. */
    unsigned long fill_hors_cycle;
    unsigned char cycle_courant;
    /* Fenêtres d'affichage installées par le jeu. Zéro signifie qu'on dessine
       encore avec le défaut, donc à une échelle inventée. */
    unsigned long viewports;

    /* --- Les textures ------------------------------------------------------- */
    dkr_texture_stats textures;          /* converties, refusees, hors bornes */
    unsigned long     textures_chargees;    /* remises au backend */
    unsigned long     textures_reutilisees; /* servies par le cache */
    unsigned long     textures_refusees;    /* memoire de texture pleine */
    /* Remplies jusqu'a la puissance de deux superieure, ce que la Voodoo exige
       et que la N64 n'impose pas. */
    unsigned long     textures_remplies;
    /* Refusees pour un rapport au-dela de 8:1, que le remplissage ne peut pas
       corriger sans multiplier la memoire par huit. */
    unsigned long     textures_hors_proportions;

    /* Combien de fois chaque opcode a été vu.
     *
     * Mille octets pour répondre à une question qu'aucun raisonnement ne tranche :
     * **de quoi une image de DKR est-elle faite ?** Sans cela on décide quoi
     * implémenter d'après une table d'opcodes, c'est-à-dire d'après ce que le
     * microcode *peut* émettre plutôt que ce que ce jeu *émet*. Les deux ont déjà
     * divergé une fois cette session, sur la borne basse de la famille F3D. */
    unsigned long opcodes[256];
} dkr_f3d_state;

/* --- Le contexte ----------------------------------------------------------- */
typedef struct {
    const unsigned char *rdram;      /* instantané RDRAM, `rdram_size` octets */
    unsigned int         rdram_size;
    /* Disposition des octets dans `rdram`. Zéro — la valeur par défaut — décrit
       le gros-boutiste franc de la console, celui que les épreuves construisent.
       Un vaut la disposition **entrelacée par XOR-3** de librecomp, celle de
       l'instantané que le jeu remet au fil graphique.
     *
       Le drapeau existe parce que les deux sont indiscernables à l'inspection :
       une display list lue avec la mauvaise convention ne plante pas, elle décode
       des opcodes plausibles à des adresses absurdes. On les rejette, on compte
       les rejets, et l'on soupçonne le décodeur. */
    unsigned char        rdram_native;
    dkr_render_backend  *backend;    /* peut être NULL : on décode sans dessiner */
    dkr_f3d_state        state;

    /* La chaîne. `transform` porte les matrices et la fenêtre ; `cache` tient les
       32 sommets du microcode, **déjà transformés en espace homogène**.
     *
       Les transformer au chargement plutôt qu'au triangle n'est pas une
       optimisation gratuite : un sommet servi par trois triangles serait sinon
       transformé trois fois, et la transformation est le poste le plus lourd du
       portage (0,682 µs par sommet, mesuré). Les coordonnées de texture, elles,
       arrivent bien au triangle — c'est ainsi que le microcode fonctionne. */
    dkr_transform        transform;
    dkr_clip_vertex      cache[32];
    unsigned char        cache_valid[32];
    dkr_render_state     render_state;

    /* Le mot d'autre-mode du RDP, accumulé par écritures partielles, et le
       combineur. Ils vivent dans le contexte et non dans l'état parce qu'ils
       sont de la mémoire de travail du décodeur, pas une mesure. */
    unsigned int         mode_h;
    unsigned int         mode_l;
    dkr_combiner         combiner;
    unsigned char        etat_sale;

    /* La résolution réellement ouverte par le backend. Le décodeur en a besoin
       pour porter le tampon du jeu à l'écran, et la déduire de la fenêtre
       courante ne marcherait plus dès qu'il la remplace. */
    unsigned int         screen_width;
    unsigned int         screen_height;

    /* L'image de texture courante, telle que `SETTIMG` la decrit. Elle ne suffit
       pas a charger : les dimensions viennent de `SETTILESIZE`, plus tard. */
    unsigned int         timg_address;
    unsigned int         timg_format;
    unsigned int         timg_size;
    /* La texture actuellement liee, par sa cle. Zero signifie aucune. */
    unsigned long long   texture_cle;
    /* Le tampon de conversion. 256x256 en 5551 : 128 Kio, portes par le contexte
       plutot qu'alloues par texture — un Pentium II n'a pas les moyens d'un
       malloc par changement de texture, et il y en a des milliers par seconde. */
    unsigned short       texels[256 * 256];
    /* Les dimensions reelles et celles apres remplissage. Leur rapport sert aux
       coordonnees de texture : la texture reelle n'occupe que le coin superieur
       gauche de ce qu'on charge. */
    int                  tex_largeur, tex_hauteur;
    int                  tex_largeur_remplie, tex_hauteur_remplie;

    /* Mode trace. Sans cet outil, tout diagnostic graphique sur la machine
       cible se fait à l'aveugle — l'écran appartient à la carte 3dfx et l'on ne
       voit rien d'autre que le résultat. */
    void (*trace)(void *user, const char *line);
    void  *trace_user;
} dkr_f3d_context;

/* Prépare le contexte. `rdram` et `rdram_size` décrivent la mémoire visible ;
   tout ce qui en sort est rejeté. */
void dkr_f3d_init(dkr_f3d_context *ctx, const unsigned char *rdram,
                  unsigned int rdram_size, dkr_render_backend *backend);

/* Exécute la display list à `address`. Rend le nombre de commandes décodées.
 *
 * `address` est une adresse RDRAM, pas un pointeur : le décodeur ne déréférence
 * jamais rien qui vienne de la display list sans l'avoir borné d'abord. */
unsigned long dkr_f3d_run(dkr_f3d_context *ctx, unsigned int address);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_F3DDKR_H */
