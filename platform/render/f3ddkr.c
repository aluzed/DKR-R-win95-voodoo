/* E04-S02 — mise en œuvre. La cartographie est dans
 * `docs/research/f3ddkr-commands.md`, le contrat dans `f3ddkr.h`. */
#include "f3ddkr.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Opcodes, repris de `f3ddkr_rt64.cpp`. */
#define OP_MATRIX        0x01
#define OP_TEXOFFSET     0x02
#define OP_MOVEMEM       0x03
#define OP_VERTEX        0x04
#define OP_TRIANGLE      0x05
#define OP_DLBRANCH      0x06
#define OP_DLCOUNTED     0x07
#define OP_ENDDL         0xB8
#define OP_MOVEWORD      0xBC
#define OP_DMAOFFSETS    0xBF
#define OP_LOADBLOCK     0xF3
#define OP_FILLRECT      0xF6
#define OP_SETFILLCOLOR  0xF7
#define OP_SETTEXIMAGE   0xFD
#define OP_SETCOLORIMAGE 0xFF

#define MOVEWORD_BILLBOARD   0x02
#define MOVEWORD_MVPMATRIX   0x0A
#define MOVEWORD_PRESENT     0xFE
#define PRESENT_MAGIC        0x444B5200u
#define PRESENT_META_MASK    0xFFu

#define RDRAM_MASK           0x00FFFFFFu
#define MAX_VERTICES         32u
#define MAX_NESTED           32u
#define VERTEX_STRIDE        10u
#define TRIANGLE_STRIDE      16u
#define MATRIX_BYTES         64u

/* Le volume de journalisation est borné. Une display list corrompue produirait
   sinon des milliers de lignes par image, ce qui noie le diagnostic au lieu de
   l'éclairer — et coûte cher sur une machine de 1998. */
#define MAX_LOGGED_REJECTS   64

/* --- Les commandes reconnues dont l'effet n'est pas encore branché ---------- *
 *
 * Le décodeur n'implémentait que sept opcodes et **arrêtait la liste** sur tout
 * le reste. C'était le bon choix tant qu'il ne lisait que des display lists
 * fabriquées : après un opcode vraiment inconnu le flux est désynchronisé, et
 * poursuivre inventerait des commandes.
 *
 * Face aux listes du jeu, ce choix rendait le décodeur inutile : mesuré sur la
 * machine, **600 listes, 3580 commandes, zéro triangle** — chacune s'arrêtait
 * sur son premier `0xE9` ou `0xB6`, c'est-à-dire une synchronisation RDP et un
 * effacement de mode géométrique. La géométrie était toujours *après*.
 *
 * Les deux familles ci-dessous sont celles du microcode F3D et du RDP, toutes en
 * commandes de huit octets, donc toutes enjambables sans ambiguïté :
 *
 *     0xB0..0xBF   immédiates F3D — RDPHALF, TRI2, modes géométriques, autres
 *                  modes, texture, POPMTX, CULLDL
 *     0xE4..0xFF   RDP — synchronisations, ciseaux, tuiles, couleurs, combineur
 *
 * La borne basse était d'abord posée à 0xB6, par lecture de la table des
 * opcodes plutôt que par mesure. La machine a répondu `0xB4` — `G_RDPHALF_1` —
 * une fois par liste, six cents fois. La famille commence bien à 0xB0, et
 * l'écart tenait à ce que la table consultée ne listait que la partie du jeu de
 * commandes qui a un effet géométrique.
 *
 * Les énumérer plutôt que de tout accepter garde la détection de
 * désynchronisation : un opcode hors de ces plages arrête toujours la liste.
 * C'est la propriété qu'on aurait perdue en remplaçant simplement le rejet par
 * un `break`, et elle vaut d'être gardée — c'est elle qui a permis de voir que
 * la disposition mémoire était juste, puisque *aucun* rejet d'adresse n'est
 * apparu. */
static int opcode_effet_differe(unsigned int opcode)
{
    return (opcode >= 0xB0u && opcode <= 0xBFu) ||
           (opcode >= 0xE4u && opcode <= 0xFFu);
}

const char *dkr_f3d_reject_text(dkr_f3d_reject r)
{
    switch (r) {
    case DKR_F3D_REJECT_ADDRESS: return "adresse hors RDRAM";
    case DKR_F3D_REJECT_COUNT:   return "nombre invalide";
    case DKR_F3D_REJECT_INDEX:   return "index de sommet hors cache";
    case DKR_F3D_REJECT_DEPTH:   return "imbrication trop profonde";
    default:                     return "opcode inconnu";
    }
}

