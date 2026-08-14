/* E04-S01 — l'interface que le décodeur F3DDKR pilote.
 *
 * Deux implémentations la consomment : Glide (E05) et le rastériseur logiciel de
 * référence (E04-S08), qui sert d'oracle de comparaison. Elle ne contient donc
 * aucun type de RT64, de SDL2 ni d'ImGui.
 *
 * ## Ce qu'elle est, et pourquoi elle est basse
 *
 * `ultramodern::renderer::RendererContext` existe déjà, mais reçoit une tâche RSP
 * brute et laisse tout faire à l'implémentation. C'est ainsi que le décodeur
 * actuel s'est soudé à RT64 : `f3ddkr_rt64.cpp` manipule `RT64::State`,
 * `RT64::DisplayList`, et enregistre des charges de travail RT64.
 *
 * Celle-ci se place un cran plus bas. Le décodeur transforme, éclaire et découpe
 * (E04-S03, E04-S05) ; le backend reçoit des primitives **déjà projetées en
 * coordonnées écran**. Ce n'est pas un choix d'élégance : aucune carte 3dfx ne
 * transforme, et une interface qui promettrait des sommets en espace objet
 * obligerait le backend Glide à refaire côté processeur ce que le décodeur vient
 * déjà de faire.
 *
 * ## La règle qui a guidé chaque décision
 *
 * L'interface **épouse la carte** au lieu de l'abstraire. Une interface trop
 * générique se paie deux fois : à l'écriture du backend Glide, qui doit émuler
 * ce que la carte ne fait pas, et à l'exécution, en surcoût par primitive sur un
 * processeur à 400 MHz.
 *
 * Chaque élément porte donc l'une de ces deux annotations :
 *
 *     NATIF     Glide sait le faire, et l'appel est nommé
 *     A EMULER  Glide ne sait pas, et le contournement est décrit
 *
 * ## Ce qui a été relevé dans `f3ddkr_rt64.cpp`, et qui décide de la forme
 *
 * Les gestionnaires du décodeur sont : `Matrix`, `Vertex`, `Triangle`,
 * `FillRect`, `SetTextureImage`, `LoadBlock`, `TextureOffset`, `MoveWord`, plus
 * le contrôle de flux des listes d'affichage — qui ne concerne pas le rendu.
 *
 * Trois observations comptent :
 *
 * 1. **Le sommet DKR ne porte pas de coordonnées de texture.** Ses dix octets
 *    sont `x, y, z` en entiers 16 bits signés et `r, g, b, a` en octets. Les
 *    coordonnées `s, t` arrivent **par coin, au moment du triangle** —
 *    `rsp.modifyVertex(vertices[corner], G_MWO_POINT_ST, texcoord)` modifie le
 *    sommet en cache juste avant de dessiner.
 *
 *    Conséquence directe : **une interface à sommets indexés serait fausse ici.**
 *    Le même sommet en cache est dessiné avec des `s, t` différents selon le
 *    triangle ; un backend indexé devrait donc tenir un cache modifiable et le
 *    réécrire à chaque triangle. Comme `grDrawTriangle` prend de toute façon
 *    trois sommets complets, l'interface les prend aussi, et l'expansion se fait
 *    côté décodeur — là où l'information est déjà en main.
 *
 * 2. **La culling est par triangle**, décidée par un bit de l'entête (0x40) et
 *    par le signe de l'échelle en x de la fenêtre d'affichage.
 *
 * 3. **Le décodeur n'a besoin que d'une seule primitive de dessin**, le triangle,
 *    plus le rectangle plein de `FillRect`. Il n'y a ni ligne, ni point, ni
 *    éventail.
 */
