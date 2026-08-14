/* Ce que la TMU exige réellement — mesuré avant d'écrire l'allocateur.
 *
 * Le ticket E05-S02 demande de mesurer avant de concevoir. La mesure qu'il vise
 * porte sur le jeu — nombre de textures par niveau, motif de réutilisation — et
 * suppose la ROM. Celle-ci porte sur **le matériel**, ne la suppose pas, et
 * conditionne autant l'allocateur : l'alignement et la granularité de la TMU
 * décident de la fragmentation, et se supposer est ici particulièrement risqué.
 *
 * ## Pourquoi ce ne sont pas des constantes qu'on peut lire dans un livre
 *
 * Glide n'a pas de gestionnaire de textures. Elle expose la mémoire de la TMU
 * comme un espace d'adressage brut : l'application choisit une adresse, y
 * télécharge, et lie cette adresse au dessin. `grTexTextureMemRequired` dit
 * combien d'octets une texture occupe — et ce nombre inclut l'arrondi que la
 * carte impose, qui n'est pas déductible de la largeur et de la hauteur.
 *
 * Un allocateur qui se contenterait de `largeur × hauteur × 2` empilerait les
 * textures trop serré. Le symptôme ne serait pas une erreur : ce serait une
 * texture qui en écrase une autre, donc un décor qui porte le motif d'un autre,
 * à un endroit qui dépend de l'ordre de chargement. C'est-à-dire le genre de
 * défaut qu'on poursuit pendant des jours.
 *
 * ## Ce témoin valide aussi ses propres constantes
 *
 * Comme pour les états (`win95-glide-etats.md`), il n'y a pas de `glide.h` sur
 * cette machine et les énumérations sont écrites de mémoire. Elles sont ici
 * vérifiables sans matériel supplémentaire : pour une texture 16 bits sans
 * mipmap, la taille attendue est connue analytiquement. Si `GR_LOD_*` ou
 * `GR_ASPECT_*` étaient faux, la taille rendue le dirait immédiatement.
 */
#include "render/glide.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (!ok) { g_fails++; }
}

/* --- Les énumérations de texture de Glide 2.x -------------------------------- */

/* Le niveau de détail nomme la **plus grande dimension**, et décroît : 256 vaut
   zéro, 1 vaut huit. C'est l'inverse de l'intuition et la première chose à
   vérifier. */
#define GR_LOD_256   0
#define GR_LOD_128   1
#define GR_LOD_64    2
#define GR_LOD_32    3
#define GR_LOD_16    4
#define GR_LOD_8     5
#define GR_LOD_4     6
#define GR_LOD_2     7
#define GR_LOD_1     8

#define GR_ASPECT_8x1  0
#define GR_ASPECT_4x1  1
#define GR_ASPECT_2x1  2
#define GR_ASPECT_1x1  3
#define GR_ASPECT_1x2  4
#define GR_ASPECT_1x4  5
#define GR_ASPECT_1x8  6

#define GR_TEXFMT_RGB_565    0x0A
#define GR_TEXFMT_ARGB_1555  0x0B
#define GR_TEXFMT_ARGB_4444  0x0C
#define GR_TEXFMT_ALPHA_8    0x02
#define GR_TEXFMT_P_8        0x05

#define GR_MIPMAPLEVELMASK_BOTH  0x03
#define GR_TMU0  0
#define GR_TMU1  1

typedef struct {
    int   smallLod;
    int   largeLod;
    int   aspectRatio;
    int   format;
    void *data;
} GrTexInfo;

typedef unsigned int (__stdcall *pfn_min_max)(int tmu);
typedef unsigned int (__stdcall *pfn_required)(unsigned evenOdd, GrTexInfo *info);
typedef void         (__stdcall *pfn_download)(int tmu, unsigned start,
                                               unsigned evenOdd, GrTexInfo *info);

/* La taille analytique d'une texture 16 bits, à partir du couple (lod, aspect).
   C'est ce que l'allocateur *croirait* si personne n'interrogeait la carte. */
static void dims_of(int lod, int aspect, int *w, int *h)
{
    const int big = 256 >> lod;
    switch (aspect) {
    case GR_ASPECT_8x1: *w = big; *h = big / 8; break;
    case GR_ASPECT_4x1: *w = big; *h = big / 4; break;
    case GR_ASPECT_2x1: *w = big; *h = big / 2; break;
    case GR_ASPECT_1x1: *w = big; *h = big;     break;
    case GR_ASPECT_1x2: *w = big / 2; *h = big; break;
    case GR_ASPECT_1x4: *w = big / 4; *h = big; break;
    default:            *w = big / 8; *h = big; break;
    }
    if (*w < 1) { *w = 1; }
    if (*h < 1) { *h = 1; }
}

