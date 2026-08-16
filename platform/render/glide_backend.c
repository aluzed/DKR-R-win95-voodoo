/* E05-S01 — la Voodoo se présente comme un `dkr_render_backend`.
 *
 * `glide.c` ouvre la carte ; ce fichier la branche sur l'interface de E04-S01,
 * de sorte que la chaîne assemblée en `tests/test_pipeline.c` puisse la traverser
 * sans changer une ligne. C'est le seul moyen de comparer la carte au rastériseur
 * de référence sur *la même* entrée, ce qui est la raison d'être de l'oracle.
 *
 * ## Ce que ce module ne fait pas, et pourquoi
 *
 * Il ne charge aucune texture. `texture_upload` rend zéro et le dit. Placer une
 * texture en mémoire de TMU est un problème d'allocation — 2 Mo par TMU, pas de
 * pagination, granularité imposée — qui a son propre ticket (E05-S02), et la
 * traduction du combineur RDP vers `grTexCombine` en a un autre (E05-S03). Les
 * écrire ici pour « avoir tout » produirait un allocateur naïf qu'il faudrait
 * jeter.
 *
 * Les modes sans texture, eux, sont complets : couleur de sommet, mélange,
 * profondeur, faces arrière, ciseaux, test alpha, brouillard.
 *
 * ## Les constantes de Glide 2.x sont écrites de mémoire — donc vérifiées
 *
 * Il n'y a pas de `glide.h` sur cette machine : la DLL du pilote 3dfx n'est pas
 * accompagnée de son en-tête. Les valeurs ci-dessous viennent de la spécification
 * Glide 2.4, de mémoire, et **une valeur fausse ne provoque aucune erreur** :
 * Glide ne valide pas ses énumérations, elle programme le registre et l'image
 * sort différente. C'est exactement le genre de faute qu'on attribue ensuite au
 * décodeur de display list.
 *
 * D'où `tools/win95/witnesses/glide_state_probe.c` : chaque mode traduit ici est
 * exercé sur la carte et **relu par `grLfbLock`**. Ce qui est confirmé par la
 * mesure est marqué CONFIRME et daté ; le reste porte SUPPOSE et ne doit pas être
 * cru. Voir `docs/research/win95-glide-etats.md`.
 */
#include "glide.h"
#include "backend.h"
#include "tmu.h"
#include "combiner.h"

#include <string.h>

#define WINAPI __stdcall

typedef unsigned int  FxU32;
typedef unsigned char FxU8;
typedef int           FxBool;

/* --- Les énumérations de Glide 2.x ------------------------------------------ */

/* Combineur de couleur. */
#define GR_COMBINE_FUNCTION_ZERO          0x0
#define GR_COMBINE_FUNCTION_LOCAL         0x1
#define GR_COMBINE_FUNCTION_LOCAL_ALPHA   0x2
#define GR_COMBINE_FUNCTION_SCALE_OTHER   0x3
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4

#define GR_COMBINE_FACTOR_ZERO            0x0
#define GR_COMBINE_FACTOR_LOCAL           0x1
#define GR_COMBINE_FACTOR_ONE             0x8

#define GR_COMBINE_LOCAL_ITERATED         0x0
#define GR_COMBINE_LOCAL_CONSTANT         0x1

#define GR_COMBINE_OTHER_ITERATED         0x0
#define GR_COMBINE_OTHER_TEXTURE          0x1
#define GR_COMBINE_OTHER_CONSTANT         0x2

/* Mélange. */
#define GR_BLEND_ZERO                     0x0
#define GR_BLEND_SRC_ALPHA                0x1
#define GR_BLEND_ONE                      0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA      0x5

/* Comparaisons — partagées par le test de profondeur et le test alpha. */
#define GR_CMP_NEVER                      0x0
#define GR_CMP_LESS                       0x1
#define GR_CMP_GREATER                    0x4
#define GR_CMP_GEQUAL                     0x6
#define GR_CMP_ALWAYS                     0x7

/* Tampon de profondeur. */
#define GR_DEPTHBUFFER_DISABLE            0x0
#define GR_DEPTHBUFFER_ZBUFFER            0x1
#define GR_DEPTHBUFFER_WBUFFER            0x2

/* Faces arrière. */
#define GR_CULL_DISABLE                   0x0
#define GR_CULL_NEGATIVE                  0x1
#define GR_CULL_POSITIVE                  0x2

/* Brouillard. */
#define GR_FOG_DISABLE                    0x0
#define GR_FOG_WITH_ITERATED_ALPHA        0x1

/* --- Textures ---------------------------------------------------------------- *
 *
 * Ces valeurs-ci ne sont pas de memoire : `tmu_probe.c` les a validees sur la
 * carte en comparant `grTexTextureMemRequired` a la taille analytique. Un LOD ou
 * un rapport d'aspect faux aurait donne une taille visiblement fausse.
 * Voir `docs/research/win95-tmu.md`. */
#define GR_LOD_256   0    /* le LOD nomme la plus grande dimension, et decroit */
#define GR_LOD_1     8
#define GR_ASPECT_8x1  0
#define GR_ASPECT_1x1  3
#define GR_ASPECT_1x8  6
#define GR_TEXFMT_ARGB_1555  0x0B
#define GR_TEXFMT_INTENSITY_8 0x03
#define GR_MIPMAPLEVELMASK_BOTH  0x03
#define GR_TMU0  0
#define GR_TMU1  1

