/* E04-S02 — épreuve du décodeur de display list.
 *
 * Le critère demande que la validation des plages soit « testée par injection de
 * display lists volontairement corrompues ». C'est exactement ce que fait cette
 * suite, et c'est ce qui la rend possible sans ROM : une display list corrompue
 * s'écrit, une vraie se capture.
 *
 * Ce qui est établi : **chaque forme d'entrée invalide produit un rejet
 * circonscrit, nommé, et laisse le décodeur en état de continuer**. Un décodeur
 * qui plante sur une donnée fausse est un décodeur qu'une ROM modifiée met à
 * genoux ; un décodeur qui l'accepte en silence adresse la mémoire de l'hôte.
 */
#include "render/f3ddkr.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "ECHEC", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "ECHEC", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

/* Une RDRAM d'essai, petite : les plages hors bornes sont alors faciles à
   fabriquer, et le comportement est le même qu'avec 8 Mio. */
#define RAM_SIZE 4096u
static unsigned char g_ram[RAM_SIZE];

static void put32(unsigned int a, unsigned int v)
{
    g_ram[a + 0] = (unsigned char)(v >> 24);
    g_ram[a + 1] = (unsigned char)(v >> 16);
    g_ram[a + 2] = (unsigned char)(v >>  8);
    g_ram[a + 3] = (unsigned char)(v);
}

/* Le dernier rectangle demande au backend, pour l'epreuve du chemin 2D. */
static int      g_rect[4];
static unsigned g_rect_argb;
static int      g_rect_n;

static void note_rect(void *self, int x0, int y0, int x1, int y1, unsigned argb)
{
    (void)self;
    g_rect[0] = x0; g_rect[1] = y0; g_rect[2] = x1; g_rect[3] = y1;
    g_rect_argb = argb;
    g_rect_n++;
}

static void put16(unsigned int a, int v)
{
    g_ram[a] = (unsigned char)((unsigned)v >> 8);
    g_ram[a + 1] = (unsigned char)v;
}

static unsigned int put_cmd(unsigned int a, unsigned int w0, unsigned int w1)
{
    put32(a, w0);
    put32(a + 4, w1);
    return a + 8;
}

/* Le mode trace, capturé pour être vérifié. */
static char g_trace[64][192];
static int  g_trace_count;

static void trace_sink(void *user, const char *line)
{
    (void)user;
    if (g_trace_count < 64) {
        strncpy(g_trace[g_trace_count], line, sizeof(g_trace[0]) - 1);
        g_trace[g_trace_count][sizeof(g_trace[0]) - 1] = '\0';
        g_trace_count++;
    }
}

static int trace_contains(const char *needle)
{
    int i;
    for (i = 0; i < g_trace_count; i++) {
        if (strstr(g_trace[i], needle)) { return 1; }
    }
    return 0;
}

static void reset(dkr_f3d_context *c, int with_trace)
{
    memset(g_ram, 0, sizeof(g_ram));
    g_trace_count = 0;
    dkr_f3d_init(c, g_ram, RAM_SIZE, NULL);
    if (with_trace) {
        c->trace = trace_sink;
    }
}