int main(void)
{
    dkr_glide_hardware   hw;
    dkr_glide_context    ctx;
    pfn_min_max  tex_min, tex_max;
    pfn_required required;
    pfn_download download;

    g_out = fopen("D:\\TMU.TXT", "w");
    say("ce que la TMU exige, mesure sur la carte\n\n");

    if (dkr_glide_detect(&hw) != DKR_GLIDE_OK) {
        say("ECHEC : pas de carte\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    say("  TMU presentes    : %d\n", hw.tmu_count);
    say("  memoire par TMU  : %u Ko, %u Ko\n",
        hw.tmu_memory_kb[0], hw.tmu_memory_kb[1]);

    if (dkr_glide_open(DKR_GLIDE_RES_640x480, &ctx) != DKR_GLIDE_OK) {
        say("ECHEC : contexte refuse\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }

    tex_min  = (pfn_min_max)  dkr_glide_symbol("_grTexMinAddress@4");
    tex_max  = (pfn_min_max)  dkr_glide_symbol("_grTexMaxAddress@4");
    required = (pfn_required) dkr_glide_symbol("_grTexTextureMemRequired@8");
    download = (pfn_download) dkr_glide_symbol("_grTexDownloadMipMap@16");

    say("\n-- les symboles --\n");
    check("grTexMinAddress",        tex_min  != 0);
    check("grTexMaxAddress",        tex_max  != 0);
    check("grTexTextureMemRequired",required != 0);
    check("grTexDownloadMipMap",    download != 0);

    /* --- L'espace adressable ------------------------------------------------- *
     *
     * Ce n'est pas « zéro à deux mégaoctets ». Glide réserve, et l'adresse
     * minimale n'est pas nécessairement nulle. Un allocateur qui partirait de
     * zéro écraserait ce que la bibliothèque y a mis. */
    if (tex_min && tex_max) {
        int t;
        say("\n-- l'espace adressable, par TMU --\n");
        for (t = 0; t < hw.tmu_count && t < 3; t++) {
            const unsigned lo = tex_min(t);
            const unsigned hi = tex_max(t);
            say("  TMU %d : de 0x%08X a 0x%08X, soit %u Ko utilisables\n",
                t, lo, hi, (hi - lo) / 1024u);
        }
        check("l'espace de la TMU 0 est non vide", tex_max(0) > tex_min(0));
        if (hw.tmu_count > 1) {
            check("les deux TMU exposent le meme espace",
                  tex_min(0) == tex_min(1) && tex_max(0) == tex_max(1));
        }
    }

    /* --- Ce qu'une texture coûte réellement ---------------------------------- */
    if (required) {
        static const struct { int lod, aspect; const char *nom; } CAS[] = {
            { GR_LOD_256, GR_ASPECT_1x1, "256x256" },
            { GR_LOD_128, GR_ASPECT_1x1, "128x128" },
            { GR_LOD_64,  GR_ASPECT_1x1, "64x64"   },
            { GR_LOD_32,  GR_ASPECT_1x1, "32x32"   },
            { GR_LOD_16,  GR_ASPECT_1x1, "16x16"   },
            { GR_LOD_8,   GR_ASPECT_1x1, "8x8"     },
            { GR_LOD_4,   GR_ASPECT_1x1, "4x4"     },
            { GR_LOD_2,   GR_ASPECT_1x1, "2x2"     },
            { GR_LOD_1,   GR_ASPECT_1x1, "1x1"     },
            { GR_LOD_64,  GR_ASPECT_2x1, "64x32"   },
            { GR_LOD_64,  GR_ASPECT_4x1, "64x16"   },
            { GR_LOD_64,  GR_ASPECT_8x1, "64x8"    },
            { GR_LOD_64,  GR_ASPECT_1x2, "32x64"   },
            { GR_LOD_64,  GR_ASPECT_1x8, "8x64"    },
            { GR_LOD_32,  GR_ASPECT_2x1, "32x16"   },
        };
        const int n = (int)(sizeof(CAS) / sizeof(CAS[0]));
        int i, exact = 0, rounded = 0;

        say("\n-- ce qu'une texture 16 bits coute en TMU --\n");
        say("  %-9s %8s %8s %s\n", "taille", "calcule", "carte", "");
        for (i = 0; i < n; i++) {
            GrTexInfo info;
            int w = 0, h = 0;
            unsigned got, want;

            dims_of(CAS[i].lod, CAS[i].aspect, &w, &h);
            want = (unsigned)(w * h * 2);

            memset(&info, 0, sizeof(info));
            info.smallLod    = CAS[i].lod;
            info.largeLod    = CAS[i].lod;      /* pas de mipmap : un seul niveau */
            info.aspectRatio = CAS[i].aspect;
            info.format      = GR_TEXFMT_RGB_565;
            info.data        = 0;

            got = required(GR_MIPMAPLEVELMASK_BOTH, &info);
            say("  %-9s %8u %8u %s\n", CAS[i].nom, want, got,
                (got == want) ? "" : (got > want ? "<-- arrondi" : "<-- INFERIEUR ?!"));
            if (got == want)      { exact++; }
            else if (got > want)  { rounded++; }
        }
        say("\n  exactes : %d, arrondies : %d, sur %d\n", exact, rounded, n);

        /* Si tout est exact, l'allocateur peut empiler au plus serré. Sinon, il
           doit demander la taille à la carte pour *chaque* texture — ce que
           l'allocateur fera de toute façon, mais il est utile de savoir si l'on
           gaspille. */
        check("aucune taille rendue n'est inferieure au calcul",
              exact + rounded == n);

        /* --- La granularité d'adresse ---------------------------------------- *
         *
         * Deux textures consécutives se placent à `addr += required(...)`. Reste
         * à savoir si l'adresse elle-même doit être alignée. On le déduit du
         * plus petit coût rendu : s'il vaut 8 pour une texture de 1x1 en 16
         * bits, c'est-à-dire 2 octets utiles, l'alignement est de 8. */
        {
            GrTexInfo info;
            unsigned smallest;
            memset(&info, 0, sizeof(info));
            info.smallLod = info.largeLod = GR_LOD_1;
            info.aspectRatio = GR_ASPECT_1x1;
            info.format = GR_TEXFMT_RGB_565;
            smallest = required(GR_MIPMAPLEVELMASK_BOTH, &info);
            say("\n  plus petite allocation possible : %u octets\n", smallest);
            say("  (une texture 1x1 en 16 bits n'occupe que 2 octets utiles)\n");
        }
    }

    /* --- Les formats ---------------------------------------------------------- *
     * Un format 8 bits doit coûter moitié moins. Si ce n'est pas le cas, c'est
     * que la valeur d'énumération est fausse — et une valeur de format fausse
     * ne provoque pas d'erreur, elle produit une texture illisible. */
    if (required) {
        static const struct { int fmt; const char *nom; int bpp; } FMT[] = {
            { GR_TEXFMT_RGB_565,   "RGB 565",   16 },
            { GR_TEXFMT_ARGB_1555, "ARGB 1555", 16 },
            { GR_TEXFMT_ARGB_4444, "ARGB 4444", 16 },
            { GR_TEXFMT_ALPHA_8,   "ALPHA 8",    8 },
            { GR_TEXFMT_P_8,       "palettise 8",8 },
        };
        int i, coherents = 0;
        say("\n-- les formats, sur une texture 64x64 --\n");
        for (i = 0; i < 5; i++) {
            GrTexInfo info;
            unsigned got;
            const unsigned want = (unsigned)(64 * 64 * FMT[i].bpp / 8);
            memset(&info, 0, sizeof(info));
            info.smallLod = info.largeLod = GR_LOD_64;
            info.aspectRatio = GR_ASPECT_1x1;
            info.format = FMT[i].fmt;
            got = required(GR_MIPMAPLEVELMASK_BOTH, &info);
            say("  %-12s %2d bits : %6u octets (calcule %6u) %s\n",
                FMT[i].nom, FMT[i].bpp, got, want, (got == want) ? "" : "<-- ecart");
            if (got == want) { coherents++; }
        }
        check("les cinq formats coutent ce que leur profondeur annonce",
              coherents == 5);
    }

    /* --- Un téléchargement réel ---------------------------------------------- *
     *
     * La mesure ne vaut que si l'on peut effectivement écrire dans cet espace.
     * On télécharge une texture reconnaissable ; E05-S02 verifiera ensuite
     * qu'elle apparait a l'ecran, ce que la relecture du tampon rend possible. */
    if (download && tex_min) {
        static unsigned short damier[64 * 64];
        GrTexInfo info;
        int x, y;
        for (y = 0; y < 64; y++) {
            for (x = 0; x < 64; x++) {
                damier[y * 64 + x] = (unsigned short)
                    (((x / 8 + y / 8) & 1) ? 0xF800 : 0x001F);
            }
        }
        memset(&info, 0, sizeof(info));
        info.smallLod = info.largeLod = GR_LOD_64;
        info.aspectRatio = GR_ASPECT_1x1;
        info.format = GR_TEXFMT_RGB_565;
        info.data = damier;
        download(GR_TMU0, tex_min(0), GR_MIPMAPLEVELMASK_BOTH, &info);
        say("\n  telechargement d'un damier 64x64 a 0x%08X : rendu la main\n",
            tex_min(0));
        /* Glide ne rend rien : l'absence de plantage est tout ce qu'on obtient
           ici, et c'est pourquoi la vraie verification est visuelle. */
    }

    dkr_glide_shutdown();
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
