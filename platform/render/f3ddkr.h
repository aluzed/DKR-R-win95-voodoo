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
 * Il en découle une limite assumée : **les sommets qu'il émet sont en espace
 * objet**, non projetés. Tant que E04-S03 n'existe pas, ils ne peuvent pas être
 * dessinés correctement — le décodeur les compte et les trace, ce qui suffit à
 * établir que la séquence de commandes est juste.
 */
#ifndef DKR_RENDER_F3DDKR_H
#define DKR_RENDER_F3DDKR_H

#include "backend.h"

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
    unsigned int texture_offset_s, texture_offset_t;

    /* Comptes, pour le mode trace et pour les épreuves. */
    unsigned long commands;
    unsigned long triangles;
    unsigned long vertices;
    unsigned long rejects[DKR_F3D_REJECT_COUNT_MAX];
} dkr_f3d_state;

/* --- Le contexte ----------------------------------------------------------- */
typedef struct {
    const unsigned char *rdram;      /* instantané RDRAM, `rdram_size` octets */
    unsigned int         rdram_size;
    dkr_render_backend  *backend;    /* peut être NULL : on décode sans dessiner */
    dkr_f3d_state        state;

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
