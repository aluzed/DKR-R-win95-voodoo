/* E04-S02 — mise en œuvre. La cartographie est dans
 * `docs/research/f3ddkr-commands.md`, le contrat dans `f3ddkr.h`. */
#include "f3ddkr.h"
#include "rdp_state.h"
#include "texture.h"

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
#define OP_SETOTHERMODE_L 0xB9
#define OP_SETOTHERMODE_H 0xBA
#define OP_SETCOMBINE     0xFC
#define MOVEMEM_VIEWPORT  0x80
#define VIEWPORT_BYTES    16u
#define OP_SETTILE        0xF5
#define OP_SETTILESIZE    0xF2
#define OP_RDPSETOTHERMODE 0xEF

#define MOVEWORD_BILLBOARD   0x02
#define MOVEWORD_MVPMATRIX   0x0A
#define MOVEWORD_PRESENT     0xFE
#define PRESENT_MAGIC        0x444B5200u
#define PRESENT_META_MASK    0xFFu

#define RDRAM_MASK           0x00FFFFFFu
#define MAX_VERTICES         32u
#define MAX_NESTED           32u
/* Une liste ordinaire va jusqu'a son ENDDL ; une liste comptee s'arrete a son
   compte. Zero ne peut pas dire les deux. */
#define SANS_COMPTE          0xFFFFFFFFu
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

/* --- Du tampon du jeu vers l'écran -----------------------------------------
 *
 * Le jeu raisonne dans son tampon de couleur — 320 pixels de large pour DKR — et
 * la carte affiche en 640x480. Le facteur est **lu** dans `SETCOLORIMAGE` plutôt
 * que supposé, et il sert à deux endroits : les rectangles pleins et la fenêtre
 * d'affichage. Les faire diverger donnerait une interface 2D et une géométrie 3D
 * à deux échelles différentes, ce qui se voit mais ne se comprend pas. */
static float echelle_ecran(const dkr_f3d_context *c)
{
    if (c->screen_width == 0u || c->state.color_image_width == 0u) {
        return 1.0f;
    }
    return (float)c->screen_width / (float)c->state.color_image_width;
}

/* Declaree ici parce que le dessin de triangles la precede dans ce fichier : la
   traduction d'etat vit avec le reste du chemin 2D, plus bas. */
