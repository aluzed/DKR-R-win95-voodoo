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
#define OP_SETTEXIMAGE   0xFD

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

static unsigned char read_u8(const dkr_f3d_context *c, unsigned int a)
{
    return c->rdram[a];
}

static short read_s16(const dkr_f3d_context *c, unsigned int a)
{
    return (short)(((unsigned)c->rdram[a] << 8) | c->rdram[a + 1]);
}

static unsigned int read_u32(const dkr_f3d_context *c, unsigned int a)
{
    return ((unsigned)c->rdram[a]     << 24) | ((unsigned)c->rdram[a + 1] << 16) |
           ((unsigned)c->rdram[a + 2] <<  8) |  (unsigned)c->rdram[a + 3];
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
        /* Le sommet DKR : x, y, z en 16 bits signes puis r, g, b, a en octets.
           **Aucune coordonnee de texture** — elles arrivent au triangle. */
        (void)read_s16(c, a + 0);
        (void)read_s16(c, a + 2);
        (void)read_s16(c, a + 4);
        (void)read_u8(c, a + 6);
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

    /* Le dessin viendra quand E04-S03 aura projete les sommets. Emettre ici des
       sommets en espace objet donnerait une image fausse plutot qu'une image
       absente, ce qui est pire : on croirait le chemin complet. */
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
        trace(c, "MoveWord matrice=%u", m);
    } else {
        trace(c, "MoveWord type=0x%02X valeur=0x%08X", type, w1);
    }
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

        switch (opcode) {
        case OP_DMAOFFSETS: cmd_dma_offsets(c, w0, w1); break;
        case OP_MATRIX:     cmd_matrix(c, w0, w1);      break;
        case OP_VERTEX:     cmd_vertex(c, w0, w1);      break;
        case OP_TRIANGLE:   cmd_triangle(c, w0, w1);    break;
        case OP_MOVEWORD:   cmd_move_word(c, w0, w1);   break;

        case OP_TEXOFFSET:
            c->state.texture_offset_s = (w1 >> 16) & 0xFFFFu;
            c->state.texture_offset_t =  w1        & 0xFFFFu;
            trace(c, "TextureOffset s=%u t=%u",
                  c->state.texture_offset_s, c->state.texture_offset_t);
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

        case OP_MOVEMEM:
        case OP_LOADBLOCK:
        case OP_FILLRECT:
        case OP_SETTEXIMAGE:
            /* Decodees comme commandes, mais leur effet appartient aux etages
               suivants — textures (E04-S07) et etat RDP (E04-S06). Les compter
               ici etablit deja que la sequence est juste. */
            trace(c, "opcode 0x%02X w0=0x%08X w1=0x%08X", opcode, w0, w1);
            break;

        default: {
            char d[48];
            sprintf(d, "0x%02X a 0x%06X", opcode, address - 8u);
            reject(c, DKR_F3D_REJECT_OPCODE, d);
            /* On s'arrete : apres un opcode inconnu, le flux est probablement
               desynchronise et poursuivre inventerait des commandes. */
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
}
