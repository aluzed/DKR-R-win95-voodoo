/* E04-S07 — épreuve du décodage des formats de texture.
 *
 * Ce qui s'y vérifie n'est pas « la conversion tourne » mais **les trois façons
 * dont elle peut tromper** :
 *
 *   - une réplication de bits oubliée, qui rend le blanc presque blanc ;
 *   - un format non pris en charge servi quand même, qui produit des couleurs
 *     arbitraires passant pour du rendu ;
 *   - une lecture hors bornes, qui n'échoue jamais bruyamment.
 *
 * Les deux dispositions de RDRAM sont éprouvées, comme pour le décodeur de
 * display list : le jeu fournit la forme entrelacée par XOR-3, les épreuves
 * construisent la forme droite, et les confondre donne des textures brouillées
 * plutôt qu'une erreur.
 */
#include "render/texture.h"

#include <stdio.h>
#include <string.h>

static int g_fails;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "ECHEC", what);
    if (!condition) { g_fails++; }
}

#define RAM 4096u
static unsigned char g_ram[RAM];
static unsigned char g_ram_x[RAM];
static unsigned short g_out[64 * 64];

static void ecrire16(unsigned int a, unsigned int v)
{
    g_ram[a] = (unsigned char)(v >> 8);
    g_ram[a + 1] = (unsigned char)v;
}

/* La permutation de librecomp. `i ^ 3` est une involution, donc la meme boucle
   sert dans les deux sens — ce qui la rend facile a verifier. */
static void entrelacer(void)
{
    unsigned int i;
    for (i = 0; i < RAM; i++) { g_ram_x[i ^ 3u] = g_ram[i]; }
}