/* --- Lecture bornée -------------------------------------------------------- *
 *
 * Toutes les lectures passent par ici. C'est ce qui rend la discipline de
 * validation vérifiable : il n'y a qu'un endroit à relire pour s'assurer que
 * rien ne sort de RDRAM.
 */
static int in_range(const dkr_f3d_context *c, unsigned int addr, unsigned int len)
{
    /* En 64 bits pour que la somme ne reboucle pas : `addr + len` sur 32 bits
       peut redevenir petit et faire passer une plage manifestement hors bornes. */
    const unsigned long long end = (unsigned long long)addr + (unsigned long long)len;
    return c->rdram && end <= (unsigned long long)c->rdram_size;
}

/* --- Deux dispositions mémoire pour la même RDRAM --------------------------- *
 *
 * Les épreuves construisent une RDRAM en gros-boutiste franc, comme la console.
 * Le jeu, lui, fournit l'instantané de librecomp, qui range la même mémoire
 * **entrelacée par XOR-3** : l'octet d'adresse invitée `a` se trouve à `a ^ 3`.
 * C'est visible dans les macros de N64Recomp :
 *
 *     MEM_BU(o, r)  ->  *(uint8_t *)(rdram + ((r + o) ^ 3) - ...)
 *     MEM_HU(o, r)  ->  *(uint16_t *)(rdram + ((r + o) ^ 2) - ...)
 *     MEM_W (o, r)  ->  *(int32_t  *)(rdram + ((r + o))     - ...)
 *
 * Le mot de 32 bits n'a **pas** de XOR : l'entrelacement et le petit-boutisme de
 * l'hôte s'annulent exactement, de sorte qu'une lecture native rend la valeur
 * invitée correcte. C'est contre-intuitif, et l'inverser — retourner les octets
 * à la main « pour corriger le boutisme » — donne des adresses absurdes que l'on
 * attribue ensuite au décodeur.
 *
 * Le drapeau vaut zéro par défaut, donc les épreuves ne changent pas de
 * comportement : c'est le jeu qui déclare la disposition qu'il fournit. */
static unsigned char read_u8(const dkr_f3d_context *c, unsigned int a)
{
    return c->rdram[c->rdram_native ? (a ^ 3u) : a];
}

static short read_s16(const dkr_f3d_context *c, unsigned int a)
{
    return (short)(((unsigned)read_u8(c, a) << 8) | read_u8(c, a + 1u));
}

static unsigned int read_u32(const dkr_f3d_context *c, unsigned int a)
{
    /* Le chemin rapide n'est pas un luxe : c'est la lecture la plus fréquente du
       décodeur — deux par commande — et la cible est un Pentium II. Il ne vaut
       que sur une adresse alignée, ce qui est le cas des display lists ; la voie
       générale reste correcte pour tout le reste. */
    if (c->rdram_native && (a & 3u) == 0u) {
        return *(const unsigned int *)(const void *)(c->rdram + a);
    }
    return ((unsigned)read_u8(c, a)      << 24) | ((unsigned)read_u8(c, a + 1u) << 16) |
           ((unsigned)read_u8(c, a + 2u) <<  8) |  (unsigned)read_u8(c, a + 3u);
}

static void trace(dkr_f3d_context *c, const char *fmt, ...)
{
    char line[192];
    va_list ap;
    if (!c->trace) {
        return;
    }
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    c->trace(c->trace_user, line);
}

static void reject(dkr_f3d_context *c, dkr_f3d_reject why, const char *detail)
{
    c->state.rejects[why]++;
    if (c->state.rejects[why] <= MAX_LOGGED_REJECTS) {
        trace(c, "REJET %s : %s", dkr_f3d_reject_text(why), detail);
    }
}

/* --- Les commandes --------------------------------------------------------- */

static void cmd_dma_offsets(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    c->state.matrix_offset = w0 & RDRAM_MASK;
    c->state.vertex_offset = w1 & RDRAM_MASK;
    trace(c, "DMAOffsets matrices=0x%06X sommets=0x%06X",
          c->state.matrix_offset, c->state.vertex_offset);
}