#ifndef DKR_RENDER_BACKEND_H
#define DKR_RENDER_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Le sommet ------------------------------------------------------------- *
 *
 * **La disposition est celle de `GrVertex` de Glide 2.x, champ pour champ, et
 * c'est délibéré.** Le backend Glide passe l'adresse du sommet directement à
 * `grDrawTriangle` : aucune conversion, aucune recopie. Sur un Pentium II, une
 * conversion de format par sommet est un coût réel, et le rendu de DKR en émet
 * des dizaines de milliers par image.
 *
 * L'ordre n'est pas intuitif — `ooz` et `a` s'intercalent entre les couleurs et
 * `oow` — et s'en écarter ne produit **aucune erreur** : Glide lit les flottants
 * aux mauvais décalages et rend des couleurs permutées. Mesuré en E09-S01, où un
 * sommet rouge sortait vert. Ne pas réordonner ces champs.
 *
 * Le rastériseur logiciel de E04-S08 lit les mêmes champs par leur nom ; la
 * disposition ne lui coûte rien.
 */
typedef struct {
    float x, y;            /* coordonnées écran, en pixels, origine en haut à gauche */
    float z;               /* ignoré par Glide ; le rastériseur logiciel s'en sert */
    float r, g, b;         /* 0..255, non 0..1 — c'est l'échelle de Glide */
    float ooz;             /* 65535/z, valeur du tampon de profondeur */
    float a;               /* 0..255 */
    float oow;             /* 1/w, correction de perspective */
    float tmu[3][4];       /* par unité de texture : sow, tow, oow, réservé */
} dkr_render_vertex;

/* Indices dans `tmu[n]`, nommés pour que les sites d'appel se lisent. */
#define DKR_TMU_SOW 0      /* s/w */
#define DKR_TMU_TOW 1      /* t/w */
#define DKR_TMU_OOW 2      /* 1/w */

/* --- L'état de rendu ------------------------------------------------------- *
 *
 * Un **bloc de valeurs**, et non une série d'appels. Le backend compare au bloc
 * courant et n'émet que les différences : c'est ce qui rend le suivi d'état bon
 * marché, et c'est le seul moyen tenable quand chaque changement d'état Glide
 * coûte un appel de fonction à travers une DLL.
 *
 * Le bloc est comparable par `memcmp` — d'où l'absence de remplissage implicite
 * et de pointeurs autres que le handle de texture.
 */

/* Le combineur. **A EMULER, partiellement.**
 *
 * Le combineur du RDP prend deux étages à quatre entrées ; celui de Glide 2.x
 * est fixe et n'offre qu'un jeu de modes. Les cas que DKR emploie réellement
 * seront relevés en E04-S06 et traduits en E05-S03 ; ceux qui n'ont pas
 * d'équivalent demanderont une seconde passe. Cette énumération ne liste donc
 * que des **intentions**, pas des modes RDP : c'est au traducteur de les
 * produire, et au backend de les honorer.
 *
 * Appels Glide : `grColorCombine`, `grAlphaCombine`, `grTexCombine`. */
typedef enum {
    DKR_COMBINE_SHADE = 0,        /* couleur du sommet seule */
    DKR_COMBINE_TEXTURE,          /* texel seul */
    DKR_COMBINE_TEXTURE_SHADE,    /* texel modulé par la couleur du sommet */
    DKR_COMBINE_TEXTURE_SHADE_ALPHA, /* idem, alpha du texel retenu */
    DKR_COMBINE_COUNT
} dkr_combine_mode;

/* Le mélange. **NATIF** — `grAlphaBlendFunction`. */
typedef enum {
    DKR_BLEND_OPAQUE = 0,         /* pas de mélange */
    DKR_BLEND_ALPHA,              /* src.a, 1-src.a */
    DKR_BLEND_ADDITIVE,           /* un, un */
    DKR_BLEND_COUNT
} dkr_blend_mode;

/* Le test de profondeur. **NATIF** — `grDepthBufferFunction`, `grDepthMask`.
 *
 * Une réserve mesurée par l'ADR 0002 : le tampon de profondeur occupe la même
 * mémoire d'image que les tampons de couleur, et c'est ce qui a écarté le triple
 * buffering. Le désactiver libère de la bande passante, pas de la mémoire. */
