/* E05-S01 — amorçage de Glide : contexte, tampons, présentation, fermeture.
 *
 * Périmètre. Cette couche ouvre la carte et rend la main ; elle ne dessine pas.
 * Les textures (E05-S02), le combineur (E05-S03) et le rendu proprement dit
 * viennent après. Elle n'implémente pas non plus l'interface de backend de
 * E04-S01, qui n'existe pas encore : c'est délibéré, et c'est écrit ici pour que
 * personne ne croie E05-S01 terminé. Le jour où cette interface sera là, elle
 * s'adaptera par-dessus sans que rien de ce qui suit ne change.
 *
 * ## Glide est chargée dynamiquement, et ce n'est pas un détail
 *
 * `glide2x.dll` vient du pilote 3dfx, pas du système. Une liaison à l'import la
 * rendrait obligatoire au chargement : sur une machine sans carte 3dfx, Windows
 * refuserait de démarrer le programme en nommant un symbole, ce qui n'apprend
 * rien au joueur. Chargée par `LoadLibrary`, son absence devient une phrase
 * qu'on peut écrire.
 *
 * ## Rien n'est codé en dur
 *
 * Le nombre de TMU et la mémoire disponible pilotent E05-S02 et E05-S04 à
 * l'exécution. Ils sont donc relevés, pas supposés — et l'ADR 0002 impose deux
 * TMU sans interdire d'en trouver une seule, auquel cas le repli multipasse de
 * E05-S04 s'appliquera.
 *
 * Un piège mesuré, et qui vaut d'être connu avant d'écrire la moindre détection :
 * **`grSstQueryHardware` ne distingue pas une Voodoo 1 d'une Voodoo 2**. Le type
 * rendu vaut 0 pour les deux, et la révision FBI est identique — relevé sur la
 * machine de test, voir `docs/research/win95-voodoo2-machine.md`. Ce sont le
 * nombre de TMU et la mémoire par TMU qui décident, et ce sont précisément les
 * deux seules choses dont le moteur ait besoin.
 */
#ifndef DKR_RENDER_GLIDE_H
#define DKR_RENDER_GLIDE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DKR_GLIDE_OK = 0,
    DKR_GLIDE_ERR_NO_LIBRARY,   /* glide2x.dll introuvable */
    DKR_GLIDE_ERR_NO_SYMBOL,    /* la DLL est là mais incomplète */
    DKR_GLIDE_ERR_NO_BOARD,     /* aucune carte 3dfx */
    DKR_GLIDE_ERR_NO_MEMORY,    /* la configuration ne tient pas dans la carte */
    DKR_GLIDE_ERR_OPEN          /* grSstWinOpen a refusé */
} dkr_glide_result;

/* Le texte est destiné au joueur, et il nomme le geste possible : « aucune carte
   3dfx détectée » se répare autrement que « glide2x.dll introuvable ». */
const char *dkr_glide_result_text(dkr_glide_result r);

/* Ce que la carte dit d'elle-même. Rempli par `dkr_glide_detect`. */
typedef struct {
    unsigned  glide_version;    /* 0x254 pour Glide 2.54 */
    int       board_count;
    int       tmu_count;
    unsigned  fb_memory_kb;     /* mémoire de tampon d'image */
    unsigned  tmu_memory_kb[3]; /* par TMU, dans l'ordre */
    int       sli;
} dkr_glide_hardware;

/* Charge la bibliothèque, résout les symboles, interroge le matériel.
   N'ouvre aucun contexte et ne touche pas à l'affichage : on peut donc
   l'appeler pour afficher un diagnostic puis renoncer. */
dkr_glide_result dkr_glide_detect(dkr_glide_hardware *out);

/* Résolutions, dans l'ordre où le repli les essaie. */
typedef enum {
    DKR_GLIDE_RES_640x480 = 0,
    DKR_GLIDE_RES_512x384,
    DKR_GLIDE_RES_400x300,
    DKR_GLIDE_RES_320x240,
    DKR_GLIDE_RES_COUNT
} dkr_glide_resolution;

typedef struct {
    dkr_glide_resolution resolution;  /* celle réellement obtenue */
    int width, height;
    int buffers;                      /* 2 = double tampon */
    int depth_buffer;                 /* 1 si un tampon de profondeur est là */
} dkr_glide_context;