static void cmd_matrix(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    unsigned int index, address;

    /* Le champ bas doit valoir 64 — la taille d'une matrice. Ce n'est pas une
       validation défensive mais la façon dont le microcode distingue ses
       variantes : autre chose, et la commande n'est pas un chargement. */
    if ((w0 & 0xFFFFu) != MATRIX_BYTES) {
        return;
    }
    index = (w0 >> 16) & 0x0Fu;
    if (index == 0) {
        index = (w0 >> 22) & 0x03u;
    }
    if (index > 2u) { index = 2u; }
    c->state.selected_matrix = index;

    address = (c->state.matrix_offset + w1) & RDRAM_MASK;
    if (!in_range(c, address, MATRIX_BYTES)) {
        char d[64];
        sprintf(d, "matrice a 0x%06X", address);
        reject(c, DKR_F3D_REJECT_ADDRESS, d);
        return;
    }
    {
        dkr_matrix loaded;
        /* `dkr_matrix_from_fixed` lit une suite d'octets en gros-boutiste franc.
           Sous la disposition de librecomp il faut donc la lui remettre à plat —
           64 octets, une fois par commande de matrice, ce qui ne pèse rien face
           aux seize multiplications qui suivent. Passer le pointeur brut ferait
           lire des matrices dont les octets sont permutés quatre par quatre : le
           décor ne planterait pas, il serait simplement faux, et l'on chercherait
           l'erreur dans la transformation. */
        unsigned char plat[MATRIX_BYTES];
        const unsigned char *source = c->rdram + address;
        if (c->rdram_native) {
            unsigned int i;
            for (i = 0; i < MATRIX_BYTES; i++) { plat[i] = read_u8(c, address + i); }
            source = plat;
        }
        if (dkr_matrix_from_fixed(source, &loaded)) {
            dkr_transform_set_matrix(&c->transform, (int)index, &loaded);
        }
    }
    dkr_transform_select(&c->transform, (int)index);
    trace(c, "Matrix emplacement=%u adresse=0x%06X", index, address);
}

static void cmd_vertex(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned int count       = ((w0 >> 19) & 0x1Fu) + 1u;
    const unsigned int destination = (w0 >> 9) & 0x1Fu;
    const unsigned int source      = (c->state.vertex_offset + w1) & RDRAM_MASK;
    unsigned int i;

    /* Les trois conditions sont distinctes et toutes nécessaires : un nombre
       trop grand, une destination trop loin, ou un lot qui déborde le cache par
       la somme des deux. La troisième est celle qu'on oublie. */
    if (count > MAX_VERTICES || destination >= MAX_VERTICES ||
        count > MAX_VERTICES - destination) {
        char d[80];
        sprintf(d, "%u sommets a l'index %u", count, destination);
        reject(c, DKR_F3D_REJECT_COUNT, d);
        return;
    }
    if (!in_range(c, source, count * VERTEX_STRIDE)) {
        char d[64];
        sprintf(d, "sommets a 0x%06X", source);
        reject(c, DKR_F3D_REJECT_ADDRESS, d);
        return;
    }
    for (i = 0; i < count; i++) {
        const unsigned int a = source + i * VERTEX_STRIDE;
        dkr_source_vertex sv;
        /* Le sommet DKR : x, y, z en 16 bits signes puis r, g, b, a en octets.
           **Aucune coordonnee de texture** — elles arrivent au triangle. */
        sv.x = read_s16(c, a + 0);
        sv.y = read_s16(c, a + 2);
        sv.z = read_s16(c, a + 4);
        sv.r = read_u8(c, a + 6);
        sv.g = read_u8(c, a + 7);
        sv.b = read_u8(c, a + 8);
        sv.a = read_u8(c, a + 9);
        /* Transforme **ici** et non au triangle : un sommet servi par trois
           triangles serait sinon transforme trois fois, et c'est le poste le
           plus lourd du portage. Les coordonnees de texture restent a zero —
           elles seront posees au triangle. */
        dkr_transform_to_clip(&c->transform, &sv, 0.0f, 0.0f,
                              &c->cache[destination + i]);
        c->cache_valid[destination + i] = 1;
    }
    c->state.vertices += count;
    trace(c, "Vertex %u sommets vers %u depuis 0x%06X", count, destination, source);
}