typedef enum {
    DKR_DEPTH_DISABLED = 0,
    DKR_DEPTH_TEST_ONLY,          /* teste sans écrire */
    DKR_DEPTH_TEST_AND_WRITE,
    DKR_DEPTH_COUNT
} dkr_depth_mode;

/* Filtrage et enveloppement. **NATIF** — `grTexFilterMode`, `grTexClampMode`. */
typedef enum { DKR_FILTER_POINT = 0, DKR_FILTER_BILINEAR } dkr_filter_mode;
typedef enum { DKR_WRAP_REPEAT = 0, DKR_WRAP_CLAMP, DKR_WRAP_MIRROR } dkr_wrap_mode;

/* La culling. **NATIF** — `grCullMode`. Par triangle dans le décodeur, mais
 * portée par l'état : Glide n'a pas de culling par primitive, et le décodeur
 * regroupe déjà ses triangles par sens. */
typedef enum { DKR_CULL_NONE = 0, DKR_CULL_FRONT, DKR_CULL_BACK } dkr_cull_mode;

/* Handle de texture rendu par le backend. Zéro signifie « aucune texture ». */
typedef unsigned int dkr_texture_handle;

typedef struct {
    dkr_combine_mode   combine;
    dkr_blend_mode     blend;
    dkr_depth_mode     depth;
    dkr_cull_mode      cull;
    dkr_filter_mode    filter;
    dkr_wrap_mode      wrap_s;
    dkr_wrap_mode      wrap_t;

    /* Test alpha. **NATIF** — `grAlphaTestFunction`, `grAlphaTestReferenceValue`.
       `alpha_reference` ne compte que si `alpha_test` est non nul. */
    unsigned char      alpha_test;
    unsigned char      alpha_reference;   /* 0..255 */

    /* Brouillard. **NATIF** — `grFogMode`, `grFogColorValue`, `grFogTable`.
       Une des rares choses que Glide fait mieux que la concurrence de l'époque,
       et DKR en fait un usage constant. */
    unsigned char      fog_enabled;
    unsigned char      pad_;              /* explicite : le bloc est memcmp-able */
    unsigned int       fog_color;         /* 0x00RRGGBB */

    dkr_texture_handle texture;
} dkr_render_state;

/* --- Les textures ---------------------------------------------------------- *
 *
 * Un cache à handles. Le décodeur fournit une texture **déjà décodée** en un
 * format que la carte accepte, plus une clé qui l'identifie ; le backend rend un
 * handle et gère seul son placement en mémoire de texture (E05-S02).
 *
 * La clé est l'adresse RDRAM combinée au format et aux dimensions, calculée par
 * le décodeur. Le backend ne l'interprète pas : il compare, c'est tout. C'est ce
 * qui lui permet de répondre « je l'ai déjà » sans décoder à nouveau — et le
 * décodage N64 (E04-S07) est cher.
 *
 * **NATIF** — `grTexDownloadMipMap`, `grTexSource`. Le placement dans la TMU est
 * en revanche entièrement à la charge du backend : Glide expose une mémoire
 * plate et une adresse, sans allocateur. */
typedef enum {
    DKR_TEXFMT_RGBA5551 = 0,      /* le format naturel de la Voodoo */
    DKR_TEXFMT_RGBA8888,          /* à convertir : la Voodoo 2 ne le prend pas */
    DKR_TEXFMT_INTENSITY8,
    DKR_TEXFMT_COUNT
} dkr_texture_format;

typedef struct {
    unsigned long long key;       /* opaque au backend ; il compare, il n'interprète pas */
    dkr_texture_format format;
    int                width, height;
    const void        *pixels;
    size_t             size_bytes;
} dkr_texture_desc;