/* Le combineur de texture, quand il y en a une. */
#define GR_TEXTURECOMBINE_ZERO   0x0
#define GR_TEXTURECOMBINE_DECAL  0x1

typedef struct {
    int   smallLod;
    int   largeLod;
    int   aspectRatio;
    int   format;
    void *data;
} GrTexInfo;

typedef unsigned int (WINAPI *pfn_tex_addr)(int tmu);
typedef unsigned int (WINAPI *pfn_tex_req)(unsigned evenOdd, GrTexInfo *info);
typedef void         (WINAPI *pfn_tex_dl)(int tmu, unsigned start,
                                          unsigned evenOdd, GrTexInfo *info);
typedef void         (WINAPI *pfn_tex_src)(int tmu, unsigned start,
                                           unsigned evenOdd, GrTexInfo *info);
typedef void         (WINAPI *pfn_tex_comb)(int tmu, FxU32 rgbFn, FxU32 rgbFac,
                                            FxU32 aFn, FxU32 aFac,
                                            FxBool rgbInv, FxBool aInv);
typedef void         (WINAPI *pfn_tex_mode)(int tmu, FxU32 a, FxU32 b);

typedef void (WINAPI *pfn_5)(FxU32, FxU32, FxU32, FxU32, FxBool);
typedef void (WINAPI *pfn_4)(FxU32, FxU32, FxU32, FxU32);
typedef void (WINAPI *pfn_1)(FxU32);

static struct {
    int   ready;
    pfn_5 color_combine;
    pfn_5 alpha_combine;
    pfn_4 blend_function;
    pfn_4 clip_window;
    pfn_1 depth_mode;
    pfn_1 depth_function;
    pfn_1 depth_mask;
    pfn_1 cull_mode;
    pfn_1 alpha_test_function;
    pfn_1 alpha_test_reference;
    pfn_1 fog_mode;
    pfn_1 fog_color;
    pfn_1 constant_color;

    pfn_tex_addr tex_min, tex_max;
    pfn_tex_req  tex_required;
    pfn_tex_dl   tex_download;
    pfn_tex_src  tex_source;
    pfn_tex_comb tex_combine;
    pfn_tex_mode tex_filter, tex_clamp;
} gs;

/* --- Les textures residentes -------------------------------------------------- *
 *
 * Le handle rendu a l'appelant est un indice dans cette table, decale de un :
 * zero signifie l'echec, et c'est le contrat de `backend.h`. La table garde ce
 * qu'il faut pour **relier** la texture au moment du dessin — Glide exige de
 * repasser le meme `GrTexInfo` a `grTexSource` qu'a `grTexDownloadMipMap`. */
typedef struct {
    unsigned long long key;
    unsigned int       address;
    unsigned int       bytes;    /* ce qu'elle occupe, pour detecter le recouvrement */
    GrTexInfo          info;
    unsigned char      tmu;      /* sur quelle unite elle reside */
    unsigned char      live;
} glide_texture;

/* --- Pourquoi un chargement echoue ------------------------------------------
 *
 * `gl_texture_upload` rend zero pour quatre raisons distinctes, et l'appelant ne
 * voit que le zero. Mesure sur la machine : 25 896 chargements pour 21 211
 * refus, un sur deux — sans que rien ne dise lequel des quatre. On a corrige la
 * saturation d'emplacements en la supposant coupable, et le chiffre n'a pas
 * bouge d'une unite. Separer les causes coute quatre entiers. */
enum {
    GL_TEX_ECHEC_PROPORTIONS = 0,  /* dimensions refusees par la carte */
    GL_TEX_ECHEC_TAILLE,           /* grTexCalcMemRequired rend zero */
    GL_TEX_ECHEC_EMPLACEMENT,      /* table de descripteurs pleine */
    GL_TEX_ECHEC_MEMOIRE,          /* allocateur de TMU sature */
    GL_TEX_ECHEC_NB
};
static unsigned long g_tex_echecs[GL_TEX_ECHEC_NB];

unsigned long dkr_glide_backend_upload_failure(int kind)
{
    if (kind < 0 || kind >= GL_TEX_ECHEC_NB) { return 0; }
    return g_tex_echecs[kind];
}

#define GLIDE_MAX_TEXTURES 512
static glide_texture g_tex[GLIDE_MAX_TEXTURES];
static dkr_tmu       g_tmu[2];
static int           g_tmu_count;

/* Le transfert vers la carte, appele par l'allocateur.
   `data` porte le `GrTexInfo` deja rempli : l'allocateur ne connait ni les LOD
   ni les rapports d'aspect, et n'a pas a les connaitre. */
static int glide_download(void *user, int tmu, unsigned int address,
                          const void *data, unsigned int bytes)
{
    (void)user; (void)bytes;
    if (!gs.tex_download || !data) { return 0; }
    gs.tex_download(tmu, address, GR_MIPMAPLEVELMASK_BOTH, (GrTexInfo *)data);
    /* Glide ne rend rien. L'absence de moyen de verifier ici est precisement
       pourquoi `glide_texture_probe.c` va relire le tampon d'image. */
    return 1;
}