static void cmd_triangle(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned int count  = ((w0 >> 20) & 0x0Fu) + 1u;
    const unsigned int source = w1 & RDRAM_MASK;
    unsigned int i;

    if (count == 0u) {
        reject(c, DKR_F3D_REJECT_COUNT, "zero triangle");
        return;
    }
    if (!in_range(c, source, count * TRIANGLE_STRIDE)) {
        char d[64];
        sprintf(d, "triangles a 0x%06X", source);
        reject(c, DKR_F3D_REJECT_ADDRESS, d);
        return;
    }
    /* **Tout le lot est valide avant d'en dessiner le premier.**
     *
     * Valider au fil de l'eau laisserait dessiner les triangles valides avant de
     * rejeter le lot, ce qui rend le defaut dependant du contenu — donc
     * difficile a reproduire. Le decodeur d'origine procede ainsi et cette
     * extraction le conserve. */
    for (i = 0; i < count; i++) {
        const unsigned int a = source + i * TRIANGLE_STRIDE;
        if (read_u8(c, a + 1) >= MAX_VERTICES ||
            read_u8(c, a + 2) >= MAX_VERTICES ||
            read_u8(c, a + 3) >= MAX_VERTICES) {
            char d[80];
            sprintf(d, "lot de %u a 0x%06X, triangle %u", count, source, i);
            reject(c, DKR_F3D_REJECT_INDEX, d);
            return;
        }
    }
    c->state.triangles += count;
    trace(c, "Triangle %u depuis 0x%06X", count, source);

    /* --- L'emission, et c'est ici que la chaine se referme ------------------ *
     *
     * Chaque triangle traverse : coordonnees de texture posees par coin,
     * decoupage au plan proche, projection, culling, rejet hors ecran. */
    for (i = 0; i < count; i++) {
        const unsigned int a = source + i * TRIANGLE_STRIDE;
        const unsigned char flags = read_u8(c, a + 0);
        const unsigned char idx[3] = { read_u8(c, a + 1), read_u8(c, a + 2),
                                       read_u8(c, a + 3) };
        dkr_clip_vertex   tri[3], clipped[6];
        dkr_render_vertex out[6];
        int pieces, k, corner, emitted_here = 0;
        dkr_cull_mode cull;

        for (corner = 0; corner < 3; corner++) {
            if (!c->cache_valid[idx[corner]]) {
                /* Un index valide pointant sur un emplacement jamais charge :
                   la display list emploie un sommet qu'elle n'a pas defini. Ce
                   n'est pas une adresse fausse, donc pas un rejet de plage —
                   mais dessiner un sommet non initialise donnerait une geometrie
                   aleatoire, ce qui est pire qu'un triangle absent. */
                reject(c, DKR_F3D_REJECT_INDEX, "sommet non charge");
                emitted_here = -1;
                break;
            }
            tri[corner] = c->cache[idx[corner]];
            /* Les s, t du coin, en 16 bits signes. C'est ici qu'elles entrent —
               le sommet ne les portait pas. */
            tri[corner].s = (float)read_s16(c, a + 4 + corner * 4);
            tri[corner].t = (float)read_s16(c, a + 6 + corner * 4);
        }
        if (emitted_here < 0) {
            continue;
        }

        pieces = dkr_clip_near(tri, clipped);
        if (pieces == 0) {
            c->state.clipped_away++;
            continue;
        }
        if (pieces == 2) {
            c->state.clip_split++;
        }

        /* Le bit 0x40 desactive l'elimination des faces arriere ; sinon le sens
           vient du signe de l'echelle en x de la fenetre. */
        cull = dkr_cull_mode_for_viewport(c->transform.viewport_scale_x,
                                          (flags & 0x40u) == 0);

        for (k = 0; k < pieces; k++) {
            dkr_render_vertex *v = &out[k * 3];
            dkr_clip_project(&c->transform, &clipped[k * 3 + 0], &v[0]);
            dkr_clip_project(&c->transform, &clipped[k * 3 + 1], &v[1]);
            dkr_clip_project(&c->transform, &clipped[k * 3 + 2], &v[2]);
            if (!dkr_cull_accept(v, cull)) {
                c->state.culled++;
                continue;
            }
            if (dkr_clip_reject_offscreen(v, 640, 480, DKR_CLIP_DEFAULT_MARGIN)) {
                c->state.clipped_away++;
                continue;
            }
            if (c->backend && c->backend->draw_triangles) {
                c->backend->draw_triangles(c->backend->self, v, 1);
            }
            c->state.emitted++;
        }
    }
}