/* --- L'interface ----------------------------------------------------------- *
 *
 * Une table de pointeurs de fonctions plutôt qu'un ensemble de symboles : les
 * deux implémentations doivent coexister dans le même binaire, le rastériseur
 * logiciel servant d'oracle au backend Glide (E04-S08). Des symboles globaux
 * l'interdiraient.
 *
 * Toute fonction peut être nulle dans une implémentation partielle ; l'appelant
 * doit vérifier. C'est ce qui permet à une implémentation vide de compiler et de
 * se lier, comme le demande E04-S01.
 */
typedef struct dkr_render_backend {
    const char *name;             /* « glide », « software » — pour les journaux */

    /* Cycle de vie. `open` rend zéro en cas d'échec ; le message reste au
       backend, qui seul sait pourquoi. */
    int  (*open)(void *self, int width, int height);
    void (*close)(void *self);

    /* Cycle d'image. **NATIF** — `grBufferClear`, `grBufferSwap`.
       `begin_frame` efface ; `present` échange les tampons. */
    void (*begin_frame)(void *self, unsigned clear_argb);
    void (*present)(void *self);

    /* État. Le backend compare au bloc courant et n'émet que les différences. */
    void (*set_state)(void *self, const dkr_render_state *state);

    /* Fenêtre de ciseaux. **NATIF** — `grClipWindow`. Les coordonnées sont en
       pixels écran, bornes incluses à gauche et en haut, exclues à droite et en
       bas — la convention de Glide, retenue pour n'avoir pas à convertir. */
    void (*set_scissor)(void *self, int x0, int y0, int x1, int y1);

    /* Triangles. Les sommets sont complets et projetés ; voir la remarque sur
       les coordonnées de texture en tête de fichier.
       **NATIF** — `grDrawTriangle`, un appel par triangle.
       `draw_triangles` prend un tableau de 3n sommets afin d'amortir l'appel
       traversant l'interface, pas celui traversant Glide. */
    void (*draw_triangles)(void *self, const dkr_render_vertex *vertices,
                           int triangle_count);

    /* Rectangle plein. **A EMULER** — Glide n'a pas de primitive de rectangle.
       Deux triangles suffisent, et le backend les fabrique : le faire ici plutôt
       que dans le décodeur évite d'imposer la même dépense au rastériseur
       logiciel, qui sait remplir un rectangle directement. */
    void (*fill_rect)(void *self, int x0, int y0, int x1, int y1,
                      unsigned argb);

    /* Textures. `upload` rend zéro en cas d'échec — mémoire de TMU pleine, par
       exemple, ce que E05-S02 devra traiter. */
    dkr_texture_handle (*texture_upload)(void *self, const dkr_texture_desc *desc);
    void               (*texture_release)(void *self, dkr_texture_handle handle);

    void *self;                   /* état privé de l'implémentation */
} dkr_render_backend;

/* Une implémentation vide, qui accepte tout et ne dessine rien.
 *
 * Elle n'est pas un bouchon de complaisance : elle sert à établir que
 * l'interface se compile et se lie sans backend réel, et elle donne au décodeur
 * une cible pendant que Glide et le rastériseur logiciel s'écrivent. Le jour où
 * un défaut de rendu apparaîtra, la comparer aux deux autres dira si le décodeur
 * ou le backend est en cause. */
void dkr_render_backend_null(dkr_render_backend *out);

/* Les implémentations réelles. Déclarées ici plutôt que chacune dans son
   en-tête : l'appelant qui choisit un backend à l'exécution les veut toutes
   visibles d'un seul include, et c'est ainsi que le comparateur de E09-S02 les
   ouvre côte à côte sur la même entrée.

   `glide` n'est disponible que sur la cible Win95 ; sur l'hôte, seule la version
   logicielle est compilée, et c'est délibéré — l'oracle doit tourner partout. */
void dkr_render_backend_software(dkr_render_backend *out);
#if defined(DKR_TARGET_WIN95)
void dkr_render_backend_glide(dkr_render_backend *out);
unsigned long dkr_glide_backend_triangle_count(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_BACKEND_H */