/* --- L'état du backend ------------------------------------------------------ */
static struct {
    int              open;
    int              width, height;
    dkr_render_state current;
    int              has_state;
    unsigned long    triangles;
} b;

/* --- Traductions ------------------------------------------------------------ *
 *
 * Chacune est une fonction distincte et courte : c'est ce qui permet au témoin
 * d'exercer un mode à la fois, et donc d'attribuer un écart d'image à une
 * traduction précise plutôt qu'à « l'état ». */

static void bind_texture(dkr_texture_handle handle);

static void apply_combine(dkr_combine_mode m, dkr_texture_handle handle)
{
    if (!gs.color_combine || !gs.alpha_combine) { return; }

    /* **Sans texture liee, on retombe sur la couleur du sommet, et c'est voulu.**
     *
     * Selectionner la texture alors qu'aucune n'est residente ne provoque pas
     * d'erreur : la TMU echantillonne ce qui traine a l'adresse ou elle pointait.
     * L'ecran est alors faux d'une maniere qui *ressemble* a un defaut de
     * combineur, et l'on cherche longtemps du mauvais cote. Un rendu franchement
     * non texture se diagnostique mieux. */
    if (handle == 0 || m == DKR_COMBINE_SHADE) {
        gs.color_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_ITERATED, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_ITERATED, 0);
        return;
    }

    bind_texture(handle);
    if (gs.tex_combine) {
        /* Une seule TMU employee ici : la texture passe telle quelle. Le
           multitexturage sur deux TMU est E05-S04, la traduction fidele du
           combineur RDP est E05-S03 — ce qui suit couvre les modes que le
           decodeur sait deja produire, pas davantage. */
        gs.tex_combine(GR_TMU0, GR_TEXTURECOMBINE_DECAL, GR_COMBINE_FACTOR_ZERO,
                       GR_TEXTURECOMBINE_DECAL, GR_COMBINE_FACTOR_ZERO, 0, 0);
    }

    switch (m) {
    case DKR_COMBINE_TEXTURE:
        /* Le texel seul : la couleur du sommet n'intervient pas. */
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
    case DKR_COMBINE_TEXTURE_SHADE_ALPHA:
        /* Texel module par la couleur du sommet, mais **alpha du texel retenu** :
           c'est ce qui permet a une texture percee de le rester quand le sommet
           porte une transparence propre. */
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
    default:  /* DKR_COMBINE_TEXTURE_SHADE */
        gs.color_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        gs.alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0);
        break;
    }
}

/* Filtrage et enveloppement. E05-S08 les traitera pour de bon ; ici l'on se
   contente de ne pas laisser un etat herite decider a notre place. */
static void apply_texture_modes(const dkr_render_state *st)
{
    /* GR_TEXTUREFILTER_POINT_SAMPLED = 0, BILINEAR = 1.
       GR_TEXTURECLAMP_WRAP = 0, CLAMP = 1 — le miroir n'existe pas sur Voodoo 2
       et se traite au decodage, ce qui est note pour E05-S08. */
    if (gs.tex_filter) {
        const FxU32 f = (st->filter == DKR_FILTER_BILINEAR) ? 1u : 0u;
        gs.tex_filter(GR_TMU0, f, f);
    }
    if (gs.tex_clamp) {
        gs.tex_clamp(GR_TMU0,
                     (st->wrap_s == DKR_WRAP_REPEAT) ? 0u : 1u,
                     (st->wrap_t == DKR_WRAP_REPEAT) ? 0u : 1u);
    }
}

