/* E04-S07 — mise en œuvre. Le contrat et le raccourci assumé sont dans
 * `texture.h`. */
#include "texture.h"

#include <stdio.h>
#include <string.h>

/* La plus grande texture que le RDP puisse tenir : 4 Kio de mémoire de texture,
   soit 2048 texels en 16 bits. On borne large — 256x256 — pour attraper une
   dimension absurde issue d'un décodage faux sans refuser de vraies textures. */
#define MAX_COTE 256

static unsigned char lire8(const unsigned char *rdram, int native, unsigned int a)
{
    return rdram[native ? (a ^ 3u) : a];
}

static unsigned short lire16(const unsigned char *rdram, int native, unsigned int a)
{
    return (unsigned short)(((unsigned)lire8(rdram, native, a) << 8) |
                            lire8(rdram, native, a + 1u));
}

/* --- Les conversions ------------------------------------------------------- *
 *
 * Toutes produisent du RGBA5551 avec l'alpha en bit 0, la disposition que
 * E05-S02 a mesurée sur la carte.
 *
 * **L'alpha est le piège de ces formats.** Un seul bit d'alpha sur la Voodoo
 * contre huit sur la N64 pour IA8 et IA16 : ce qui était un dégradé de
 * transparence devient un seuil. Le résultat n'est pas faux au sens d'une
 * couleur erronée — il est *dur* là où le jeu voulait du doux, et cela se
 * remarque surtout sur les ombres et les halos. Le seuil est à mi-course,
 * faute d'un meilleur choix, et il est signalé ici plutôt que découvert. */
static unsigned short gris_vers_5551(unsigned int i, unsigned int alpha)
{
    const unsigned int c = i >> 3;   /* 8 bits vers 5 */
    return (unsigned short)((c << 11) | (c << 6) | (c << 1) | (alpha ? 1u : 0u));
}

unsigned int dkr_texture_bytes(dkr_n64_size size, int width, int height)
{
    const unsigned int n = (unsigned int)width * (unsigned int)height;
    if (width <= 0 || height <= 0) {
        return 0u;
    }
    switch (size) {
    case DKR_N64_SIZ_4:  return (n + 1u) / 2u;
    case DKR_N64_SIZ_8:  return n;
    case DKR_N64_SIZ_16: return n * 2u;
    case DKR_N64_SIZ_32: return n * 4u;
    default:             return 0u;
    }
}

const char *dkr_texture_format_name(dkr_n64_format format, dkr_n64_size size)
{
    static const char *bits[4] = { "4", "8", "16", "32" };
    static char nom[16];
    const char *f;
    switch (format) {
    case DKR_N64_FMT_RGBA: f = "RGBA"; break;
    case DKR_N64_FMT_YUV:  f = "YUV";  break;
    case DKR_N64_FMT_CI:   f = "CI";   break;
    case DKR_N64_FMT_IA:   f = "IA";   break;
    case DKR_N64_FMT_I:    f = "I";    break;
    default:               f = "?";    break;
    }
    sprintf(nom, "%s%s", f, bits[(int)size & 3]);
    return nom;
}

int dkr_texture_convert(const unsigned char *rdram, unsigned int rdram_size,
                        int native, unsigned int address,
                        dkr_n64_format format, dkr_n64_size size,
                        int width, int height,
                        unsigned short *sortie, dkr_texture_stats *stats)
{
    const unsigned int octets = dkr_texture_bytes(size, width, height);
    unsigned int i, n;

    if (!rdram || !sortie || octets == 0u) {
        return 0;
    }
    if (width > MAX_COTE || height > MAX_COTE) {
        if (stats) { stats->trop_grandes++; }
        return 0;
    }
    /* La borne est vérifiée ici et une seule fois, plutôt qu'à chaque texel :
       c'est le même choix que dans le décodeur de display list, et pour la même
       raison — un seul endroit à relire pour s'assurer que rien ne sort. */
    if ((unsigned long long)address + octets > (unsigned long long)rdram_size) {
        if (stats) { stats->hors_rdram++; }
        return 0;
    }

    n = (unsigned int)width * (unsigned int)height;

    if (format == DKR_N64_FMT_RGBA && size == DKR_N64_SIZ_16) {
        /* Le cas courant de DKR, et le seul qui soit une recopie : le RDP range
           déjà du 5551 avec l'alpha en bit 0. Rien à convertir, seulement à
           remettre dans l'ordre des octets de l'hôte. */
        for (i = 0; i < n; i++) {
            sortie[i] = lire16(rdram, native, address + i * 2u);
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    if (format == DKR_N64_FMT_RGBA && size == DKR_N64_SIZ_32) {
        for (i = 0; i < n; i++) {
            const unsigned int a = address + i * 4u;
            const unsigned int r = lire8(rdram, native, a) >> 3;
            const unsigned int v = lire8(rdram, native, a + 1u) >> 3;
            const unsigned int b = lire8(rdram, native, a + 2u) >> 3;
            const unsigned int al = lire8(rdram, native, a + 3u);
            sortie[i] = (unsigned short)((r << 11) | (v << 6) | (b << 1) |
                                         (al >= 128u ? 1u : 0u));
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    if (format == DKR_N64_FMT_I && size == DKR_N64_SIZ_8) {
        for (i = 0; i < n; i++) {
            sortie[i] = gris_vers_5551(lire8(rdram, native, address + i), 1u);
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    if (format == DKR_N64_FMT_I && size == DKR_N64_SIZ_4) {
        for (i = 0; i < n; i++) {
            const unsigned char o = lire8(rdram, native, address + i / 2u);
            const unsigned int  q = (i & 1u) ? (o & 0x0Fu) : (unsigned int)(o >> 4);
            /* 4 bits vers 8 par réplication : 15 doit donner 255, non 240. */
            sortie[i] = gris_vers_5551((q << 4) | q, 1u);
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    if (format == DKR_N64_FMT_IA && size == DKR_N64_SIZ_16) {
        for (i = 0; i < n; i++) {
            const unsigned short m = lire16(rdram, native, address + i * 2u);
            sortie[i] = gris_vers_5551((m >> 8) & 0xFFu, (m & 0xFFu) >= 128u);
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    if (format == DKR_N64_FMT_IA && size == DKR_N64_SIZ_8) {
        for (i = 0; i < n; i++) {
            const unsigned char o = lire8(rdram, native, address + i);
            const unsigned int  it = (unsigned int)(o >> 4);
            sortie[i] = gris_vers_5551((it << 4) | it, (o & 0x0Fu) >= 8u);
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    if (format == DKR_N64_FMT_IA && size == DKR_N64_SIZ_4) {
        for (i = 0; i < n; i++) {
            const unsigned char o = lire8(rdram, native, address + i / 2u);
            const unsigned int  q = (i & 1u) ? (o & 0x0Fu) : (unsigned int)(o >> 4);
            const unsigned int  it = q >> 1;   /* trois bits d'intensité */
            const unsigned int  it8 = (it << 5) | (it << 2) | (it >> 1);
            sortie[i] = gris_vers_5551(it8, q & 1u);
        }
        if (stats) { stats->converties++; }
        return 1;
    }

    /* CI4, CI8 et YUV restent dehors. Les indexés demandent la palette, que le
       RDP charge par `LOADTLUT` dans l'autre moitié de la mémoire de texture ;
       les servir sans elle donnerait des couleurs arbitraires, ce qui est pire
       qu'une absence puisque cela passe pour du rendu. On refuse, on compte, et
       l'appelant ne dessine pas plutôt que de dessiner faux. */
    if (stats) { stats->non_prises_en_charge++; }
    return 0;
}