static void cmd_move_word(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned char type = (unsigned char)(w0 & 0xFFu);
    if (type == MOVEWORD_PRESENT &&
        (w1 & ~PRESENT_META_MASK) == PRESENT_MAGIC) {
        /* Extension du portage, pas du microcode d'origine : le mot magique
           « DKR\0 » distingue les commandes ajoutees par le moteur moderne de
           celles du jeu. */
        trace(c, "PresentationGroup mode=%u", w1 & 7u);
    } else if (type == MOVEWORD_BILLBOARD) {
        c->state.billboard = (unsigned char)(w1 & 1u);
        trace(c, "MoveWord panneau=%u", c->state.billboard);
    } else if (type == MOVEWORD_MVPMATRIX) {
        unsigned int m = (w1 >> 6) & 0x03u;
        if (m > 2u) { m = 2u; }
        c->state.selected_matrix = m;
        dkr_transform_select(&c->transform, (int)m);
        trace(c, "MoveWord matrice=%u", m);
    } else {
        trace(c, "MoveWord type=0x%02X valeur=0x%08X", type, w1);
    }
}

/* --- Le rectangle plein ----------------------------------------------------- *
 *
 * Mesuré sur la machine avant d'être écrit : sur les 47 000 commandes de la
 * séquence de démarrage, **`FILLRECT` est le seul ordre de dessin émis** — ni
 * sommet, ni triangle, ni rectangle texturé, deux remplissages par image. Ce
 * chemin n'est donc pas un détail de la 2D : c'est tout ce qui met des pixels à
 * l'écran à ce stade du portage.
 *
 * ## Les coordonnées
 *
 * `gDPFillRectangle` range les deux coins dans les deux mots, en virgule fixe
 * 10.2, et **le coin inférieur droit est inclus** :
 *
 *     w0 = opcode<<24 | lrx<<14 | lry<<2
 *     w1 =              ulx<<14 | uly<<2
 *
 * Oublier l'inclusion donne un rectangle trop court d'un pixel en bas et à
 * droite. Sur un effacement plein écran cela laisse une ligne du fond visible,
 * qu'on attribue au rastériseur plutôt qu'à la convention.
 *
 * ## L'échelle
 *
 * Les coordonnées sont dans l'espace du tampon de couleur du jeu, pas dans celui
 * de l'écran. Le facteur se **lit** dans `SETCOLORIMAGE`, qui porte la largeur,
 * plutôt que de supposer les 320 pixels habituels de la N64 : DKR change de
 * tampon en cours de route, et une échelle supposée produirait un décor décalé
 * sur certains écrans seulement — le genre de défaut qu'on met des heures à
 * relier à sa cause.
 *
 * ## La couleur
 *
 * En mode remplissage sur seize bits, `SETFILLCOLOR` porte **deux pixels
 * RGBA5551 côte à côte**, parce que le RDP écrit deux pixels par cycle. On prend
 * les seize bits de poids faible : les deux moitiés sont identiques pour un
 * remplissage uni, et une couleur à demi fausse serait plus déroutante qu'une
 * couleur franchement fausse. */
static unsigned int couleur_depuis_5551(unsigned int pixel)
{
    const unsigned int r = (pixel >> 11) & 0x1Fu;
    const unsigned int v = (pixel >>  6) & 0x1Fu;
    const unsigned int b = (pixel >>  1) & 0x1Fu;
    /* La réplication des bits de poids fort plutôt qu'un décalage seul : 31 doit
       donner 255 et non 248, sans quoi le blanc n'est jamais blanc. */
    const unsigned int r8 = (r << 3) | (r >> 2);
    const unsigned int v8 = (v << 3) | (v >> 2);
    const unsigned int b8 = (b << 3) | (b >> 2);
    return (r8 << 16) | (v8 << 8) | b8;
}

