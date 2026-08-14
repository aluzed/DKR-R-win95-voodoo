/* E05-S01 — mise en œuvre. Le contrat et les pièges sont dans `glide.h`. */
#include "glide.h"

#include <windows.h>
#include <string.h>

#include "win95/startup.h"

/* --- Les constantes de Glide 2.x ------------------------------------------ *
 *
 * Recopiées de `glide.h` de 3dfx plutôt qu'incluses : la cible n'a pas le SDK,
 * et une poignée de constantes vaut mieux qu'une dépendance de plus. Chacune est
 * vérifiée par la démonstration de E09-S01, qui a ouvert un contexte et dessiné
 * un triangle avec ces valeurs.
 */
typedef unsigned int  FxU32;
typedef unsigned char FxU8;
typedef int           FxBool;

#define GR_RESOLUTION_320x240   0x0
#define GR_RESOLUTION_400x300   0x4
#define GR_RESOLUTION_512x384   0x6
#define GR_RESOLUTION_640x480   0x7
#define GR_REFRESH_60Hz         0x0
#define GR_COLORFORMAT_ARGB     0x0
#define GR_ORIGIN_UPPER_LEFT    0x0
#define GR_BUFFER_BACKBUFFER    0x1
#define GR_WDEPTHVALUE_FARTHEST 0xFFFF

/* `GrVertex` de Glide 2.x. L'ordre n'est pas intuitif — `ooz` et `a`
   s'intercalent entre les couleurs et `oow` — et une disposition « logique »
   compile parfaitement en rendant des couleurs permutées : Glide lit les
   flottants aux mauvais décalages, sans la moindre erreur. Vérifié à l'écran
   par la démonstration de E09-S01, où un sommet rouge sortait vert. */
typedef struct {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    float tmuvtx[3 * 4];
} GrVertex;

typedef FxU32  (WINAPI *pfn_grGlideInit)(void);
typedef void   (WINAPI *pfn_grGlideShutdown)(void);
typedef FxBool (WINAPI *pfn_grSstQueryHardware)(void *);
typedef void   (WINAPI *pfn_grSstSelect)(int);
typedef FxBool (WINAPI *pfn_grSstWinOpen)(FxU32, int, int, int, int, int, int);
typedef void   (WINAPI *pfn_grSstWinClose)(void);
typedef void   (WINAPI *pfn_grBufferClear)(FxU32, FxU8, FxU32);
typedef void   (WINAPI *pfn_grBufferSwap)(int);
typedef void   (WINAPI *pfn_grDrawTriangle)(const void *, const void *, const void *);
typedef void   (WINAPI *pfn_grGlideGetVersion)(char *);

static struct {
    HMODULE dll;
    pfn_grGlideInit        init;
    pfn_grGlideShutdown    shutdown;
    pfn_grSstQueryHardware query;
    pfn_grSstSelect        select;
    pfn_grSstWinOpen       win_open;
    pfn_grSstWinClose      win_close;
    pfn_grBufferClear      clear;
    pfn_grBufferSwap       swap;
    pfn_grDrawTriangle     triangle;
    pfn_grGlideGetVersion  version;

    int  initialised;   /* grGlideInit appelé */
    int  context_open;  /* grSstWinOpen réussi */
    int  hardware_known;
    dkr_glide_hardware hw;
    dkr_glide_context ctx;
} g;

const char *dkr_glide_result_text(dkr_glide_result r)
{
    switch (r) {
    case DKR_GLIDE_OK:             return "succes";
    case DKR_GLIDE_ERR_NO_LIBRARY: return "glide2x.dll introuvable — pilote 3dfx absent ?";
    case DKR_GLIDE_ERR_NO_SYMBOL:  return "glide2x.dll incomplete — version inattendue";
    case DKR_GLIDE_ERR_NO_BOARD:   return "aucune carte 3dfx detectee";
    case DKR_GLIDE_ERR_NO_MEMORY:  return "la carte n'a pas assez de memoire d'image";
    default:                       return "ouverture du contexte refusee";
    }
}

/* --- Résolutions ----------------------------------------------------------- */

static const struct { int glide_id, w, h; } RESOLUTIONS[DKR_GLIDE_RES_COUNT] = {
    { GR_RESOLUTION_640x480, 640, 480 },
    { GR_RESOLUTION_512x384, 512, 384 },
    { GR_RESOLUTION_400x300, 400, 300 },
    { GR_RESOLUTION_320x240, 320, 240 },
};

/* Ce que coûte une résolution en mémoire de tampon d'image.
 *
 * Deux tampons de couleur en 16 bits plus un tampon de profondeur en 16 bits :
 * trois surfaces de `w * h * 2` octets. C'est le calcul de l'ADR 0002, qui
 * écarte le triple buffering pour cette raison — 2,34 Mio contre 2 Mo sur la
 * Voodoo 2 8 Mo. */
