/* E05-S02 — allocation de la mémoire de texture de la TMU.
 *
 * Glide n'a pas de gestionnaire de textures. Elle expose la mémoire de la TMU
 * comme un espace d'adressage brut : on choisit une adresse, on y télécharge par
 * `grTexDownloadMipMap`, on lie cette adresse au dessin. Allocation,
 * fragmentation et éviction sont entièrement à écrire.
 *
 * ## La mesure a décidé de la conception
 *
 * Relevé sur la carte (`docs/research/win95-tmu.md`) :
 *
 *     espace adressable  0x00000000 .. 0x001FFFF8 par TMU, soit 2047 Kio
 *     granularité        8 octets — une texture 1x1 en coûte 8 pour 2 utiles
 *     coût               exactement largeur x hauteur x profondeur, sinon
 *
 * Et surtout : **toute taille de texture est une puissance de deux**. Ce n'est
 * pas une observation statistique, c'est une conséquence — les dimensions que le
 * couple (LOD, rapport d'aspect) peut exprimer sont des puissances de deux, et
 * la profondeur vaut un ou deux octets.
 *
 * Le ticket envisageait un allocateur par classes de taille « si les textures du
 * jeu se répartissent en un petit nombre de tailles ». La mesure donne mieux
 * qu'une répartition : elle donne une propriété. Un allocateur **buddy** est
 * exactement un allocateur par classes de taille doté de la fusion, et sur des
 * demandes toutes en puissances de deux il ne produit **aucune fragmentation
 * externe** — jamais un trou inutilisable entre deux textures.
 *
 * C'est ce qui compte ici plus qu'ailleurs. Sur une carte de 1998 sans
 * pagination, une fragmentation qui refuse une texture ne dégrade pas les
 * performances : elle fait manquer un décor.
 *
 * ## Ce qui reste supposé, et qui ne pourra l'être longtemps
 *
 * La taille du plus petit bloc. Elle vaut 128 octets, soit une texture 8x8 en
 * 16 bits, parce que descendre à la granularité matérielle de 8 octets
 * multiplierait par seize la table de suivi pour des textures qui n'existent
 * probablement pas. **Ce « probablement » est le seul endroit de ce fichier qui
 * ne repose pas sur une mesure**, et il tombera dès que la ROM permettra de
 * relever les tailles réelles.
 *
 * ## Ce module ne parle pas à Glide
 *
 * Le téléchargement passe par un pointeur de fonction fourni par l'appelant.
 * C'est ce qui permet d'éprouver l'allocateur et la politique d'éviction sur
 * l'hôte, exhaustivement, sans carte — et c'est nécessaire : la ROM absente
 * interdit de les éprouver en jeu.
 */
#ifndef DKR_RENDER_TMU_H
#define DKR_RENDER_TMU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Granularité matérielle relevée. L'allocateur n'y descend pas, mais toute
   adresse qu'il rend en est un multiple — c'est ce que la carte exige. */
#define DKR_TMU_GRANULARITY   8u

/* Le plus petit bloc géré. Voir l'en-tête : c'est la seule supposition. */
#define DKR_TMU_MIN_BLOCK     128u

/* L'arbre buddy couvre 2 Mio, la taille d'une TMU de Voodoo 2. Une carte plus
   grande est traitée en n'ouvrant qu'une partie de l'arbre ; une plus petite, en
   réservant le haut. Les deux cas passent par `dkr_tmu_init`. */
#define DKR_TMU_SPAN          0x200000u
#define DKR_TMU_LEAVES        (DKR_TMU_SPAN / DKR_TMU_MIN_BLOCK)   /* 16384 */
#define DKR_TMU_NODES         (2u * DKR_TMU_LEAVES)                /* 32768 */

/* Rendu par `dkr_tmu_alloc` quand la place manque. Zéro est une adresse
   parfaitement valide sur cette carte — la mesure le dit, `grTexMinAddress`
   rend zéro — donc le sentinelle ne peut pas être zéro. */
#define DKR_TMU_NONE          0xFFFFFFFFu

/* --- L'état d'un nœud de l'arbre -------------------------------------------- */
typedef enum {
    DKR_TMU_FREE = 0,     /* libre et entier */
    DKR_TMU_SPLIT,        /* coupé en deux : ses fils portent l'information */
    DKR_TMU_USED,         /* alloué en entier */
    DKR_TMU_RESERVED      /* hors de la mémoire réelle, jamais allouable */
} dkr_tmu_node_state;

/* --- Une texture résidente --------------------------------------------------- */
typedef struct {
    unsigned long long key;        /* identité, opaque : on compare, on n'interprète pas */
    unsigned int       address;
    unsigned int       bytes;
    unsigned long      last_used;  /* horodatage logique, pour le moindre récemment utilisé */
    unsigned char      pinned;     /* protégée de l'éviction le temps d'une image */
    unsigned char      live;
} dkr_tmu_resident;

