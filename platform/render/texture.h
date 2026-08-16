/* E04-S07 — décodage des formats de texture de la N64.
 *
 * Le RDP échantillonne huit combinaisons de format et de taille. La Voodoo n'en
 * connaît qu'une qui nous intéresse : **RGBA5551**, son format naturel, mesuré
 * par E05-S02. Tout ce que ce module fait est donc de ramener les huit à celui-
 * là, une fois, au chargement — et non par texel au dessin, ce qu'un Pentium II
 * ne pardonnerait pas.
 *
 * ## Ce qui rend le problème abordable, et ce qui ne l'est pas
 *
 * Abordable : DKR emploie surtout RGBA16, c'est-à-dire **déjà du 5551**. La
 * conversion y est une recopie, à l'ordre des octets près.
 *
 * Moins abordable : les formats indexés (CI4, CI8) demandent la palette, que le
 * RDP charge par une commande distincte (`LOADTLUT`) dans une autre moitié de
 * la mémoire de texture. Un décodeur qui les ignorerait produirait des surfaces
 * uniformément noires plutôt qu'une erreur — d'où le compte de formats non pris
 * en charge, que l'appelant doit regarder.
 *
 * ## Le raccourci assumé : lire la RDRAM plutôt qu'émuler la TMEM
 *
 * Le RDP ne dessine pas depuis la RDRAM : il copie d'abord dans ses 4 Kio de
 * mémoire de texture par `LOADBLOCK` ou `LOADTILE`, et échantillonne ensuite
 * depuis là. Émuler fidèlement cette mémoire — avec l'entrelacement par mot pair
 * et impair sur les lignes impaires — est un travail à part entière.
 *
 * Ce module lit **directement en RDRAM**, à l'adresse de `SETTIMG`, avec les
 * dimensions de `SETTILESIZE`. C'est exact tant qu'une texture est chargée d'un
 * bloc et dessinée entière, ce qui est le cas courant, et faux pour les atlas
 * dont on ne charge qu'un pavé. Le raccourci est nommé ici plutôt que découvert
 * plus tard sur une texture décalée.
 */
#ifndef DKR_RENDER_TEXTURE_H
#define DKR_RENDER_TEXTURE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Les formats du RDP, valeurs de `G_IM_FMT_*`. */
typedef enum {
    DKR_N64_FMT_RGBA = 0,
    DKR_N64_FMT_YUV,
    DKR_N64_FMT_CI,
    DKR_N64_FMT_IA,
    DKR_N64_FMT_I
} dkr_n64_format;

/* Les tailles, valeurs de `G_IM_SIZ_*` : 4, 8, 16 et 32 bits par texel. */
typedef enum {
    DKR_N64_SIZ_4 = 0,
    DKR_N64_SIZ_8,
    DKR_N64_SIZ_16,
    DKR_N64_SIZ_32
} dkr_n64_size;

/* Ce que le décodeur a rencontré. Les compteurs sont là parce qu'un format non
   pris en charge ne se voit pas : il produit une surface noire, pas une
   erreur. */
typedef struct {
    unsigned long converties;
    unsigned long non_prises_en_charge;
    unsigned long trop_grandes;
    unsigned long hors_rdram;
} dkr_texture_stats;

/* Convertit une texture de la RDRAM vers RGBA5551.
 *
 * `rdram` est l'instantané, `rdram_size` sa taille, `native` la disposition
 * entrelacée par XOR-3 de librecomp (voir `f3ddkr.h`). `sortie` reçoit
 * `width * height` demi-mots.
 *
 * Rend 1 en cas de succès, 0 sinon — et dans ce cas l'appelant ne doit pas
 * dessiner avec, plutôt que de dessiner du noir.
 */
int dkr_texture_convert(const unsigned char *rdram, unsigned int rdram_size,
                        int native, unsigned int address,
                        dkr_n64_format format, dkr_n64_size size,
                        int width, int height,
                        unsigned short *sortie, dkr_texture_stats *stats);

/* La taille en octets d'une texture de ces dimensions dans ce format. Rend 0
   si la combinaison n'a pas de sens. */
unsigned int dkr_texture_bytes(dkr_n64_size size, int width, int height);

/* Le nom du format, pour les journaux. Un format non pris en charge doit
   pouvoir être nommé dans le compte rendu, sans quoi « non pris en charge :
   1240 » n'oriente vers rien. */
const char *dkr_texture_format_name(dkr_n64_format format, dkr_n64_size size);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_TEXTURE_H */