/* Ouvre le contexte à `wanted`, ou à la première résolution inférieure qui tient
   dans la mémoire relevée. `out` reçoit ce qui a été obtenu, qui peut différer
   de ce qui était demandé — l'appelant doit le lire plutôt que le supposer.
 *
 * Le calcul du repli est fait ici et non par essais successifs sur le matériel :
 * `grSstWinOpen` échouant, on ne sait pas *pourquoi*, et un message qui dit
 * « 640x480 ne tient pas dans 2 Mo » vaut mieux qu'un refus muet. */
dkr_glide_result dkr_glide_open(dkr_glide_resolution wanted,
                                dkr_glide_context *out);

/* Efface le tampon arrière puis l'échange. `argb` est la couleur d'effacement. */
void dkr_glide_clear(unsigned argb);
void dkr_glide_swap(void);

/* Ferme le contexte et **restitue le mode d'affichage**.
 *
 * Sur une Voodoo 1 ou 2, la carte prend la main en plein écran par un relais
 * analogique : tant que le contexte est ouvert, l'écran affiche la sortie 3dfx
 * et non celle de la carte 2D. Une fermeture manquée laisse donc l'écran noir,
 * et seul un redémarrage le récupère — ce qui est très pénalisant au moment
 * précis où l'on plante souvent.
 *
 * C'est pourquoi `dkr_glide_open` inscrit cette fonction au registre d'arrêt
 * anormal de E02-S03 : le filtre d'exception la rappelle avant d'afficher quoi
 * que ce soit. Appeler deux fois est sans effet. */
void dkr_glide_shutdown(void);

/* --- Relire ce que la carte a dessiné --------------------------------------- *
 *
 * Sur une Voodoo passthrough, l'écran appartient à la carte : **aucune capture
 * de l'émulateur ne montre la sortie 3dfx**, et tout ce que le projet affirmait
 * jusqu'ici sur le rendu Glide reposait sur l'absence de plantage.
 *
 * `grLfbLock` donne accès au tampon d'image. Cela débloque trois choses d'un
 * coup, et c'est pourquoi cette fonction vaut d'être écrite :
 *
 *   - la vérification visuelle du triangle de E05-S01, jusqu'ici partielle ;
 *   - la mesure de la marge hors écran que Glide tolère (E04-S05) ;
 *   - la comparaison avec le rastériseur de référence (E09-S02), qui est la
 *     raison d'être de tout l'oracle.
 *
 * `out` reçoit du ARGB 32 bits, lignes du haut vers le bas — le format du
 * rastériseur logiciel, pour que les deux images se comparent sans conversion.
 * La carte, elle, travaille en 565 : la conversion réplique les bits de poids
 * fort, sans quoi le blanc ne serait pas blanc et toute comparaison dériverait.
 *
 * Rend le nombre de pixels lus, ou zéro. */
int dkr_glide_read_framebuffer(unsigned *out, int max_pixels,
                               int *width, int *height);

/* --- Ce dont le calque de backend a besoin ---------------------------------- *
 *
 * `glide_backend.c` programme les registres d'état — mélange, profondeur,
 * ciseaux, brouillard — que cette couche-ci n'a pas à connaître. Plutôt que d'y
 * dupliquer le chargement de la DLL, on expose la résolution de symbole.
 *
 * Rend NULL si la bibliothèque n'est pas chargée ou si le symbole manque, ce qui
 * est un cas normal : Glide 2.4 n'exporte pas tout ce que Glide 2.6 exporte, et
 * un état non programmable doit dégrader, pas planter. */
void *dkr_glide_symbol(const char *decorated_name);

/* Trois sommets déjà à la disposition de `GrVertex`, remis tels quels à
   `grDrawTriangle`. Le type reste opaque ici : `backend.h` n'est pas inclus par
   cette couche, et l'assertion de disposition vit dans `backend_layout_check.c`. */
void dkr_glide_draw_raw(const void *a, const void *b, const void *c);

/* Un triangle Gouraud plein écran, pour prouver que la chaîne va jusqu'au pixel.
   Sa place ici est provisoire : elle disparaîtra quand E04-S01 donnera une vraie
   interface de dessin. */
void dkr_glide_draw_test_triangle(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_GLIDE_H */