int main(void)
{
    dkr_texture_stats st;
    memset(&st, 0, sizeof(st));

    printf("decodage des formats de texture\n\n");

    /* --- RGBA16 : le cas courant de DKR, une recopie -------------------------- */
    memset(g_ram, 0, sizeof(g_ram));
    ecrire16(0x100u, 0xFFFFu);   /* blanc opaque */
    ecrire16(0x102u, 0xF801u);   /* rouge pur opaque */
    ecrire16(0x104u, 0x0001u);   /* noir opaque */
    ecrire16(0x106u, 0x07C1u);   /* vert pur opaque */
    entrelacer();

    check("RGBA16 se convertit",
          dkr_texture_convert(g_ram, RAM, 0, 0x100u, DKR_N64_FMT_RGBA,
                              DKR_N64_SIZ_16, 2, 2, g_out, &st) == 1);
    check("et rend les texels tels quels — c'est deja du 5551",
          g_out[0] == 0xFFFFu && g_out[1] == 0xF801u &&
          g_out[2] == 0x0001u && g_out[3] == 0x07C1u);

    /* La meme texture dans la disposition du jeu doit donner exactement la meme
       chose. C'est le controle qui attrape une conversion qui « marcherait » sur
       les epreuves et brouillerait tout sur la machine. */
    {
        unsigned short autre[4];
        memset(autre, 0, sizeof(autre));
        check("la disposition entrelacee se convertit aussi",
              dkr_texture_convert(g_ram_x, RAM, 1, 0x100u, DKR_N64_FMT_RGBA,
                                  DKR_N64_SIZ_16, 2, 2, autre, &st) == 1);
        check("et donne exactement le meme resultat",
              memcmp(autre, g_out, sizeof(autre)) == 0);
    }

    /* --- I8 : la replication des bits ---------------------------------------- *
     *
     * 255 doit donner du blanc franc. Un decalage seul donnerait 0xF7DE au lieu
     * de 0xFFFF — assez proche pour passer a l'oeil, assez faux pour qu'aucun
     * blanc ne soit jamais blanc. */
    memset(g_ram, 0, sizeof(g_ram));
    g_ram[0x200] = 0xFF;
    g_ram[0x201] = 0x00;
    entrelacer();
    check("I8 se convertit",
          dkr_texture_convert(g_ram, RAM, 0, 0x200u, DKR_N64_FMT_I,
                              DKR_N64_SIZ_8, 2, 1, g_out, &st) == 1);
    check("I8 : 255 donne un blanc franc, pas un blanc approche",
          ((g_out[0] >> 11) & 0x1Fu) == 0x1Fu &&
          ((g_out[0] >> 6) & 0x1Fu) == 0x1Fu &&
          ((g_out[0] >> 1) & 0x1Fu) == 0x1Fu);
    check("I8 : 0 donne du noir", (g_out[1] & 0xFFFEu) == 0u);

    /* --- IA16 : l'alpha devient un seuil -------------------------------------- *
     *
     * Huit bits d'alpha sur la N64, **un seul** sur la Voodoo. Le degrade devient
     * un seuil, et c'est une perte assumee — mais elle doit au moins tomber du
     * bon cote. */
    memset(g_ram, 0, sizeof(g_ram));
    ecrire16(0x300u, 0xFF00u);   /* intensite pleine, alpha nul   */
    ecrire16(0x302u, 0xFFFFu);   /* intensite pleine, alpha plein */
    entrelacer();
    check("IA16 se convertit",
          dkr_texture_convert(g_ram, RAM, 0, 0x300u, DKR_N64_FMT_IA,
                              DKR_N64_SIZ_16, 2, 1, g_out, &st) == 1);
    check("IA16 : l'alpha nul devient transparent, l'alpha plein opaque",
          (g_out[0] & 1u) == 0u && (g_out[1] & 1u) == 1u);

    /* --- Ce qui doit etre refuse ---------------------------------------------- *
     *
     * Les formats indexes demandent la palette, chargee par une commande a part.
     * Les servir sans elle donnerait des couleurs arbitraires — pire qu'une
     * absence, puisque cela passe pour du rendu. */
    {
        const unsigned long avant = st.non_prises_en_charge;
        check("CI8 est refuse, faute de palette",
              dkr_texture_convert(g_ram, RAM, 0, 0x100u, DKR_N64_FMT_CI,
                                  DKR_N64_SIZ_8, 4, 4, g_out, &st) == 0);
        check("et le refus est compte, sans quoi il serait invisible",
              st.non_prises_en_charge == avant + 1u);
    }

    /* Hors bornes : le controle qui ne se voit jamais si on l'omet. */
    {
        const unsigned long avant = st.hors_rdram;
        check("une texture qui depasse la RDRAM est refusee",
              dkr_texture_convert(g_ram, RAM, 0, RAM - 8u, DKR_N64_FMT_RGBA,
                                  DKR_N64_SIZ_16, 16, 16, g_out, &st) == 0);
        check("et le refus est compte", st.hors_rdram == avant + 1u);
    }

    /* Une dimension absurde issue d'un decodage faux ne doit pas deborder le
       tampon de conversion, qui fait 256x256. */
    check("une dimension au-dela du tampon est refusee",
          dkr_texture_convert(g_ram, RAM, 0, 0x100u, DKR_N64_FMT_RGBA,
                              DKR_N64_SIZ_16, 512, 512, g_out, &st) == 0);

    /* Et la taille en octets, dont depend la verification de bornes. */
    check("la taille en octets suit le format",
          dkr_texture_bytes(DKR_N64_SIZ_4, 8, 8) == 32u &&
          dkr_texture_bytes(DKR_N64_SIZ_8, 8, 8) == 64u &&
          dkr_texture_bytes(DKR_N64_SIZ_16, 8, 8) == 128u &&
          dkr_texture_bytes(DKR_N64_SIZ_32, 8, 8) == 256u);

    printf("\n  converties=%lu refusees=%lu hors-rdram=%lu trop-grandes=%lu\n",
           st.converties, st.non_prises_en_charge, st.hors_rdram,
           st.trop_grandes);
    printf("\n%d echec(s)\n", g_fails);
    return g_fails != 0;
}