static void apply_blend(dkr_blend_mode m)
{
    if (!gs.blend_function) { return; }
    switch (m) {
    case DKR_BLEND_ALPHA:
        gs.blend_function(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                          GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    case DKR_BLEND_ADDITIVE:
        gs.blend_function(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    default:
        gs.blend_function(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    }
}

/* Quel tampon employer. Un, en W, par défaut — voir `apply_depth`.
 *
 * Exposé pour que E05-S05 puisse **comparer les deux par la mesure** plutôt que
 * de trancher sur la réputation du mode W. Le ticket demande explicitement que
 * ce choix soit justifié par une mesure d'artefacts consignée. */
static int g_depth_en_w = 1;

void dkr_glide_backend_depth_mode(int en_w)
{
    g_depth_en_w = en_w;
}

static void apply_depth(dkr_depth_mode m)
{
    if (!gs.depth_mode || !gs.depth_function || !gs.depth_mask) { return; }
    if (m == DKR_DEPTH_DISABLED) {
        gs.depth_mode(GR_DEPTHBUFFER_DISABLE);
        gs.depth_mask(0);
        return;
    }
    /* **Tampon en w, et non en z.**
     *
     * `dkr_render_vertex` porte déjà `oow = 1/w`, que Glide consomme telle
     * quelle en mode w. Le mode z, lui, lit `ooz` sur [0, 65535] alors que la
     * chaîne produit une profondeur sur [0, 1] : il faudrait remettre chaque
     * sommet à l'échelle, donc les recopier, donc perdre le bénéfice d'avoir
     * calqué la disposition de `GrVertex` champ pour champ.
     *
     * **Le sens de la comparaison ne s'inverse pas**, et c'est contre l'intuition.
     *
     * On raisonne naturellement ainsi : le sommet porte `1/w`, un objet proche a
     * un `1/w` grand, donc le proche gagne avec `GR_CMP_GREATER`. Ce fichier l'a
     * d'abord écrit, et l'écran est resté **entièrement noir** — dans les deux
     * ordres de dessin, ce qui exclut un problème de tri.
     *
     * Le raisonnement oublie que Glide ne stocke pas `1/w` : elle range une
     * valeur w encodée qui **croît avec la distance**, et `grBufferClear` efface
     * à `GR_WDEPTHVALUE_FARTHEST` = 0xFFFF. Rien ne pouvant dépasser ce maximum,
     * `GR_CMP_GREATER` rejette la totalité de la scène. La comparaison est donc
     * `LESS`, comme en tampon z.
     *
     * Le symptôme méritait d'être écrit : un écran noir se diagnostique d'abord
     * comme un défaut de géométrie ou de fenêtre, et l'on cherche longtemps avant
     * de soupçonner un tampon de profondeur qui fonctionne parfaitement.
     * Confirmé par relecture le 14 août 2026 ; voir `win95-glide-etats.md`. */
    gs.depth_mode(g_depth_en_w ? GR_DEPTHBUFFER_WBUFFER : GR_DEPTHBUFFER_ZBUFFER);
    gs.depth_function(GR_CMP_LESS);
    gs.depth_mask(m == DKR_DEPTH_TEST_AND_WRITE ? 1 : 0);
}

static void apply_cull(dkr_cull_mode m)
{
    if (!gs.cull_mode) { return; }
    /* La chaîne élimine déjà les faces arrière elle-même (`dkr_cull_accept`),
       parce que le rastériseur de référence doit éliminer les mêmes triangles
       que la carte. Programmer *aussi* la carte serait redondant et risquerait
       d'éliminer deux fois selon des conventions opposées — donc de tout vider.
       On la désactive explicitement plutôt que de la laisser dans un état hérité. */
    (void)m;
    gs.cull_mode(GR_CULL_DISABLE);
}

static void apply_alpha_test(unsigned char enabled, unsigned char reference)
{
    if (!gs.alpha_test_function || !gs.alpha_test_reference) { return; }
    if (!enabled) {
        gs.alpha_test_function(GR_CMP_ALWAYS);
        return;
    }
    gs.alpha_test_reference(reference);
    gs.alpha_test_function(GR_CMP_GEQUAL);
}

static void apply_fog(unsigned char enabled, unsigned int color)
{
    if (!gs.fog_mode) { return; }
    if (!enabled) {
        gs.fog_mode(GR_FOG_DISABLE);
        return;
    }
    if (gs.fog_color) { gs.fog_color(color & 0x00FFFFFFu); }
    /* Alpha itérée plutôt que table : DKR calcule son brouillard par sommet, et
       la table de Glide imposerait une courbe qui n'est pas la sienne. */
    gs.fog_mode(GR_FOG_WITH_ITERATED_ALPHA);
}

/* --- L'interface ------------------------------------------------------------ */

/* Le nombre de TMU vient de la detection, pas d'une constante : l'ADR 0002
   impose deux TMU sans interdire d'en trouver une seule, auquel cas le repli
   multipasse de E05-S04 s'appliquera. */
static int hw_tmu_count(void)
{
    dkr_glide_hardware hw;
    if (dkr_glide_detect(&hw) != DKR_GLIDE_OK) { return 0; }
    return hw.tmu_count;
}

static int gl_open(void *self, int width, int height)
{
    dkr_glide_context ctx;
    dkr_glide_resolution wanted = DKR_GLIDE_RES_640x480;

    (void)self;
    if (width <= 320 && height <= 240)      { wanted = DKR_GLIDE_RES_320x240; }
    else if (width <= 400 && height <= 300) { wanted = DKR_GLIDE_RES_400x300; }
    else if (width <= 512 && height <= 384) { wanted = DKR_GLIDE_RES_512x384; }

    memset(&b, 0, sizeof(b));
    if (dkr_glide_open(wanted, &ctx) != DKR_GLIDE_OK) {
        return 0;
    }
    b.open   = 1;
    b.width  = ctx.width;
    b.height = ctx.height;

    if (!gs.ready) {
        gs.color_combine        = (pfn_5)dkr_glide_symbol("_grColorCombine@20");
        gs.alpha_combine        = (pfn_5)dkr_glide_symbol("_grAlphaCombine@20");
        gs.blend_function       = (pfn_4)dkr_glide_symbol("_grAlphaBlendFunction@16");
        gs.clip_window          = (pfn_4)dkr_glide_symbol("_grClipWindow@16");
        gs.depth_mode           = (pfn_1)dkr_glide_symbol("_grDepthBufferMode@4");
        gs.depth_function       = (pfn_1)dkr_glide_symbol("_grDepthBufferFunction@4");
        gs.depth_mask           = (pfn_1)dkr_glide_symbol("_grDepthMask@4");
        gs.cull_mode            = (pfn_1)dkr_glide_symbol("_grCullMode@4");
        gs.alpha_test_function  = (pfn_1)dkr_glide_symbol("_grAlphaTestFunction@4");
        gs.alpha_test_reference = (pfn_1)dkr_glide_symbol("_grAlphaTestReferenceValue@4");
        gs.fog_mode             = (pfn_1)dkr_glide_symbol("_grFogMode@4");
        gs.fog_color            = (pfn_1)dkr_glide_symbol("_grFogColorValue@4");
        gs.constant_color       = (pfn_1)dkr_glide_symbol("_grConstantColorValue@4");
        gs.tex_min      = (pfn_tex_addr)dkr_glide_symbol("_grTexMinAddress@4");
        gs.tex_max      = (pfn_tex_addr)dkr_glide_symbol("_grTexMaxAddress@4");
        gs.tex_required = (pfn_tex_req) dkr_glide_symbol("_grTexTextureMemRequired@8");
        gs.tex_download = (pfn_tex_dl)  dkr_glide_symbol("_grTexDownloadMipMap@16");
        gs.tex_source   = (pfn_tex_src) dkr_glide_symbol("_grTexSource@16");
        gs.tex_combine  = (pfn_tex_comb)dkr_glide_symbol("_grTexCombine@28");
        gs.tex_filter   = (pfn_tex_mode)dkr_glide_symbol("_grTexFilterMode@12");
        gs.tex_clamp    = (pfn_tex_mode)dkr_glide_symbol("_grTexClampMode@12");
        gs.ready = 1;
    }

    /* Les bornes de la TMU sont **demandees**, jamais supposees. La mesure a
       montre `grTexMinAddress` a zero, ce qui interdit de faire de zero un
       sentinelle — d'ou `DKR_TMU_NONE` a 0xFFFFFFFF. */
    memset(g_tex, 0, sizeof(g_tex));
    g_tmu_count = 0;
    if (gs.tex_min && gs.tex_max) {
        int i;
        const int n = (hw_tmu_count() > 2) ? 2 : hw_tmu_count();
        for (i = 0; i < n; i++) {
            dkr_tmu_init(&g_tmu[i], i, gs.tex_min(i), gs.tex_max(i),
                         glide_download, 0);
        }
        g_tmu_count = n;
    }
    return 1;
}

static void gl_close(void *self)
{
    (void)self;
    dkr_glide_shutdown();
    b.open = 0;
}

static void gl_begin_frame(void *self, unsigned clear_argb)
{
    (void)self;
    b.triangles = 0;
    {
        /* L'allocateur doit savoir qu'une image commence : c'est ce qui leve les
           protections de l'image precedente et remet les compteurs par image a
           zero. Sans cela, plus rien ne serait jamais evincable. */
        int i;
        for (i = 0; i < g_tmu_count; i++) { dkr_tmu_begin_frame(&g_tmu[i]); }
    }

    /* --- Glide n'efface la profondeur que si l'écriture y est autorisée ------ *
     *
     * `grBufferClear` prend une valeur de profondeur, mais elle n'est écrite que
     * si `grDepthMask` est ouvert. L'état laissé par la fin de l'image
     * précédente le referme dès que le dernier triangle était en
     * `DKR_DEPTH_DISABLED` ou en test-sans-écriture — c'est-à-dire presque
     * toujours, l'interface se dessinant par-dessus la scène.
     *
     * Tant que le test de profondeur était inactif, cela ne se voyait pas : rien
     * ne lisait le tampon. Dès qu'il s'est activé, le tampon a gardé les
     * profondeurs de la première image pour toutes les suivantes, et **l'écran
     * est devenu noir** — tout échouait au test contre une scène figée.
     *
     * Le symptôme est le même que celui d'un sens de comparaison inversé, déjà
     * consigné dans `win95-glide-etats.md`, et c'est ce qui rend ce défaut
     * coûteux : on va vérifier la comparaison, on la trouve juste, et l'on
     * cherche ailleurs que dans l'effacement.
     *
     * On ouvre donc le masque le temps de l'effacement. `has_state` est invalidé
     * pour que le prochain `set_state` repose l'état réel plutôt que de le
     * croire déjà en place — sans quoi la comparaison de blocs sauterait la
     * remise en ordre. */
    if (gs.depth_mask) {
        gs.depth_mask(1);
        b.has_state = 0;
    }
    dkr_glide_clear(clear_argb);
}

static void gl_present(void *self)
{
    (void)self;
    dkr_glide_swap();
}

static void gl_set_state(void *self, const dkr_render_state *state)
{
    (void)self;
    if (!state) { return; }
    /* Le bloc est `memcmp`-able par construction — c'est pour cela qu'il porte un
       champ de bourrage explicite. Sauter un état identique évite une rafale
       d'écritures de registres par triangle, ce qui coûte cher sur un bus PCI de
       1998. */
    if (b.has_state && memcmp(&b.current, state, sizeof(*state)) == 0) {
        return;
    }
    b.current   = *state;
    b.has_state = 1;

    apply_combine(state->combine, state->texture);
    apply_texture_modes(state);
    apply_blend(state->blend);
    apply_depth(state->depth);
    apply_cull(state->cull);
    apply_alpha_test(state->alpha_test, state->alpha_reference);
    apply_fog(state->fog_enabled, state->fog_color);
}

static void gl_set_scissor(void *self, int x0, int y0, int x1, int y1)
{
    (void)self;
    if (!gs.clip_window) { return; }
    /* Glide refuse une fenêtre qui déborde du tampon, et le refus est silencieux :
       la fenêtre précédente reste, et le partage d'écran à deux joueurs se met à
       dessiner l'un sur l'autre. On borne donc ici. */
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > b.width)  { x1 = b.width; }
    if (y1 > b.height) { y1 = b.height; }
    if (x1 <= x0 || y1 <= y0) { return; }
    gs.clip_window((FxU32)x0, (FxU32)y0, (FxU32)x1, (FxU32)y1);
}

static void gl_draw_triangles(void *self, const dkr_render_vertex *vertices,
                              int count)
{
    int i;
    (void)self;
    if (!vertices || count <= 0) { return; }
    /* `dkr_render_vertex` a la disposition de `GrVertex`, champ pour champ —
       `backend_layout_check.c` le vérifie à la compilation. Le passage se fait
       donc sans conversion ni copie, ce qui était tout l'objet de ce calque. */
    for (i = 0; i + 2 < count * 3; i += 3) {
        dkr_glide_draw_raw(&vertices[i], &vertices[i + 1], &vertices[i + 2]);
        b.triangles++;
    }
}

static void gl_fill_rect(void *self, int x0, int y0, int x1, int y1,
                         unsigned argb)
{
    /* Deux triangles plutôt que `grBufferClear` sur une fenêtre de ciseaux :
       l'effacement ignore le mélange et le test alpha, alors que DKR emploie ces
       rectangles pour les fondus au noir, qui sont translucides. */
    dkr_render_vertex v[6];
    const float r = (float)((argb >> 16) & 0xFF);
    const float g = (float)((argb >> 8) & 0xFF);
    const float bl = (float)(argb & 0xFF);
    const float a = (float)((argb >> 24) & 0xFF);
    const float xs[6] = { (float)x0, (float)x1, (float)x1,
                          (float)x0, (float)x1, (float)x0 };
    const float ys[6] = { (float)y0, (float)y0, (float)y1,
                          (float)y0, (float)y1, (float)y1 };
    int i;

    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = r; v[i].g = g; v[i].b = bl; v[i].a = a;
        /* Au premier plan et sans profondeur : un rectangle d'interface ne
           participe pas au tri. */
        v[i].oow = 1.0f;
        v[i].z = 0.0f;
        v[i].ooz = 0.0f;
    }
    gl_draw_triangles(self, v, 2);
}

/* Traduit (largeur, hauteur) en couple (LOD, rapport d'aspect).
 *
 * Glide ne connait pas les dimensions : elle connait la plus grande, et le
 * rapport. Rend zero si la texture n'est pas exprimable — dimensions qui ne sont
 * pas des puissances de deux, ou rapport au-dela de 8:1. **Refuser est le bon
 * comportement** : approcher donnerait une texture lue de travers, ce qui
 * ressemble a un defaut de coordonnees et se diagnostique tres mal. */
static int lod_and_aspect(int w, int h, int *lod, int *aspect)
{
    int big = (w > h) ? w : h;
    int small = (w > h) ? h : w;
    int ratio = 0, k = 0;

    if (w <= 0 || h <= 0 || big > 256) { return 0; }
    if ((w & (w - 1)) != 0 || (h & (h - 1)) != 0) { return 0; }
    if (big / small > 8) { return 0; }

    for (k = 0; (256 >> k) != big; k++) {
        if (k > 8) { return 0; }
    }
    *lod = k;

    /* GR_ASPECT_1x1 vaut 3 ; les rapports larges descendent vers 0, les hauts
       montent vers 6. */
    ratio = big / small;
    if (w >= h) {
        *aspect = (ratio == 1) ? GR_ASPECT_1x1
                : (ratio == 2) ? 2 : (ratio == 4) ? 1 : GR_ASPECT_8x1;
    } else {
        *aspect = (ratio == 2) ? 4 : (ratio == 4) ? 5 : GR_ASPECT_1x8;
    }
    return 1;
}

static dkr_texture_handle gl_texture_upload(void *self,
                                            const dkr_texture_desc *desc)
{
    int lod = 0, aspect = 0, i, slot = -1;
    unsigned int bytes, address;
    GrTexInfo info;

    (void)self;
    if (!desc || !desc->pixels || g_tmu_count == 0 || !gs.tex_required) {
        return 0;
    }
    if (!lod_and_aspect(desc->width, desc->height, &lod, &aspect)) {
        g_tex_echecs[GL_TEX_ECHEC_PROPORTIONS]++;
        return 0;
    }

    memset(&info, 0, sizeof(info));
    info.smallLod    = lod;
    info.largeLod    = lod;        /* pas de mipmap : E05-S08 */
    info.aspectRatio = aspect;
    info.format      = (desc->format == DKR_TEXFMT_INTENSITY8)
                       ? GR_TEXFMT_INTENSITY_8 : GR_TEXFMT_ARGB_1555;
    info.data        = (void *)desc->pixels;

    /* **La taille vient de la carte, pas d'un calcul.** La mesure a montre un
       cas d'arrondi — une texture 1x1 coute 8 octets pour 2 utiles — et empiler
       d'apres un calcul ferait se recouvrir deux textures. Le symptome ne serait
       pas une erreur mais un decor portant le motif d'un autre, a un endroit qui
       depend de l'ordre de chargement. */
    bytes = gs.tex_required(GR_MIPMAPLEVELMASK_BOTH, &info);
    if (bytes == 0u) { g_tex_echecs[GL_TEX_ECHEC_TAILLE]++; return 0; }

    for (i = 0; i < GLIDE_MAX_TEXTURES; i++) {
        if (g_tex[i].live && g_tex[i].key == desc->key) { slot = i; break; }
        if (!g_tex[i].live && slot < 0) { slot = i; }
    }
    if (slot < 0) { g_tex_echecs[GL_TEX_ECHEC_EMPLACEMENT]++; return 0; }

    /* **On passe par l'allocateur meme quand la texture est deja connue.**
     *
     * Rendre directement le handle serait plus rapide et serait un piege : la
     * date d'usage de la texture n'avancerait jamais, l'allocateur la croirait
     * abandonnee, et il evincerait au moindre recemment utilise precisement ce
     * que le jeu emploie a chaque image. Le symptome serait un retelechargement
     * permanent — donc des a-coups — sur les textures les plus vues.
     *
     * `dkr_tmu_acquire` distingue seul le succes du defaut : c'est lui qui tient
     * les compteurs, et il doit les tenir sur la totalite des demandes. */
    /* **Deux espaces, pas un.** Chaque TMU a sa memoire propre, et une texture
       n'est echantillonnable que depuis l'unite ou elle reside. Une meme cle
       peut donc legitimement etre residente deux fois — mais seulement si les
       deux unites l'echantillonnent, et la cle du cache inclut la TMU pour que
       ce ne soit jamais accidentel. */
    {
        int cible = desc->tmu;
        if (cible < 0 || cible >= g_tmu_count) { cible = 0; }
        g_tex[slot].tmu = (unsigned char)cible;
        address = dkr_tmu_acquire(&g_tmu[cible], desc->key, &info, bytes);
    }
    if (address == DKR_TMU_NONE) {
        g_tex_echecs[GL_TEX_ECHEC_MEMOIRE]++;
        g_tex[slot].live = 0;
        return 0;
    }

    /* --- Faire suivre la table les evictions de l'allocateur ---------------- *
     *
     * La table de descripteurs et l'allocateur de TMU avaient des vies
     * independantes, et c'etait un defaut a deux faces :
     *
     *   - **Une texture evincee gardait son emplacement `live`.** Les 512
     *     emplacements se remplissaient en une douzaine d'images — DKR en charge
     *     une quarantaine par image — puis `slot < 0` refusait tout. Mesure sur
     *     la machine : 25 853 chargements pour **21 195 refus**.
     *
     *   - **Pire que le refus** : tant que l'emplacement vivait, il designait de
     *     la memoire que l'allocateur avait reattribuee. C'est exactement le
     *     symptome que le commentaire de `tex_required` redoute plus haut — un
     *     decor portant le motif d'un autre, a un endroit qui depend de l'ordre
     *     de chargement.
     *
     * On invalide donc tout emplacement de la meme unite dont la plage recouvre
     * celle qu'on vient d'obtenir. L'allocateur reste seul juge de ce qui reside
     * ou ; la table se contente de le suivre, ce qui est la seule facon qu'elle
     * ne mente pas. Le balayage coute 512 comparaisons par chargement, soit
     * quelques dizaines de milliers par image — negligeable devant une seule
     * conversion de texture. */
    {
        int j;
        for (j = 0; j < GLIDE_MAX_TEXTURES; j++) {
            if (j == slot || !g_tex[j].live) { continue; }
            if (g_tex[j].tmu != g_tex[slot].tmu) { continue; }
            if (g_tex[j].address < address + bytes &&
                address < g_tex[j].address + g_tex[j].bytes) {
                g_tex[j].live = 0;
            }
        }
    }

    g_tex[slot].key     = desc->key;
    g_tex[slot].address = address;
    g_tex[slot].bytes   = bytes;
    g_tex[slot].info    = info;
    g_tex[slot].info.data = 0;   /* les pixels ne nous appartiennent pas */
    g_tex[slot].live    = 1;
    return (dkr_texture_handle)(slot + 1);
}

static void gl_texture_release(void *self, dkr_texture_handle handle)
{
    (void)self;
    if (handle == 0 || handle > GLIDE_MAX_TEXTURES) { return; }
    /* On oublie le handle sans liberer le bloc : c'est l'allocateur qui decide
       quand evincer, au moindre recemment utilise, et il le fera mieux que
       l'appelant. Liberer ici jetterait une texture que l'image suivante
       redemanderait — le pire regime, ou l'on paie le bus pour rien. */
    g_tex[handle - 1].live = 0;
}

/* Lie la texture courante avant le dessin. Sans `grTexSource`, la TMU echantillonne
   ce qui traine a l'adresse ou elle pointait — donc une autre texture. */
static void bind_texture(dkr_texture_handle handle)
{
    glide_texture *tx;
    if (handle == 0 || handle > GLIDE_MAX_TEXTURES || !gs.tex_source) { return; }
    tx = &g_tex[handle - 1];
    if (!tx->live) { return; }
    /* Lier sur l'unite ou la texture reside, et non sur la TMU 0 par defaut :
       lier une adresse de la TMU 1 sur la TMU 0 ne provoque aucune erreur, la
       TMU 0 echantillonnant simplement ce qui traine a cette adresse chez elle.
       Le decor porterait alors le motif d'un autre. */
    gs.tex_source(tx->tmu ? GR_TMU1 : GR_TMU0, tx->address,
                  GR_MIPMAPLEVELMASK_BOTH, &tx->info);
}

void dkr_render_backend_glide(dkr_render_backend *out)
{
    if (!out) { return; }
    memset(out, 0, sizeof(*out));
    out->self            = &b;
    out->name            = "3dfx Glide";
    out->open            = gl_open;
    out->close           = gl_close;
    out->begin_frame     = gl_begin_frame;
    out->present         = gl_present;
    out->set_state       = gl_set_state;
    out->set_scissor     = gl_set_scissor;
    out->draw_triangles  = gl_draw_triangles;
    out->fill_rect       = gl_fill_rect;
    out->texture_upload  = gl_texture_upload;
    out->texture_release = gl_texture_release;
}

unsigned long dkr_glide_backend_triangle_count(void)
{
    return b.triangles;
}

/* Applique un reglage de la table de E05-S03, tel quel.
 *
 * Point d'entree direct, employe par le harnais de mesure et destine au moteur.
 * Il court-circuite `apply_combine`, dont les quatre modes ne sont qu'un
 * raccourci : la table couvre vingt-neuf configurations, et c'est elle qui doit
 * decider, pas une enumeration qui la resume.
 *
 * `constant_argb` charge l'unique registre constant de Glide. **Quel registre du
 * RDP y placer est une decision de la table** — `DKR_CONST_PRIMITIVE` ou
 * `DKR_CONST_ENVIRONMENT` — et la seconde constante, quand elle est necessaire,
 * voyage dans l'alpha du sommet. */
void dkr_glide_backend_set_recipe(const dkr_cc_setup *r, unsigned constant_argb)
{
    if (!r) { return; }
    if (gs.constant_color) { gs.constant_color(constant_argb); }
    if (gs.color_combine) {
        gs.color_combine(r->cc_function, r->cc_factor, r->cc_local, r->cc_other, 0);
    }
    if (gs.alpha_combine) {
        gs.alpha_combine(r->ac_function, r->ac_factor, r->ac_local, r->ac_other, 0);
    }
    if (gs.tex_combine && r->uses_texture) {
        gs.tex_combine(GR_TMU0, r->tc_function, r->tc_factor,
                       r->tc_function, r->tc_factor, 0, 0);
    }
}

void dkr_glide_backend_bind(dkr_texture_handle handle)
{
    bind_texture(handle);
}

/* Chaîne les deux unités : la TMU 1 échantillonne, sa sortie devient l'entrée
 * « other » de la TMU 0, dont la sortie alimente le combineur de couleurs.
 *
 * **L'ordre des appels n'est pas indifférent.** Glide veut la TMU la plus haute
 * d'abord : c'est elle qui commence la chaîne, et la programmer après la TMU 0
 * laisse cette dernière chaînée sur une unité pas encore configurée. L'effet
 * n'est pas une erreur mais une image construite à partir de l'état précédent —
 * donc juste tant qu'on ne change rien, et fausse au premier changement d'état,
 * ce qui est le pire moment pour s'en apercevoir.
 *
 * `fonction` et `facteur` sont passés plutôt que codés : leurs valeurs
 * d'énumération sont mesurées par `multitex_probe.c`, et ce projet a déjà payé
 * deux fois pour avoir supposé de telles valeurs. */
/* **Le repli à une TMU doit être éprouvable sur une carte qui en a deux.**
 *
 * C'est le risque que le ticket nomme : « facile à écrire et facile à ne jamais
 * tester, faute de matériel à une seule TMU sous la main ». Sans ce drapeau, le
 * chemin multipasse ne serait vérifié qu'après une remontée d'utilisateur — donc
 * sur la machine de quelqu'un d'autre, et sans trace. */
static int g_force_une_tmu;

void dkr_glide_backend_force_single_tmu(int force)
{
    g_force_une_tmu = force;
}

int dkr_glide_backend_tmu_count(void)
{
    return g_force_une_tmu ? 1 : g_tmu_count;
}

void dkr_glide_backend_chain(dkr_texture_handle tmu0, dkr_texture_handle tmu1,
                             unsigned char fonction, unsigned char facteur)
{
    if (dkr_glide_backend_tmu_count() < 2 || !gs.tex_combine) { return; }

    bind_texture(tmu1);
    /* La TMU 1 se contente d'échantillonner : elle n'a pas d'unité en amont. */
    gs.tex_combine(GR_TMU1, GR_TEXTURECOMBINE_DECAL, 0,
                             GR_TEXTURECOMBINE_DECAL, 0, 0, 0);
    bind_texture(tmu0);
    gs.tex_combine(GR_TMU0, fonction, facteur, fonction, facteur, 0, 0);
}

const dkr_tmu *dkr_glide_backend_tmu(int index)
{
    if (index < 0 || index >= g_tmu_count) { return 0; }
    return &g_tmu[index];
}