static unsigned fb_cost_kb(int w, int h)
{
    return (unsigned)(((long)w * h * 2 * 3) / 1024);
}

/* --- Détection ------------------------------------------------------------- */

static void unload(void)
{
    if (g.dll) {
        FreeLibrary(g.dll);
    }
    memset(&g, 0, sizeof(g));
}

static void *sym(const char *decorated)
{
    return (void *)GetProcAddress(g.dll, decorated);
}

dkr_glide_result dkr_glide_detect(dkr_glide_hardware *out)
{
    /* La structure rendue par `grSstQueryHardware` est lue par décalages plutôt
       que par une déclaration : sa disposition exacte varie entre versions de
       Glide, et seuls les premiers champs nous intéressent. Le tampon est
       largement dimensionné — Glide écrit la configuration de toutes les cartes
       possibles, et un tampon trop court serait débordé en silence. */
    FxU32 hw[128];
    char  version_text[80];

    /* Idempotente, et il a fallu la machine pour l'apprendre.
     *
     * La première version déchargeait et rechargeait `glide2x.dll` à chaque
     * appel. Le témoin appelait `detect` puis `open`, qui redétectait : le
     * second `grGlideInit` tombait sur une bibliothèque qui tenait encore la
     * carte, et Glide refusait par **« Mutual exclusion prohibits this »** —
     * un message qui ne désigne pas sa cause.
     *
     * Une detection qui a des effets de bord n'est pas une detection. Celle-ci
     * rend simplement ce qu'elle sait déjà. */
    if (g.hardware_known) {
        if (out) { *out = g.hw; }
        return DKR_GLIDE_OK;
    }
    if (g.dll) {
        unload();
    }
    memset(&g, 0, sizeof(g));

    g.dll = LoadLibraryA("glide2x.dll");
    if (!g.dll) {
        return DKR_GLIDE_ERR_NO_LIBRARY;
    }

    /* Les noms portent la décoration stdcall complète : `glide2x.dll` exporte
       `_grGlideInit@0` et non `grGlideInit`. */
    g.init      = (pfn_grGlideInit)        sym("_grGlideInit@0");
    g.shutdown  = (pfn_grGlideShutdown)    sym("_grGlideShutdown@0");
    g.query     = (pfn_grSstQueryHardware) sym("_grSstQueryHardware@4");
    g.select    = (pfn_grSstSelect)        sym("_grSstSelect@4");
    g.win_open  = (pfn_grSstWinOpen)       sym("_grSstWinOpen@28");
    g.win_close = (pfn_grSstWinClose)      sym("_grSstWinClose@0");
    g.clear     = (pfn_grBufferClear)      sym("_grBufferClear@12");
    g.swap      = (pfn_grBufferSwap)       sym("_grBufferSwap@4");
    g.triangle  = (pfn_grDrawTriangle)     sym("_grDrawTriangle@12");
    g.version   = (pfn_grGlideGetVersion)  sym("_grGlideGetVersion@4");

    if (!g.init || !g.shutdown || !g.query || !g.select || !g.win_open ||
        !g.win_close || !g.clear || !g.swap || !g.triangle) {
        unload();
        return DKR_GLIDE_ERR_NO_SYMBOL;
    }

    g.init();
    g.initialised = 1;

    memset(hw, 0, sizeof(hw));
    if (!g.query(hw)) {
        g.shutdown();
        unload();
        return DKR_GLIDE_ERR_NO_BOARD;
    }

    {
        dkr_glide_hardware *const info = &g.hw;
        int i;
        memset(info, 0, sizeof(*info));
        /* Disposition de `GrHwConfiguration` : num_sst, puis pour la carte 0
           type, fbRam, fbiRev, nTexelfx, sliDetect, puis (tmuRev, tmuRam) par
           TMU. Les mémoires sont en mégaoctets. */
        info->board_count  = (int)hw[0];
        info->tmu_count    = (int)hw[4];
        info->fb_memory_kb = hw[2] * 1024u;
        info->sli          = (int)hw[5];
        for (i = 0; i < 3 && i < info->tmu_count; i++) {
            info->tmu_memory_kb[i] = hw[7 + (unsigned)i * 2] * 1024u;
        }
        if (g.version) {
            /* « Glide 2.54 » → 0x254, sans dépendre de la forme exacte du
               texte : on prend les chiffres et le point. */
            const char *p;
            memset(version_text, 0, sizeof(version_text));
            g.version(version_text);
            version_text[sizeof(version_text) - 1] = '\0';
            for (p = version_text; *p; p++) {
                if (*p >= '0' && *p <= '9' && p[1] == '.') {
                    info->glide_version = (unsigned)(p[0] - '0') * 0x100u
                                       + (unsigned)(p[2] - '0') * 0x10u
                                       + (unsigned)(p[3] >= '0' && p[3] <= '9'
                                                    ? p[3] - '0' : 0);
                    break;
                }
            }
        }
        if (info->board_count <= 0) {
            g.shutdown();
            unload();
            return DKR_GLIDE_ERR_NO_BOARD;
        }
        g.hardware_known = 1;
        if (out) { *out = *info; }
    }
    return DKR_GLIDE_OK;
}