static void appliquer_etat(dkr_f3d_context *c);

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
            /* --- La normalisation, qui manquait -------------------------- *
             *
             * Le microcode donne s et t en **10.5 en virgule fixe** : trente-
             * deux pas par texel. `dkr_clip_project` attend, lui, du [0,1] —
             * son commentaire le dit, et il applique ensuite l'échelle de 256
             * de Glide. Entre les deux il manquait la division par 32 et par la
             * largeur de la texture.
             *
             * L'ordre de grandeur de l'erreur dit pourquoi rien ne s'échantil-
             * lonnait : pour une texture de 32 texels, un coin à droite vaut
             * 1024 en brut, donc 262 144 après l'échelle de Glide au lieu de
             * 256. Ce n'est pas une texture décalée, c'est une texture hors de
             * tout.
             *
             * **La largeur remplie, pas la réelle** : la texture n'occupe que le
             * coin supérieur gauche de ce qu'on a chargé, puisque le remplissage
             * en puissance de deux l'a agrandie. Normaliser sur la largeur
             * réelle étirerait le motif d'un facteur allant jusqu'à deux. */
            {
                const float sb = (float)read_s16(c, a + 4 + corner * 4);
                const float tb = (float)read_s16(c, a + 6 + corner * 4);
                tri[corner].s = sb * c->tex_echelle_s;
                tri[corner].t = tb * c->tex_echelle_t;
                /* La mesure qui peut réfuter l'interprétation ci-dessus : si le
                   10.5 est le bon format et la largeur la bonne, les extrêmes
                   doivent tenir dans un voisinage de [0,1]. Des milliers
                   diraient que l'échelle est fausse, et le dire en chiffres
                   plutôt qu'à l'écran est tout l'intérêt. */
                if (tri[corner].s < c->state.s_min) { c->state.s_min = tri[corner].s; }
                if (tri[corner].s > c->state.s_max) { c->state.s_max = tri[corner].s; }
                if (tri[corner].t < c->state.t_min) { c->state.t_min = tri[corner].t; }
                if (tri[corner].t > c->state.t_max) { c->state.t_max = tri[corner].t; }
            }
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
            appliquer_etat(c);
            /* **De quoi les triangles émis sont faits.**
             *
             * L'écran reste blanc alors que les textures chargent et que les
             * coordonnées tiennent dans le bon ordre de grandeur. Trois causes
             * restent possibles et un seul chiffre — « émis » — les confond :
             * un combineur qui ne lit pas de texel, une texture non liée, ou un
             * échantillonnage muet. Les deux premières se comptent ici, et
             * c'est trois entiers contre une nouvelle hypothèse au hasard. */
            if (c->render_state.combine < DKR_COMBINE_COUNT) {
                c->state.emis_par_combine[c->render_state.combine]++;
            }
            if (c->render_state.texture != 0) {
                c->state.emis_avec_texture++;
            }
            /* **La taille des triangles à l'écran.**
             *
             * 490 triangles par image sont émis, et l'écran n'en montre qu'un
             * seul, énorme. Les deux ne peuvent pas être vrais en même temps
             * sans que quelque chose d'autre soit faux, et « émis » ne dit pas
             * lequel. Une distribution dominée par des triangles de plus de dix
             * mille pixels accuserait la projection ou les matrices ; une
             * distribution normale dirait au contraire que la géométrie est
             * juste et que c'est l'échantillonnage qui manque.
             *
             * L'aire par le produit vectoriel, en valeur absolue et sans
             * division : on ne cherche pas l'aire exacte mais l'ordre de
             * grandeur, et une racine par triangle se paierait. */
            {
                const float ax = v[1].x - v[0].x, ay = v[1].y - v[0].y;
                const float bx = v[2].x - v[0].x, by = v[2].y - v[0].y;
                float aire = (ax * by - ay * bx) * 0.5f;
                if (aire < 0.0f) { aire = -aire; }
                if (aire < 1.0f)         { c->state.aire[0]++; }
                else if (aire < 100.0f)  { c->state.aire[1]++; }
                else if (aire < 10000.0f){ c->state.aire[2]++; }
                else                     { c->state.aire[3]++; }
            }
            /* **Le mode de profondeur au moment du dessin.**
             *
             * La distribution des aires est normale — 45 % des triangles sous
             * cent pixels — donc la géométrie n'est pas dégénérée. Mais l'écran
             * est couvert par un seul grand polygone, ce qui est exactement ce
             * que produit un tri de profondeur absent : les 18 % de triangles
             * de plus de dix mille pixels recouvrent tout ce qui a été dessiné
             * avant. Compter les modes dit si le test est actif, plutôt que de
             * le supposer d'après le code qui le traduit. */
            if (c->render_state.depth < 4) {
                c->state.emis_par_profondeur[c->render_state.depth]++;
            }
            /* **La plage des profondeurs transmises.**
             *
             * Glide en mode tampon W consomme `oow` directement. L'écran est
             * noir depuis que le test s'active, et deux causes très différentes
             * donnent exactement ce symptôme : un sens de comparaison inversé —
             * déjà consigné dans `win95-glide-etats.md` — ou des profondeurs
             * dégénérées. Ouvrir le masque d'écriture pendant l'effacement n'a
             * rien changé, donc la première est écartée d'un cran.
             *
             * On relève donc l'entrée du test. Des `oow` tous égaux, négatifs,
             * ou hors de la plage que Glide encode expliqueraient le noir sans
             * qu'aucune convention ne soit en cause. */
            /* **Le mélange et le test alpha, comptés comme la profondeur.**
             *
             * Le correctif de `G_RDPSETOTHERMODE` n'a pas réécrit que le mode de
             * cycle : la moitié basse porte aussi le mélangeur et la comparaison
             * alpha. Trois causes peuvent noircir l'écran et j'en ai vérifié une
             * seule — c'est exactement la faute qui a coûté un correctif inutile
             * sur les refus de texture. On les sépare avant d'en corriger une. */
            if (c->render_state.blend < 8) {
                c->state.emis_par_melange[c->render_state.blend]++;
            }
            if (c->render_state.alpha_test) {
                c->state.emis_avec_test_alpha++;
                if (c->render_state.alpha_reference > c->state.alpha_ref_max) {
                    c->state.alpha_ref_max = c->render_state.alpha_reference;
                }
            }
            {
                int q;
                for (q = 0; q < 3; q++) {
                    const float o = v[q].oow;
                    if (o < c->state.oow_min) { c->state.oow_min = o; }
                    if (o > c->state.oow_max) { c->state.oow_max = o; }
                }
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


/* --- L'état RDP, accumulé puis traduit -------------------------------------- *
 *
 * `SETOTHERMODE_H` et `_L` sont des **écritures partielles** : chaque commande
 * remplace un champ du mot de mode, sans toucher au reste. Le codage est celui
 * de F3D — décalage en bits 8..15, longueur en bits 0..7, donnée **déjà
 * décalée** dans `w1` :
 *
 *     0xBA001402 w1=0x00000000   décalage 20, longueur 2  -> type de cycle
 *     0xBA001701 w1=0x00800000   décalage 23, longueur 1  -> le bit y est deja
 *     0xB900031D w1=0x0F0A4000   décalage  3, longueur 29 -> mode de rendu
 *
 * Les trois échantillons viennent de la machine, pas d'un en-tête : F3DEX2
 * inverse le décalage, et se tromper de famille donnerait des champs voisins de
 * ceux visés — un filtrage à la place d'un type de cycle, par exemple, c'est-à-
 * dire une image plausible et fausse plutôt qu'une erreur franche.
 *
 * L'état n'est traduit qu'au moment de dessiner. Le faire à chaque écriture
 * coûterait une traduction complète par commande, et il y en a plus de six mille
 * par image ; le faire au dessin la fait payer une fois par changement réel. */
static void ecrire_othermode(unsigned int *mot, unsigned int w0, unsigned int w1)
{
    const unsigned int sft = (w0 >> 8) & 0xFFu;
    const unsigned int len = w0 & 0xFFu;
    unsigned int masque;
    if (len == 0u || len > 32u || sft >= 32u) {
        return;
    }
    masque = (len >= 32u) ? 0xFFFFFFFFu : (((1u << len) - 1u) << sft);
    *mot = (*mot & ~masque) | (w1 & masque);
}

/* Traduit l'état RDP accumulé et le remet au backend, si quelque chose a changé
   depuis le dernier dessin. */
static void appliquer_etat(dkr_f3d_context *c)
{
    dkr_rdp_state rdp;
    int exact = 1;

    if (!c->etat_sale) {
        return;
    }
    c->etat_sale = 0;

    memset(&rdp, 0, sizeof(rdp));
    dkr_rdp_decode_othermode(c->mode_h, c->mode_l, &rdp);
    rdp.combiner = c->combiner;

    /* Le type de cycle décodé se vérifie tout seul : pendant un `FILLRECT` il
       doit valoir `FILL`. Un décalage mal placé le mettrait ailleurs, et ce
       compteur le dirait sans qu'on ait à regarder l'écran. */
    c->state.cycle_courant = (unsigned char)rdp.cycle;

    /* --- Le filet que `rdp_state.h` réclame, et que personne ne tenait ------- *
     *
     * « Un cas non répertorié doit **se signaler** plutôt que produire un rendu
     * faux en silence. Une configuration manquée ne se voit pas au décodage —
     * elle se voit à l'écran, sous forme d'une surface d'une couleur
     * inattendue, éventuellement dans un seul niveau. »
     *
     * L'inventaire du portage voisin dénombre 33 configurations. La clé les
     * identifie exactement ; `dkr_rdp_combiner_name` rend NULL pour les autres.
     * On compte donc, et l'on retient les premières clés inconnues — un compte
     * seul dirait qu'il en manque, pas lesquelles, et c'est la différence entre
     * un chiffre et une piste. */
    {
        const unsigned long long cle = dkr_rdp_combiner_key(&rdp.combiner, rdp.cycle);
        if (dkr_rdp_combiner_name(cle) != 0) {
            c->state.combineurs_connus++;
        } else {
            unsigned i;
            int vue = 0;
            c->state.combineurs_inconnus++;
            for (i = 0; i < c->state.cles_inconnues_n; i++) {
                if (c->state.cles_inconnues[i] == cle) { vue = 1; break; }
            }
            if (!vue && c->state.cles_inconnues_n < 8u) {
                /* **La clé ne suffit pas.** Elle identifie une configuration ;
                   elle ne dit pas ce qu'elle calcule, donc elle ne permet pas de
                   l'ajouter à la table. On garde la composition, qui est ce dont
                   on a besoin pour la nommer contre les macros `G_CC_*`. */
                const unsigned i2 = c->state.cles_inconnues_n;
                c->state.cles_inconnues[i2] = cle;
                c->state.compo_inconnues[i2] = rdp.combiner;
                c->state.cycle_inconnu[i2] = (unsigned char)rdp.cycle;
                c->state.cles_inconnues_n++;
            }
        }
    }

    dkr_rdp_to_render_state(&rdp, &c->render_state, &exact);
    /* Interrupteur de diagnostic, pas un contournement.
     *
     * Trois causes peuvent noircir l'écran et deux ont été écartées par la
     * mesure. La troisième — la profondeur — ne se réfute pas en la regardant :
     * ses entrées sont saines, sa configuration est celle que E05-S05 a
     * mesurée, et elle noircit quand même. La désactiver d'un cran répond en
     * une course à une question que l'inspection ne tranche pas, et l'on garde
     * l'interrupteur : il resservira à chaque fois qu'un doute portera sur le
     * tri plutôt que sur ce qui est dessiné. */
    if (c->sans_profondeur) {
        c->render_state.depth = DKR_DEPTH_DISABLED;
    }
    /* --- Le handle de texture ne survit pas à la traduction ------------------ *
     *
     * `dkr_rdp_to_render_state` remplit **tout** le bloc depuis l'état RDP, et
     * l'état RDP ne connaît pas nos handles : le champ `texture` que le
     * chargement venait d'y poser était donc écrasé à chaque application.
     *
     * Mesuré, et c'est ce qui a désigné la cause sans détour : 45 773 textures
     * chargées, 246 707 triangles émis avec un combineur qui lit un texel, et
     * **zéro triangle émis avec une texture liée**. Trois chiffres qui, séparés,
     * ne laissent qu'une explication ; réunis sous « émis », ils n'en
     * laissaient aucune.
     *
     * Le handle vit donc dans le contexte, qui est sa vraie place — c'est une
     * ressource du décodeur, pas un mode du RDP — et il est reposé après la
     * traduction. */
    c->render_state.texture = c->texture_liee;
    if (!exact) {
        /* **Une traduction approchée qui ne s'annonce pas est pire qu'un
           échec** : elle produit une image plausible et fausse. Le compteur est
           le filet que `rdp_state.h` réclame explicitement. */
        c->state.etats_approches++;
    }
    c->state.etats_appliques++;

    if (c->backend && c->backend->set_state) {
        c->backend->set_state(c->backend->self, &c->render_state);
    }
}



/* --- Les textures ---------------------------------------------------------- *
 *
 * Trois commandes portent l'information, et **aucune ne suffit seule** :
 *
 *     SETTIMG      (0xFD)  format, taille, adresse en RDRAM
 *     SETTILE      (0xF5)  format et taille de la tuile, enveloppement
 *     SETTILESIZE  (0xF2)  les dimensions, en virgule fixe 10.2
 *
 * Relevé sur la machine, la séquence de DKR :
 *
 *     0xFD100000 w1=0x00252D60   RGBA, 16 bits, adresse 0x252D60
 *     0xF5100000 w1=0x07080200   tuile 7
 *     0xF3000000 w1=0x077FF100   LoadBlock
 *     0xF5101000 w1=0x00080200   tuile 0
 *     0xF2000000 w1=0x0007C0FC   lrs=124, lrt=252 -> 32x64 texels
 *
 * On charge à `SETTILESIZE` parce que c'est la dernière des trois : avant elle
 * les dimensions sont inconnues, et charger sur `SETTIMG` donnerait une texture
 * de taille inventée. L'ordre est celui du microcode, pas une convention qu'on
 * choisit.
 *
 * La clé de cache réunit adresse, format, taille et dimensions. L'adresse seule
 * ne suffirait pas : DKR réemploie ses tampons, et deux textures différentes
 * peuvent partager une adresse d'une image à l'autre. Une clé trop courte ne
 * plante pas — elle affiche l'ancienne texture, ce qui se remarque tard. */
/* --- Ce que la Voodoo accepte, et ce que la N64 envoie ---------------------- *
 *
 * Le RDP échantillonne n'importe quelles dimensions ; la Voodoo exige des
 * **puissances de deux**, un côté d'au plus 256, et un rapport d'au plus 8:1.
 *
 * Mesuré sur la machine, une fois les quatre causes de refus séparées :
 *
 *     refus-detail: proportions=21084 taille=0 emplacements=0 memoire-tmu=0
 *
 * **Tous** les refus venaient de là, et aucun de la mémoire — ce qui a invalidé
 * la correction précédente, faite en supposant la saturation coupable. Séparer
 * les causes a coûté quatre entiers ; les confondre avait coûté un correctif.
 *
 * On remplit donc jusqu'à la puissance de deux supérieure et l'on retient le
 * rapport, dont les coordonnées de texture ont besoin : la texture réelle
 * n'occupe plus que le coin supérieur gauche.
 *
 * **Ce que le remplissage abîme, et qu'il vaut mieux dire** : une texture
 * répétée montrera son remplissage aux jointures, puisque l'enveloppement se
 * fait sur la taille remplie et non sur la taille réelle. DKR emploie
 * l'enveloppement dix-huit fois contre le bornage quatre fois, donc la question
 * se posera. La réponse propre est de répéter le motif dans le remplissage
 * plutôt que de le laisser vide ; c'est ce qui est fait ici. */
static int puissance_de_deux(int n)
{
    int p = 1;
    while (p < n && p < 256) { p <<= 1; }
    return p;
}

static void cmd_set_tile_size(dkr_f3d_context *c, unsigned int w0, unsigned int w1)
{
    const unsigned int lrs = (w1 >> 12) & 0xFFFu;
    const unsigned int lrt = w1 & 0xFFFu;
    const unsigned int uls = (w0 >> 12) & 0xFFFu;
    const unsigned int ult = w0 & 0xFFFu;
    /* 10.2 en virgule fixe, et les deux coins sont **inclus** — comme pour le
       rectangle plein, et pour la même raison de convention du RDP. */
    const int largeur = (int)((lrs >> 2) - (uls >> 2)) + 1;
    const int hauteur = (int)((lrt >> 2) - (ult >> 2)) + 1;
    unsigned long long cle;

    if (largeur <= 0 || hauteur <= 0) {
        return;
    }

    cle = ((unsigned long long)c->timg_address << 24)
        ^ ((unsigned long long)c->timg_format << 20)
        ^ ((unsigned long long)c->timg_size   << 18)
        ^ ((unsigned long long)largeur << 9)
        ^ (unsigned long long)hauteur;

    if (cle == c->texture_cle && c->render_state.texture != 0) {
        /* Déjà chargée et encore liée : rien à faire. Sans ce test on
           reconvertirait la même texture des milliers de fois par image, et sur
           un Pentium II cela seul suffirait à rendre le portage injouable. */
        c->state.textures_reutilisees++;
        return;
    }

    {
        const int pl = puissance_de_deux(largeur);
        const int ph = puissance_de_deux(hauteur);
        /* Le rapport d'au plus 8:1 de la carte. On ne peut pas remplir pour le
           satisfaire — cela reviendrait à multiplier la mémoire par huit — donc
           on refuse, et on le compte plutôt que de le taire. */
        const int grand = (pl > ph) ? pl : ph;
        const int petit = (pl > ph) ? ph : pl;
        if (grand > 256 || (petit > 0 && grand / petit > 8)) {
            c->render_state.texture = 0;
            c->texture_liee = 0;
            c->texture_cle = 0;
            c->state.textures_hors_proportions++;
            c->etat_sale = 1;
            return;
        }
        c->tex_largeur = largeur;
        c->tex_hauteur = hauteur;
        c->tex_largeur_remplie = pl;
        c->tex_hauteur_remplie = ph;
    }

    if (!dkr_texture_convert(c->rdram, c->rdram_size, c->rdram_native,
                             c->timg_address,
                             (dkr_n64_format)c->timg_format,
                             (dkr_n64_size)c->timg_size,
                             largeur, hauteur, c->texels, &c->state.textures)) {
        /* Refusée : on **délie** plutôt que de dessiner avec la précédente. Une
           texture périmée sur une surface est plus déroutante qu'une surface
           sans texture, parce qu'elle passe pour du rendu. */
        c->render_state.texture = 0;
        c->texture_liee = 0;
        c->texture_cle = 0;
        c->etat_sale = 1;
        return;
    }

    /* Le remplissage, en place et de bas en haut pour ne pas écraser ce qu'on
       recopie. Le motif est **répété** plutôt que laissé vide : c'est ce qui
       rend le remplissage invisible quand la texture est enveloppée, et cela ne
       coûte rien de plus qu'un remplissage nul. */
    if (c->tex_largeur_remplie != largeur || c->tex_hauteur_remplie != hauteur) {
        int y, x;
        for (y = c->tex_hauteur_remplie - 1; y >= 0; y--) {
            const int sy = y % hauteur;
            for (x = c->tex_largeur_remplie - 1; x >= 0; x--) {
                const int sx = x % largeur;
                c->texels[(size_t)y * (size_t)c->tex_largeur_remplie + (size_t)x] =
                    c->texels[(size_t)sy * (size_t)largeur + (size_t)sx];
            }
        }
        c->state.textures_remplies++;
    }

    if (c->backend && c->backend->texture_upload) {
        dkr_texture_desc d;
        dkr_texture_handle h;
        memset(&d, 0, sizeof(d));
        d.key = cle;
        d.format = DKR_TEXFMT_RGBA5551;
        d.width = c->tex_largeur_remplie;
        d.height = c->tex_hauteur_remplie;
        d.pixels = c->texels;
        d.size_bytes = (size_t)c->tex_largeur_remplie *
                       (size_t)c->tex_hauteur_remplie * 2u;
        h = c->backend->texture_upload(c->backend->self, &d);
        if (h != 0) {
            c->texture_liee = h;
            /* 1/32 pour le 10.5 du microcode, 1/largeur pour passer en [0,1].
               Les deux en une seule multiplication par sommet : la
               transformation est déjà le poste le plus lourd du portage. */
            c->tex_echelle_s = 1.0f / (32.0f * (float)c->tex_largeur_remplie);
            c->tex_echelle_t = 1.0f / (32.0f * (float)c->tex_hauteur_remplie);
            c->render_state.texture = h;
            c->texture_cle = cle;
            c->etat_sale = 1;
            c->state.textures_chargees++;
        } else {
            /* Mémoire de texture pleine. C'est E05-S02 qui l'administre ; ici on
               se contente de ne pas dessiner avec une poignée invalide. */
            c->render_state.texture = 0;
            c->texture_liee = 0;
            c->texture_cle = 0;
            c->state.textures_refusees++;
        }
    }

    trace(c, "SetTileSize %dx%d %s a 0x%06X", largeur, hauteur,
          dkr_texture_format_name((dkr_n64_format)c->timg_format,
                                  (dkr_n64_size)c->timg_size),
          c->timg_address);
}

/* --- La fenêtre d'affichage, celle du jeu et non celle qu'on suppose -------- *
 *
 * Jusqu'ici la fenêtre venait du défaut de `dkr_transform_init` — 640x480,
 * plausible et faux. Le symptôme mesuré : un unique triangle couvrant la moitié
 * de l'écran, alors que la géométrie et l'ombrage étaient corrects.
 *
 * `MOVEMEM` d'index `0x80` la porte, en seize octets. L'échantillon relevé sur
 * la machine :
 *
 *     opcode 0x03 w0=0x03800010 w1=0x000DD148
 *                    ^^ index   ^^^^ seize octets
 *
 * La structure est `short vscale[4]` puis `short vtrans[4]`, en virgule fixe
 * 2.2 — d'où la division par quatre. Les deux dernières composantes portent la
 * profondeur et ne servent pas ici : notre plage de profondeur est celle du
 * backend, établie par E05-S05.
 *
 * **Le signe en y s'inverse.** Le jeu donne une échelle positive ; la convention
 * de `dkr_transform` la veut négative, comme son propre défaut. S'en dispenser
 * retournerait l'image de haut en bas — visible, mais facile à attribuer à la
 * projection plutôt qu'à une convention de signe. */
static void cmd_viewport(dkr_f3d_context *c, unsigned int address)
{
    const short sx = read_s16(c, address + 0u);
    const short sy = read_s16(c, address + 2u);
    const short tx = read_s16(c, address + 8u);
    const short ty = read_s16(c, address + 10u);
    const float echelle = echelle_ecran(c);

    /* Une fenêtre nulle n'est pas une fenêtre : elle projetterait tous les
       sommets au même point, ce qui ressemble à une matrice fausse. On garde
       alors celle qu'on avait plutôt que d'en installer une inutilisable. */
    if (sx == 0 || sy == 0) {
        trace(c, "Viewport ignore : echelle nulle");
        return;
    }

    dkr_transform_set_viewport(&c->transform,
                               ((float)sx / 4.0f) * echelle,
                               -((float)sy / 4.0f) * echelle,
                               ((float)tx / 4.0f) * echelle,
                               ((float)ty / 4.0f) * echelle);
    c->state.viewports++;
    trace(c, "Viewport echelle=%d,%d translation=%d,%d (x%d/100)",
          sx / 4, sy / 4, tx / 4, ty / 4, (int)(echelle * 100.0f));
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
        echelle_x = echelle_ecran(c);
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

    /* Contrôle qui ne coûte rien et qui se déclenche tout seul : le RDP ne
       remplit qu'en mode `FILL`. Un décalage mal placé dans l'écriture du mot de
       mode se verrait ici, en chiffres, plutôt qu'à l'écran sous forme d'une
       surface d'une couleur inattendue. */
    {
        dkr_rdp_state verif;
        memset(&verif, 0, sizeof(verif));
        dkr_rdp_decode_othermode(c->mode_h, c->mode_l, &verif);
        if (verif.cycle != DKR_CYCLE_FILL) { c->state.fill_hors_cycle++; }
    }
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
    /* --- Ce qui termine une liste comptée --------------------------------- *
     *
     * Une liste comptée n'a **pas** d'`ENDDL` : c'est son compte qui la termine.
     * Le décodeur l'ignorait — il empilait l'adresse de retour, sautait, et
     * attendait un `ENDDL` qui ne viendrait jamais. Il sortait donc de la liste
     * par le bas et continuait dans la mémoire qui suit, jusqu'à buter sur du
     * hasard.
     *
     * Le symptôme, mesuré sur la machine : **soixante-dix commandes par liste,
     * constant, deux remplissages et pas un triangle**, et un rejet par image.
     * La liste d'affichage de DKR charge une texture par une liste comptée de
     * sept commandes, et toute la géométrie vient *après* ce retour. Elle était
     * perdue là, à chaque image, depuis le début.
     *
     * `SANS_COMPTE` distingue « jusqu'à `ENDDL` » de « plus une seule commande ».
     * Sans ce sentinelle, zéro voudrait dire les deux, et une liste ordinaire se
     * terminerait à sa première commande. */
    unsigned int reste_stack[MAX_NESTED];
    unsigned int reste = SANS_COMPTE;
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
        /* Decompte avant d'executer, pour que la valeur empilee par un appel
           imbrique soit celle du parent **apres** cette commande. La decrementer
           apres la ferait recompter au retour. */
        if (reste != SANS_COMPTE && reste > 0u) { reste--; }

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
                reste_stack[depth] = reste;
                return_stack[depth++] = address;
                reste = SANS_COMPTE;   /* une liste appelee va jusqu'a son ENDDL */
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
                reste = reste_stack[depth];
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
            reste_stack[depth] = reste;
            return_stack[depth++] = address;
            reste = count;
            trace(c, "CountedDisplayList %u commandes a 0x%06X", count, target);
            address = target;
            break;
        }

        case OP_FILLRECT:
            cmd_fill_rect(c, w0, w1);
            break;

        case OP_RDPSETOTHERMODE:
            /* --- Le mot de mode, écrit en entier ------------------------------ *
             *
             * `SETOTHERMODE_H` et `_L` sont des écritures **partielles** ;
             * celle-ci remplace les deux moitiés d'un coup. Elle était enjambée,
             * et le mode restait donc figé sur le dernier réglage partiel — en
             * pratique celui des remplissages plein écran, c'est-à-dire le mode
             * de cycle FILL.
             *
             * Le symptôme n'accusait rien : les 32 411 configurations de
             * combineur du jeu étaient toutes enregistrées en cycle FILL, donc
             * aucune ne pouvait correspondre à la table — le mode de cycle fait
             * partie de la clé. On aurait conclu que la table était incomplète
             * et on l'aurait enrichie de configurations qui n'auraient rien
             * reconnu non plus.
             *
             * L'histogramme des opcodes portait la réponse depuis le début :
             * `EF:1798`, mille sept cent quatre-vingt-dix-huit fois par course,
             * dans les huit premiers. Il était enjambé au même titre que les
             * synchronisations, faute d'avoir regardé ce qu'il faisait.
             *
             * La moitié haute ne tient que sur vingt-quatre bits — c'est ce que
             * la commande transporte, le reste du mot n'existant pas côté RDP. */
            c->mode_h = w0 & 0x00FFFFFFu;
            c->mode_l = w1;
            c->etat_sale = 1;
            trace(c, "SetOtherMode entier h=0x%06X l=0x%08X",
                  c->mode_h, c->mode_l);
            break;

        case OP_SETOTHERMODE_H:
            ecrire_othermode(&c->mode_h, w0, w1);
            c->etat_sale = 1;
            break;

        case OP_SETOTHERMODE_L:
            ecrire_othermode(&c->mode_l, w0, w1);
            c->etat_sale = 1;
            break;

        case OP_SETCOMBINE:
            dkr_rdp_decode_combine(w0, w1, &c->combiner);
            c->etat_sale = 1;
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

        case OP_SETTEXIMAGE:
            c->timg_format  = (w0 >> 21) & 0x07u;
            c->timg_size    = (w0 >> 19) & 0x03u;
            c->timg_address = w1 & RDRAM_MASK;
            trace(c, "SetTextureImage %s a 0x%06X",
                  dkr_texture_format_name((dkr_n64_format)c->timg_format,
                                          (dkr_n64_size)c->timg_size),
                  c->timg_address);
            break;

        case OP_SETTILESIZE:
            cmd_set_tile_size(c, w0, w1);
            break;

        case OP_MOVEMEM: {
            const unsigned int index = (w0 >> 16) & 0xFFu;
            const unsigned int taille = w0 & 0xFFFFu;
            const unsigned int source = w1 & RDRAM_MASK;
            if (index == MOVEMEM_VIEWPORT && taille >= VIEWPORT_BYTES &&
                in_range(c, source, VIEWPORT_BYTES)) {
                cmd_viewport(c, source);
            } else {
                trace(c, "MoveMem index=0x%02X taille=%u a 0x%06X",
                      index, taille, source);
            }
            break;
        }

        case OP_LOADBLOCK:
            /* `LOADBLOCK` copie la texture de la RDRAM vers la memoire de
               texture du RDP. Ce portage lit directement en RDRAM — le raccourci
               assume et documente en tete de `texture.h` — donc la copie n'a
               rien a faire ici. La commande reste decodee pour que la sequence
               apparaisse dans la trace. */
            trace(c, "LoadBlock w0=0x%08X w1=0x%08X", w0, w1);
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

        /* Le compte est epuise : on revient, sans attendre d'ENDDL. C'est le
           seul terminateur d'une liste comptee, et l'oublier faisait sortir le
           decodeur par le bas de la liste dans la memoire qui suit. */
        if (running && reste == 0u && depth > 0u) {
            address = return_stack[--depth];
            reste = reste_stack[depth];
            trace(c, "CountedDisplayList terminee — retour a 0x%06X", address);
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
    /* Une échelle non nulle par défaut : sans texture liée les coordonnées ne
       servent pas, mais zéro les écraserait toutes sur un point, ce qui
       ressemblerait à un défaut de transformation plutôt qu'à une absence. */
    ctx->tex_echelle_s = 1.0f / 32.0f;
    ctx->tex_echelle_t = 1.0f / 32.0f;
    ctx->state.s_min = 1.0e30f;
    ctx->state.t_min = 1.0e30f;
    ctx->state.s_max = -1.0e30f;
    ctx->state.oow_min = 1.0e30f;
    ctx->state.oow_max = -1.0e30f;
    ctx->state.t_max = -1.0e30f;
    ctx->rdram      = rdram;
    ctx->rdram_size = rdram_size;
    ctx->backend    = backend;
    dkr_transform_init(&ctx->transform);
}