static void cmd_fill_rect(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    /* 10.2 en virgule fixe : deux bits de fraction, qu'on abandonne. Le RDP
       remplit par pixel entier en mode remplissage. */
    const int lrx = (int)((w0 >> 14) & 0x3FFu);
    const int lry = (int)((w0 >>  2) & 0x3FFu);
    const int ulx = (int)((w1 >> 14) & 0x3FFu);
    const int uly = (int)((w1 >>  2) & 0x3FFu);

    const float ecran_w = 2.0f * c->transform.viewport_scale_x;
    const float ecran_h = -2.0f * c->transform.viewport_scale_y;
    float echelle_x = 1.0f, echelle_y = 1.0f;
    int x0, y0, x1, y1;

    if (!c->backend || !c->backend->fill_rect) {
        trace(c, "FillRect ignore : pas de backend");
        return;
    }

    if (c->state.color_image_width > 0u && ecran_w > 0.0f) {
        echelle_x = ecran_w / (float)c->state.color_image_width;
        /* La hauteur du tampon n'est portée par aucune commande — le RDP ne la
           connaît pas, il n'a que la largeur et l'adresse. On applique donc le
           même facteur qu'en x, ce qui est juste tant que le tampon a le rapport
           de l'écran. C'est le cas de DKR (320x240 pour 640x480) et c'est une
           supposition qu'il faudra reprendre le jour où ce ne le sera plus. */
        echelle_y = echelle_x;
    }
    (void)ecran_h;

    x0 = (int)((float)ulx * echelle_x);
    y0 = (int)((float)uly * echelle_y);
    /* +1 : le coin inférieur droit est inclus côté RDP, exclu côté backend. */
    x1 = (int)((float)(lrx + 1) * echelle_x);
    y1 = (int)((float)(lry + 1) * echelle_y);

    c->backend->fill_rect(c->backend->self, x0, y0, x1, y1,
                          c->state.fill_color_argb);
    c->state.rects++;
    trace(c, "FillRect %d,%d..%d,%d couleur=0x%06X",
          x0, y0, x1, y1, c->state.fill_color_argb);
}

/* --- La boucle ------------------------------------------------------------- */