/* --- Ouverture ------------------------------------------------------------- */

/* Rappelée par le filtre d'exception de E02-S03. Une Voodoo passthrough qui
   garde la main laisse l'écran noir jusqu'au redémarrage. */
static void restore_display_on_crash(void)
{
    dkr_glide_shutdown();
}

dkr_glide_result dkr_glide_open(dkr_glide_resolution wanted,
                                dkr_glide_context *out)
{
    dkr_glide_hardware hw;
    dkr_glide_result   r;
    int                i;

    r = dkr_glide_detect(&hw);
    if (r != DKR_GLIDE_OK) {
        return r;
    }
    g.select(0);

    if (wanted < 0 || wanted >= DKR_GLIDE_RES_COUNT) {
        wanted = DKR_GLIDE_RES_640x480;
    }

    /* Le repli descend depuis la résolution demandée. Le budget est calculé,
       non deviné : `grSstWinOpen` qui échoue ne dit pas pourquoi, et « 640x480
       ne tient pas dans 2 Mo » est une phrase qu'on peut montrer. */
    for (i = (int)wanted; i < DKR_GLIDE_RES_COUNT; i++) {
        if (fb_cost_kb(RESOLUTIONS[i].w, RESOLUTIONS[i].h) > hw.fb_memory_kb) {
            continue;
        }
        if (g.win_open(0, RESOLUTIONS[i].glide_id, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
            g.context_open   = 1;
            g.ctx.resolution = (dkr_glide_resolution)i;
            g.ctx.width      = RESOLUTIONS[i].w;
            g.ctx.height     = RESOLUTIONS[i].h;
            g.ctx.buffers    = 2;
            g.ctx.depth_buffer = 1;
            if (out) { *out = g.ctx; }
            /* Inscrit **après** l'ouverture : avant, il n'y aurait rien à
               restituer, et le registre est de taille fixe. */
            dkr_win95_at_abnormal_exit(restore_display_on_crash);
            return DKR_GLIDE_OK;
        }
    }

    /* Aucune n'a tenu. Distinguer « pas assez de mémoire » de « refus » aide
       l'appelant à écrire quelque chose d'utile. */
    r = (fb_cost_kb(RESOLUTIONS[wanted].w, RESOLUTIONS[wanted].h) > hw.fb_memory_kb)
        ? DKR_GLIDE_ERR_NO_MEMORY : DKR_GLIDE_ERR_OPEN;
    g.shutdown();
    unload();
    return r;
}

void dkr_glide_clear(unsigned argb)
{
    if (g.context_open) {
        g.clear(argb, 0, GR_WDEPTHVALUE_FARTHEST);
    }
}

void dkr_glide_swap(void)
{
    if (g.context_open) {
        /* 1 : synchroniser sur le balayage. Le choix entre celui-ci et l'échange
           immédiat se mesure en E06-S04 ; par défaut on évite le déchirement. */
        g.swap(1);
    }
}

void dkr_glide_shutdown(void)
{
    /* Idempotente, et elle doit l'être : le filtre d'exception peut l'appeler
       alors que la fermeture normale est déjà passée. */
    if (g.context_open) {
        g.win_close();
        g.context_open = 0;
    }
    if (g.initialised) {
        g.shutdown();
        g.initialised = 0;
    }
    unload();
}

void dkr_glide_draw_test_triangle(void)
{
    GrVertex a, b, c;
    const float w = (float)g.ctx.width;
    const float h = (float)g.ctx.height;

    if (!g.context_open) {
        return;
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));

    a.x = w * 0.5f;  a.y = h * 0.15f; a.r = 255.0f; a.g = 0.0f;   a.b = 0.0f;
    b.x = w * 0.85f; b.y = h * 0.85f; b.r = 0.0f;   b.g = 255.0f; b.b = 0.0f;
    c.x = w * 0.15f; c.y = h * 0.85f; c.r = 0.0f;   c.g = 0.0f;   c.b = 255.0f;
    a.a = b.a = c.a = 255.0f;
    a.oow = b.oow = c.oow = 1.0f;
    a.ooz = b.ooz = c.ooz = 1.0f;

    g.triangle(&a, &b, &c);
}