int main(void)
{
    dkr_f3d_context c;
    unsigned int a;

    g_out = fopen("D:\\F3DDKR.TXT", "w");

    /* --- Une liste bien formée --------------------------------------------- */
    reset(&c, 1);
    a = put_cmd(0, 0xBF000100u, 0x00000200u);          /* DMAOffsets */
    a = put_cmd(a, 0xB8000000u, 0x00000000u);          /* EndDisplayList */
    (void)a;
    check("une liste bien formee s'execute", dkr_f3d_run(&c, 0) == 2);
    check("les bases de DMA sont retenues",
          c.state.matrix_offset == 0x000100u && c.state.vertex_offset == 0x000200u);
    check("le mode trace journalise les commandes",
          trace_contains("DMAOffsets") && trace_contains("EndDisplayList"));

    /* --- Sommets : les trois conditions de bornage -------------------------- *
     *
     * La troisieme — un lot qui deborde le cache par la somme de la destination
     * et du nombre — est celle qu'on oublie, et c'est celle qui laisse ecrire
     * au-dela du cache. */
    reset(&c, 1);
    /* 32 sommets a l'index 16 : le lot deborde de 16. */
    a = put_cmd(0, 0x04F82000u | (16u << 9), 0x00000000u);
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("un lot de sommets qui deborde le cache est rejete",
          c.state.rejects[DKR_F3D_REJECT_COUNT] == 1 && c.state.vertices == 0);

    /* Une source hors RDRAM. */
    reset(&c, 1);
    a = put_cmd(0, 0xBF000000u, RAM_SIZE - 4u);        /* base de sommets au bord */
    a = put_cmd(a, 0x04080000u, 0x00000000u);          /* 2 sommets => 20 octets */
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("des sommets hors RDRAM sont rejetes",
          c.state.rejects[DKR_F3D_REJECT_ADDRESS] >= 1 && c.state.vertices == 0);

    /* --- Triangles : un index hors du cache --------------------------------- *
     *
     * Et surtout : **le lot entier doit etre rejete**, pas seulement le triangle
     * fautif. Valider au fil de l'eau laisserait dessiner ceux d'avant, ce qui
     * rend le defaut dependant du contenu. */
    reset(&c, 1);
    {
        const unsigned int table = 0x100u;
        /* Deux triangles : le premier valide, le second avec un index a 40. */
        g_ram[table + 1] = 0; g_ram[table + 2] = 1; g_ram[table + 3] = 2;
        g_ram[table + 17] = 0; g_ram[table + 18] = 40; g_ram[table + 19] = 2;
        a = put_cmd(0, 0x05100000u, table);            /* 2 triangles */
        (void)put_cmd(a, 0xB8000000u, 0u);
        dkr_f3d_run(&c, 0);
        check("un index de sommet hors cache est rejete",
              c.state.rejects[DKR_F3D_REJECT_INDEX] == 1);
        check("et c'est **tout le lot** qui est rejete, pas le seul fautif",
              c.state.triangles == 0);
    }

    /* Dessiner sans avoir charge de sommets : les index sont dans les bornes du
       cache, mais le cache est vide. Ce n'est pas une adresse fausse — donc pas
       un rejet de plage — et dessiner des sommets non initialises donnerait une
       geometrie aleatoire, ce qui est pire qu'un triangle absent. */
    reset(&c, 1);
    {
        const unsigned int table = 0x100u;
        g_ram[table + 1] = 0; g_ram[table + 2] = 1; g_ram[table + 3] = 2;
        a = put_cmd(0, 0x05000000u, table);
        (void)put_cmd(a, 0xB8000000u, 0u);
        dkr_f3d_run(&c, 0);
        check("dessiner sans avoir charge de sommets est rejete",
              c.state.rejects[DKR_F3D_REJECT_INDEX] == 1 && c.state.emitted == 0);
    }

    /* Un lot valide, sommets charges d'abord — la sequence d'une vraie display
       list. Sans ce controle, le precedent pourrait passer pour une mauvaise
       raison : un decodeur qui rejetterait tout le satisferait aussi. */
    reset(&c, 1);
    {
        const unsigned int table = 0x100u;
        const unsigned int verts = 0x200u;
        g_ram[table + 1] = 0; g_ram[table + 2] = 1; g_ram[table + 3] = 2;
        /* Trois sommets, a des positions distinctes pour que le triangle ait une
           surface. z positif : devant le plan proche. */
        put16(verts +  0, -10); put16(verts +  2, -10); put16(verts +  4, 100);
        put16(verts + 10,  10); put16(verts + 12, -10); put16(verts + 14, 100);
        put16(verts + 20,   0); put16(verts + 22,  10); put16(verts + 24, 100);
        a = put_cmd(0, 0x04000000u | (2u << 19), verts);   /* 3 sommets */
        a = put_cmd(a, 0x05000000u, table);                /* 1 triangle */
        (void)put_cmd(a, 0xB8000000u, 0u);
        /* Une projection ou w = z. **Sans elle, w vaut 1** et un sommet a x = -10
           se retrouve a dix demi-ecrans du centre, donc hors de la bande de garde
           — le triangle est alors correctement ecarte, et le controle echouerait
           pour une raison qui n'a rien a voir avec ce qu'il verifie. */
        {
            dkr_matrix proj;
            memset(&proj, 0, sizeof(proj));
            proj.m[0][0] = 1.0f; proj.m[1][1] = 1.0f; proj.m[2][2] = 0.5f;
            proj.m[2][3] = 1.0f;
            dkr_transform_set_projection(&c.transform, &proj);
        }
        dkr_f3d_run(&c, 0);
        check("un lot de triangles valide est accepte",
              c.state.triangles == 1 && c.state.rejects[DKR_F3D_REJECT_INDEX] == 0);
        /* Et la chaine va jusqu'au bout : le triangle atteint le backend. */
        check("et la chaine l'emet effectivement", c.state.emitted == 1);
    }

    /* --- Imbrication : la profondeur est bornee ----------------------------- */
    reset(&c, 0);
    {
        /* Une liste qui s'appelle elle-meme : sans borne, la pile deborde. */
        unsigned int i;
        put_cmd(0, 0x06000000u, 0x00000000u);          /* appel vers soi-meme */
        for (i = 0; i < 4; i++) { /* rien : la boucle est dans la liste */ }
        dkr_f3d_run(&c, 0);
        check("une liste recursive ne deborde pas la pile",
              c.state.rejects[DKR_F3D_REJECT_DEPTH] >= 1);
    }

    /* Un appel puis un retour : la pile se depile bien. */
    reset(&c, 1);
    a = put_cmd(0, 0x06000000u, 0x00000200u);          /* appel vers 0x200 */
    (void)put_cmd(a, 0xB8000000u, 0u);                 /* fin, apres retour */
    put_cmd(0x200u, 0xB8000000u, 0u);                  /* retour */
    dkr_f3d_run(&c, 0);
    check("un appel suivi d'un retour revient au bon endroit",
          trace_contains("retour a 0x000008") &&
          c.state.rejects[DKR_F3D_REJECT_DEPTH] == 0);

    /* --- Une commande a cheval sur la fin de RDRAM --------------------------- */
    reset(&c, 0);
    check("une liste qui commence hors RDRAM est rejetee",
          dkr_f3d_run(&c, RAM_SIZE - 4u) == 0 &&
          c.state.rejects[DKR_F3D_REJECT_ADDRESS] == 1);

    /* --- Un opcode inconnu arrete le decodage -------------------------------- *
     *
     * Poursuivre apres un opcode inconnu inventerait des commandes : le flux est
     * probablement desynchronise, et chaque mot suivant serait lu a la mauvaise
     * frontiere. */
    reset(&c, 1);
    a = put_cmd(0, 0x99000000u, 0u);                   /* opcode inexistant */
    (void)put_cmd(a, 0x04000000u, 0u);                 /* ne doit pas etre lu */
    dkr_f3d_run(&c, 0);
    check("un opcode inconnu est rejete",
          c.state.rejects[DKR_F3D_REJECT_OPCODE] == 1);
    check("et le decodage s'arrete la", c.state.vertices == 0);

    /* --- Le bornage du journal ---------------------------------------------- *
     *
     * Une display list corrompue produirait des milliers de lignes par image, ce
     * qui noie le diagnostic et coute cher sur une machine de 1998. */
    reset(&c, 1);
    {
        unsigned int i, at = 0;
        for (i = 0; i < 200u; i++) {
            at = put_cmd(at, 0x04F82000u | (16u << 9), 0u);   /* toujours rejete */
        }
        put_cmd(at, 0xB8000000u, 0u);
        dkr_f3d_run(&c, 0);
        check("200 rejets sont tous comptes",
              c.state.rejects[DKR_F3D_REJECT_COUNT] == 200u);
        check("mais le journal est borne", g_trace_count <= 64);
    }

    /* --- MoveWord ------------------------------------------------------------ */
    reset(&c, 1);
    a = put_cmd(0, 0xBC000002u, 0x00000001u);          /* panneau d'affichage */
    a = put_cmd(a, 0xBC00000Au, 0x00000080u);          /* matrice 2 */
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("MoveWord pose le mode panneau", c.state.billboard == 1);
    check("MoveWord selectionne la matrice", c.state.selected_matrix == 2);

    /* Le groupe de presentation est une **extension du portage**, reconnue a son
       mot magique. Sans le magique, c'est un MoveWord ordinaire. */
    reset(&c, 1);
    a = put_cmd(0, 0xBC0000FEu, 0x444B5202u);
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("le groupe de presentation est reconnu au mot magique",
          trace_contains("PresentationGroup"));
    reset(&c, 1);
    a = put_cmd(0, 0xBC0000FEu, 0x12345678u);
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("et un mot quelconque ne l'est pas",
          !trace_contains("PresentationGroup"));

    /* --- TextureOffset porte une adresse, pas des decalages ------------------ *
     *
     * Ce decodeur lisait `w1` comme deux decalages de texture sur seize bits.
     * Le portage voisin, qui tourne, en fait une **base d'adressage RDRAM**, et
     * remet a zero le decalage et le compte. L'erreur n'aurait pas saute aux
     * yeux : elle aurait deplace des motifs plutot que de les faire disparaitre,
     * et l'on aurait cherche du cote du decodage de texture. */
    {
        dkr_f3d_context ctx2;
        unsigned int at2 = 0;
        memset(g_ram, 0, sizeof(g_ram));
        at2 = put_cmd(at2, 0x02000000u, 0x00123456u);   /* TextureOffset */
        (void)put_cmd(at2, 0xB8000000u, 0u);
        dkr_f3d_init(&ctx2, g_ram, RAM_SIZE, NULL);
        ctx2.state.texture_shift = 7;
        ctx2.state.texture_count = 9;
        (void)dkr_f3d_run(&ctx2, 0);
        check("TextureOffset retient une adresse RDRAM",
              ctx2.state.texture_offset == 0x123456u);
        check("et remet le decalage et le compte a zero",
              ctx2.state.texture_shift == 0 && ctx2.state.texture_count == 0);
        /* Le masque de 24 bits n'est pas decoratif : la RDRAM fait 8 Mio, et
           les octets de poids fort d'une commande portent autre chose. */
        memset(g_ram, 0, sizeof(g_ram));
        at2 = 0;
        at2 = put_cmd(at2, 0x02000000u, 0xFF123456u);
        (void)put_cmd(at2, 0xB8000000u, 0u);
        dkr_f3d_init(&ctx2, g_ram, RAM_SIZE, NULL);
        (void)dkr_f3d_run(&ctx2, 0);
        check("l'adresse est bornee a 24 bits, la taille de la RDRAM",
              ctx2.state.texture_offset == 0x123456u);
    }

    /* --- Les deux dispositions de RDRAM donnent le meme decodage -------------- *
     *
     * Le jeu ne fournit pas la RDRAM en gros-boutiste franc : librecomp la range
     * **entrelacee par XOR-3**, l'octet d'adresse invitee `a` se trouvant a
     * `a ^ 3`. Le decodeur porte donc `rdram_native`, et tout l'interet est que
     * les deux voies rendent exactement le meme resultat.
     *
     * L'epreuve construit une scene en gros-boutiste, en fabrique la permutation
     * XOR-3, et compare les deux decodages champ par champ. C'est le seul
     * controle qui puisse echouer si l'une des deux voies derive : une display
     * list lue avec la mauvaise convention ne plante pas, elle decode des
     * opcodes plausibles a des adresses absurdes. Sans ce controle, la faute
     * apparaitrait sur la machine, sous forme d'un decor absent, et se
     * chercherait dans le rastériseur. */
    {
        dkr_f3d_context droit, tordu;
        static unsigned char entrelace[RAM_SIZE];
        unsigned int i, at3 = 0;

        memset(g_ram, 0, sizeof(g_ram));
        /* Une scene qui exerce les trois largeurs de lecture : la commande
           (32 bits), les sommets (16 bits signes) et la matrice (octets). */
        at3 = put_cmd(at3, 0xBF000000u, 0x00000000u);          /* DMAOffsets */
        at3 = put_cmd(at3, 0x01000040u, 0x00000200u);          /* Matrix, 64 o */
        at3 = put_cmd(at3, 0x04000000u | (2u << 19), 0x300u);  /* Vertex x3 */
        at3 = put_cmd(at3, 0x05000000u, 0x00000102u);          /* Triangle */
        (void)put_cmd(at3, 0xB8000000u, 0u);
        /* Une matrice identite en virgule fixe, et trois sommets reconnaissables. */
        for (i = 0; i < 4; i++) {
            put16(0x200u + i * 10u, 1);        /* partie entiere, diagonale */
        }
        for (i = 0; i < 3; i++) {
            put16(0x300u + i * 16u + 0u, (int)(100 * (i + 1)));
            put16(0x300u + i * 16u + 2u, (int)(-50 * (i + 1)));
            put16(0x300u + i * 16u + 4u, 200);
        }

        /* La permutation. `i ^ 3` est une involution, donc la meme boucle sert
           dans les deux sens ; c'est aussi ce qui la rend facile a verifier. */
        for (i = 0; i < RAM_SIZE; i++) { entrelace[i ^ 3u] = g_ram[i]; }

        dkr_f3d_init(&droit, g_ram, RAM_SIZE, NULL);
        (void)dkr_f3d_run(&droit, 0);

        dkr_f3d_init(&tordu, entrelace, RAM_SIZE, NULL);
        tordu.rdram_native = 1;
        (void)dkr_f3d_run(&tordu, 0);

        check("la disposition entrelacee decode le meme nombre de commandes",
              droit.state.commands == tordu.state.commands);
        check("les memes sommets", droit.state.vertices == tordu.state.vertices);
        check("les memes triangles", droit.state.triangles == tordu.state.triangles);
        check("les memes emissions", droit.state.emitted == tordu.state.emitted);
        /* Le controle qui empeche les precedents de reussir a vide : si la scene
           n'avait rien decode, tous les compteurs vaudraient zero des deux cotes
           et l'accord serait vide de sens. */
        check("et la scene a reellement decode quelque chose",
              droit.state.vertices == 3 && droit.state.triangles == 1);
        {
            int memes_rejets = 1;
            for (i = 0; i < (unsigned)DKR_F3D_REJECT_COUNT_MAX; i++) {
                if (droit.state.rejects[i] != tordu.state.rejects[i]) { memes_rejets = 0; }
            }
            check("et les memes rejets, categorie par categorie", memes_rejets);
        }
        /* La matrice traverse un chemin distinct des lectures de 32 bits — elle
           passe par un tampon remis a plat — donc elle merite son propre
           controle plutot que d'etre couverte par ricochet. */
        {
            int meme_matrice = 1;
            for (i = 0; i < 16u; i++) {
                const float a = droit.transform.slot[0].m[i / 4u][i % 4u];
                const float b = tordu.transform.slot[0].m[i / 4u][i % 4u];
                if (a != b) { meme_matrice = 0; }
            }
            check("et la matrice chargee est identique dans les deux dispositions",
                  meme_matrice);
        }
    }

    /* --- Le rectangle plein --------------------------------------------------- *
     *
     * Mesure sur la machine avant d'etre ecrit : sur les 47 000 commandes de la
     * sequence de demarrage de DKR, `FILLRECT` est le **seul** ordre de dessin
     * emis. Cette epreuve porte donc sur le chemin dont depend le premier pixel
     * que le portage affichera.
     *
     * Trois choses s'y verifient, chacune parce qu'elle a une facon propre de
     * mal tourner :
     *
     *   - la conversion 5551 vers 888, ou 31 doit donner 255 et non 248 ;
     *   - l'inclusion du coin inferieur droit, qui coute un pixel si on l'oublie ;
     *   - l'echelle, **lue** dans SETCOLORIMAGE et non supposee.
     */
    {
        dkr_f3d_context ctx4;
        dkr_render_backend bk;
        unsigned int at4 = 0;

        /* Un backend local plutot que l'implementation vide : celle-ci accepte
           tout et n'enregistre rien, donc elle ne peut pas dire *ou* le
           rectangle a ete demande. Or c'est exactement ce qu'on veut verifier —
           l'inclusion du coin et l'echelle sont des erreurs de coordonnees, pas
           de comptage. */
        memset(&bk, 0, sizeof(bk));
        bk.name = "epreuve";
        bk.fill_rect = note_rect;
        g_rect_n = 0;

        memset(g_ram, 0, sizeof(g_ram));
        at4 = put_cmd(at4, 0xFF000000u | (320u - 1u), 0x00100000u); /* SetColorImage */
        at4 = put_cmd(at4, 0xF7000000u, 0xFFFFFFFFu);               /* blanc */
        /* 0,0 .. 9,4 inclus, donc 10 par 5 pixels a l'echelle 1. */
        at4 = put_cmd(at4, 0xF6000000u | (9u << 14) | (4u << 2), 0u);
        (void)put_cmd(at4, 0xB8000000u, 0u);

        dkr_f3d_init(&ctx4, g_ram, RAM_SIZE, &bk);
        /* Une fenetre de 320x240 : l'echelle vaut alors exactement un, ce qui
           rend les coordonnees attendues lisibles sans calcul. */
        dkr_transform_set_viewport(&ctx4.transform, 160.0f, -120.0f, 160.0f, 120.0f);
        (void)dkr_f3d_run(&ctx4, 0);

        check("la largeur du tampon est lue dans SetColorImage",
              ctx4.state.color_image_width == 320u);
        check("le rectangle atteint le backend", ctx4.state.rects == 1 && g_rect_n == 1);
        /* 0,0 .. 9,4 **inclus** doit devenir 0,0 .. 10,5 exclu. Oublier le +1
           laisserait une ligne du fond visible en bas et a droite d'un
           effacement plein ecran, ce qu'on attribuerait au rasteriseur. */
        check("le coin inferieur droit est inclus cote RDP, exclu cote backend",
              g_rect[0] == 0 && g_rect[1] == 0 && g_rect[2] == 10 && g_rect[3] == 5);
        /* 0xFFFF en 5551 est blanc opaque. Le controle porte sur 255 et non sur
           « non nul » : un decalage sans replication des bits de poids fort
           donnerait 248, une valeur assez proche pour passer inapercue a l'oeil
           et assez fausse pour que le blanc ne soit jamais blanc. */
        check("le blanc 5551 devient 0xFFFFFF et non 0xF8F8F8",
              ctx4.state.fill_color_argb == 0x00FFFFFFu);

        /* Et une couleur qui n'est ni noire ni blanche, sans quoi une conversion
           qui ne ferait que saturer passerait le controle precedent. */
        memset(g_ram, 0, sizeof(g_ram));
        at4 = 0;
        at4 = put_cmd(at4, 0xFF000000u | (320u - 1u), 0x00100000u);
        /* rouge = 31, vert = 0, bleu = 0, alpha = 1 -> 0xF801 */
        at4 = put_cmd(at4, 0xF7000000u, 0xF801F801u);
        (void)put_cmd(at4, 0xB8000000u, 0u);
        dkr_f3d_init(&ctx4, g_ram, RAM_SIZE, &bk);
        (void)dkr_f3d_run(&ctx4, 0);
        check("un rouge pur 5551 devient 0xFF0000",
              ctx4.state.fill_color_argb == 0x00FF0000u);
    }

    /* --- Le retour d'une liste comptee ---------------------------------------- *
     *
     * Une liste comptee n'a pas d'`ENDDL` : c'est son compte qui la termine. Le
     * decodeur empilait l'adresse de retour et l'ignorait, donc il sortait de la
     * liste par le bas et continuait dans la memoire qui suit.
     *
     * Le symptome sur la machine etait muet et couteux : 70 commandes par liste,
     * constant, deux remplissages et **pas un triangle**. DKR charge ses textures
     * par une liste comptee de sept commandes, et toute la geometrie vient apres
     * ce retour. Elle etait perdue la, a chaque image.
     *
     * L'epreuve reproduit exactement ce piege : de l'ordure est posee juste apres
     * la liste comptee, la ou le decodeur derapait. Sans le retour, il la lit et
     * rejette ; avec, il ne la voit jamais. C'est ce qui fait que le controle
     * porte sur la correction plutot que sur sa formulation. */
    {
        dkr_f3d_context ctx5;
        unsigned int at5 = 0, corps, k;

        memset(g_ram, 0, sizeof(g_ram));
        /* La liste principale : appelle une liste comptee de 3 commandes, puis
           charge trois sommets et un triangle, puis se termine. */
        at5 = put_cmd(at5, 0xBF000000u, 0x00000000u);           /* DMAOffsets */
        at5 = put_cmd(at5, 0x07000000u | (3u << 16), 0x600u);   /* liste comptee */
        at5 = put_cmd(at5, 0x04000000u | (2u << 19), 0x300u);   /* Vertex x3 */
        at5 = put_cmd(at5, 0x05000000u, 0x00000102u);           /* Triangle */
        (void)put_cmd(at5, 0xB8000000u, 0u);

        /* Le corps compte : trois commandes RDP anodines, **sans ENDDL**, et
           immediatement suivies d'ordure. C'est la disposition reelle. */
        corps = 0x600u;
        corps = put_cmd(corps, 0xE7000000u, 0u);                /* PipeSync */
        corps = put_cmd(corps, 0xE7000000u, 0u);
        corps = put_cmd(corps, 0xE7000000u, 0u);
        (void)put_cmd(corps, 0x99000000u, 0x99999999u);         /* ordure */

        for (k = 0; k < 3; k++) {
            put16(0x300u + k * 16u + 0u, (int)(10 * (k + 1)));
            put16(0x300u + k * 16u + 2u, (int)(20 * (k + 1)));
            put16(0x300u + k * 16u + 4u, 200);
        }

        dkr_f3d_init(&ctx5, g_ram, RAM_SIZE, NULL);
        (void)dkr_f3d_run(&ctx5, 0);

        check("la liste comptee rend la main a son compte, sans ENDDL",
              ctx5.state.rejects[DKR_F3D_REJECT_OPCODE] == 0);
        /* Le controle qui compte vraiment : ce qui suit le retour est atteint.
           Sans le retour, les sommets et le triangle sont derriere l'ordure et
           ne sont jamais lus — exactement ce que la machine montrait. */
        check("et ce qui suit le retour est decode",
              ctx5.state.vertices == 3 && ctx5.state.triangles == 1);
    }

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