unsigned long dkr_f3d_run(dkr_f3d_context *c, unsigned int address)
{
    unsigned int return_stack[MAX_NESTED];
    unsigned int depth = 0;
    unsigned long executed = 0;
    int running = 1;

    if (!c || !c->rdram) {
        return 0;
    }
    address &= RDRAM_MASK;

    while (running) {
        unsigned int w0, w1, opcode;

        if (!in_range(c, address, 8u)) {
            char d[64];
            sprintf(d, "commande a 0x%06X", address);
            reject(c, DKR_F3D_REJECT_ADDRESS, d);
            break;
        }
        w0 = read_u32(c, address);
        w1 = read_u32(c, address + 4u);
        opcode = (w0 >> 24) & 0xFFu;
        address += 8u;
        executed++;
        c->state.commands++;
        c->state.opcodes[opcode]++;

        switch (opcode) {
        case OP_DMAOFFSETS: cmd_dma_offsets(c, w0, w1); break;
        case OP_MATRIX:     cmd_matrix(c, w0, w1);      break;
        case OP_VERTEX:     cmd_vertex(c, w0, w1);      break;
        case OP_TRIANGLE:   cmd_triangle(c, w0, w1);    break;
        case OP_MOVEWORD:   cmd_move_word(c, w0, w1);   break;

        case OP_TEXOFFSET:
            /* **`w1` est une adresse RDRAM, pas un couple de decalages.**
             *
             * Ce decodeur lisait `(w1 >> 16)` et `(w1 & 0xFFFF)` comme des
             * decalages `s` et `t` sur seize bits. Le portage voisin, qui tourne,
             * en fait tout autre chose : `data.texture_offset = w1 & 0x00FFFFFF`,
             * une **base d'adressage pour le chargement de texture**, et la
             * commande remet a zero le decalage et le compte.
             *
             * L'erreur ne se serait pas vue tout de suite. Une base d'adresse
             * lue comme deux decalages de texture produit des coordonnees
             * absurdes sur les surfaces concernees — donc un motif deplace, pas
             * une absence — et l'on aurait cherche du cote du decodage de
             * texture. E05-S07 demandait de relever ce comportement plutot que
             * de le supposer ; c'est ce qui l'a revele. */
            c->state.texture_offset = w1 & 0x00FFFFFFu;
            c->state.texture_shift  = 0;
            c->state.texture_count  = 0;
            trace(c, "TextureOffset base=0x%06X", c->state.texture_offset);
            break;

        case OP_DLBRANCH: {
            /* Alignee sur huit octets — la taille d'une commande. Une cible
               desalignee decoderait des mots a cheval et produirait des opcodes
               fantaisistes. */
            const unsigned int target = w1 & 0x00FFFFF8u;
            const int branch = ((w0 >> 16) & 0x01u) != 0;
            if (!in_range(c, target, 8u)) {
                char d[64];
                sprintf(d, "liste a 0x%06X", target);
                reject(c, DKR_F3D_REJECT_ADDRESS, d);
                break;
            }
            if (!branch) {
                if (depth >= MAX_NESTED) {
                    char d[48];
                    sprintf(d, "profondeur %u", depth);
                    reject(c, DKR_F3D_REJECT_DEPTH, d);
                    break;
                }
                return_stack[depth++] = address;
            }
            trace(c, "DisplayList %s vers 0x%06X",
                  branch ? "branchement" : "appel", target);
            address = target;
            break;
        }

        case OP_ENDDL:
            if (depth == 0u) {
                trace(c, "EndDisplayList — fin");
                running = 0;
            } else {
                address = return_stack[--depth];
                trace(c, "EndDisplayList — retour a 0x%06X", address);
            }
            break;

        case OP_DLCOUNTED: {
            const unsigned int count  = (w0 >> 16) & 0xFFu;
            const unsigned int target = w1 & RDRAM_MASK;
            if (count == 0u || target == 0u ||
                !in_range(c, target, count * 8u)) {
                char d[72];
                sprintf(d, "%u commandes a 0x%06X", count, target);
                reject(c, DKR_F3D_REJECT_COUNT, d);
                break;
            }
            if (depth >= MAX_NESTED) {
                reject(c, DKR_F3D_REJECT_DEPTH, "liste comptee");
                break;
            }
            return_stack[depth++] = address;
            trace(c, "CountedDisplayList %u commandes a 0x%06X", count, target);
            address = target;
            break;
        }

        case OP_FILLRECT:
            cmd_fill_rect(c, w0, w1);
            break;

        case OP_SETFILLCOLOR:
            c->state.fill_color_raw = w1;
            c->state.fill_color_argb = couleur_depuis_5551(w1 & 0xFFFFu);
            trace(c, "SetFillColor brut=0x%08X -> 0x%06X",
                  w1, c->state.fill_color_argb);
            break;

        case OP_SETCOLORIMAGE:
            /* Les douze bits de poids faible portent la largeur moins un. C'est
               d'ici que vient l'échelle des rectangles, plutôt que d'une
               supposition sur les 320 pixels de la N64. */
            c->state.color_image_width = (w0 & 0xFFFu) + 1u;
            trace(c, "SetColorImage largeur=%u adresse=0x%06X",
                  c->state.color_image_width, w1 & RDRAM_MASK);
            break;

        case OP_MOVEMEM:
        case OP_LOADBLOCK:
        case OP_SETTEXIMAGE:
            /* Decodees comme commandes, mais leur effet appartient aux etages
               suivants — textures (E04-S07) et etat RDP (E04-S06). Les compter
               ici etablit deja que la sequence est juste. */
            trace(c, "opcode 0x%02X w0=0x%08X w1=0x%08X", opcode, w0, w1);
            break;

        default: {
            char d[48];
            if (opcode_effet_differe(opcode)) {
                /* Reconnue, enjambee. Comptee a part de `commands` : ce chiffre
                   dit **quelle part de l'image on ignore encore**, et c'est la
                   mesure qui manquerait le plus quand le decor sortira faux. */
                c->state.deferred++;
                if (c->state.deferred <= MAX_LOGGED_REJECTS) {
                    trace(c, "differe 0x%02X w0=0x%08X w1=0x%08X", opcode, w0, w1);
                }
                break;
            }
            sprintf(d, "0x%02X a 0x%06X", opcode, address - 8u);
            reject(c, DKR_F3D_REJECT_OPCODE, d);
            /* On s'arrete : apres un opcode vraiment inconnu, le flux est
               probablement desynchronise et poursuivre inventerait des
               commandes. La detection subsiste precisement parce que les
               familles connues sont enumerees plutot que tout accepte. */
            running = 0;
            break;
        }
        }
    }
    return executed;
}

void dkr_f3d_init(dkr_f3d_context *ctx, const unsigned char *rdram,
                  unsigned int rdram_size, dkr_render_backend *backend)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->rdram      = rdram;
    ctx->rdram_size = rdram_size;
    ctx->backend    = backend;
    dkr_transform_init(&ctx->transform);
}