#define DKR_TMU_MAX_RESIDENT 512

/* --- Les compteurs ------------------------------------------------------------ *
 *
 * Le ticket les veut lisibles en jeu, et c'est justifié : un défaut de cache de
 * texture ne se voit pas dans un journal, il se sent à la manette. Un
 * téléchargement en cours de course est un à-coup — le bus PCI de 1998 met du
 * temps à passer 64 Kio, et cela se produit pendant une image de 16 ms. */
typedef struct {
    unsigned long hits;            /* déjà résidente */
    unsigned long misses;          /* a fallu télécharger */
    unsigned long evictions;
    unsigned long downloads;       /* nombre d'appels à la fonction de transfert */
    unsigned long download_bytes;
    unsigned long failures;        /* place introuvable même après éviction */
    unsigned long downloads_this_frame;
    unsigned long bytes_this_frame;
    unsigned long peak_bytes;      /* pic d'occupation atteint */
} dkr_tmu_stats;

/* Le transfert vers la carte. Rend non nul en cas de succès.
   `user` est le contexte de l'appelant ; `data` et `bytes` décrivent la texture
   déjà décodée — le décodage est E04-S07 et n'entre pas ici. */
typedef int (*dkr_tmu_download_fn)(void *user, int tmu, unsigned int address,
                                   const void *data, unsigned int bytes);

typedef struct dkr_tmu {
    int                 index;         /* 0 ou 1 : quelle TMU */
    unsigned int        base, limit;   /* l'espace réellement utilisable */
    unsigned char       node[DKR_TMU_NODES];
    unsigned int        used_bytes;

    dkr_tmu_resident    resident[DKR_TMU_MAX_RESIDENT];
    unsigned long       clock;         /* horodatage logique, avance à chaque usage */

    dkr_tmu_download_fn download;
    void               *download_user;

    dkr_tmu_stats       stats;
} dkr_tmu;

/* Prépare l'allocateur sur `[base, limit)`. Les adresses viennent de
   `grTexMinAddress` et `grTexMaxAddress` — elles ne sont pas supposées, et la
   mesure a montré que `base` vaut zéro sur cette carte, ce qui interdit d'en
   faire un sentinelle. */
void dkr_tmu_init(dkr_tmu *t, int index, unsigned int base, unsigned int limit,
                  dkr_tmu_download_fn download, void *user);

/* Alloue `bytes` octets. Rend l'adresse, ou `DKR_TMU_NONE`.
   La taille est arrondie à la puissance de deux supérieure : c'est sans effet
   sur les textures, dont la taille en est déjà une. */
unsigned int dkr_tmu_alloc(dkr_tmu *t, unsigned int bytes);

/* Rend un bloc. L'adresse doit être celle qu'`alloc` avait rendue. */
void dkr_tmu_free(dkr_tmu *t, unsigned int address, unsigned int bytes);

/* --- Le cache ---------------------------------------------------------------- *
 *
 * `dkr_tmu_acquire` est le point d'entrée du moteur : « donne-moi l'adresse de
 * cette texture, en la téléchargeant s'il le faut ». Il compte les succès et les
 * échecs, évince au moindre récemment utilisé quand la place manque, et rend
 * `DKR_TMU_NONE` s'il n'y parvient pas.
 *
 * `bytes` doit venir de `grTexTextureMemRequired` et non d'un calcul : la mesure
 * a montré un cas d'arrondi (une texture 1x1 coûte 8 octets pour 2 utiles), et
 * empiler d'après un calcul ferait se recouvrir deux textures. Le symptôme ne
 * serait pas une erreur mais un décor portant le motif d'un autre. */
unsigned int dkr_tmu_acquire(dkr_tmu *t, unsigned long long key,
                             const void *data, unsigned int bytes);

/* Marque le début d'une image : remet les compteurs par image à zéro et lève
   toutes les protections. */
void dkr_tmu_begin_frame(dkr_tmu *t);

/* Protège une texture de l'éviction jusqu'à la fin de l'image. Sans cela, une
   image qui demande plus de textures que la TMU n'en tient évincerait celles
   qu'elle vient de télécharger — le pire cas possible, où l'on transfère
   beaucoup pour n'afficher rien de plus. */
void dkr_tmu_pin(dkr_tmu *t, unsigned long long key);

/* Vide entièrement : à appeler au changement de niveau. */
void dkr_tmu_reset(dkr_tmu *t);

/* Occupation courante, en octets. */
unsigned int dkr_tmu_used(const dkr_tmu *t);

/* Écrit une ligne d'état lisible à l'écran (E08-S01). `out` reçoit au plus
   `size` caractères, terminaison comprise. */
void dkr_tmu_format_status(const dkr_tmu *t, char *out, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_TMU_H */
