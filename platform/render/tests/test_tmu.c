/* E05-S02 — l'allocateur de TMU, éprouvé sans carte.
 *
 * La ROM absente interdit d'éprouver l'allocateur en jeu, et la carte ne dit
 * rien de ce qu'elle reçoit : `grTexDownloadMipMap` ne rend aucun code d'erreur.
 * Cette suite est donc la seule vérification disponible du raisonnement, et elle
 * doit être exhaustive là où elle le peut.
 *
 * Elle vérifie trois choses de nature différente :
 *
 *   - **les invariants de l'arbre** — deux allocations ne se recouvrent jamais,
 *     et aucune ne sort de la mémoire réelle. C'est vérifié par force brute sur
 *     toutes les allocations vivantes, pas par échantillonnage ;
 *   - **l'absence de fragmentation externe**, la propriété qui a justifié le
 *     choix du buddy plutôt qu'un allocateur par classes ;
 *   - **la politique d'éviction**, dont le cas intéressant n'est pas « évincer
 *     le plus ancien » mais « ne pas évincer ce dont l'image en cours a besoin ».
 */
#include "render/tmu.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", ok ? "ok   " : "ECHEC", what);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static void report(const char *fmt, unsigned long a, unsigned long b)
{
    char line[200];
    sprintf(line, fmt, a, b);
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

/* --- Le faux transfert -------------------------------------------------------- *
 *
 * Il enregistre ce qu'on lui donne, ce qui permet de vérifier non seulement que
 * l'allocateur rend des adresses cohérentes, mais qu'il **télécharge la bonne
 * quantité au bon endroit** — deux choses qu'un allocateur peut réussir
 * séparément et rater ensemble. */
typedef struct { unsigned int address, bytes; } transfer;
static transfer g_transfers[4096];
static int      g_transfer_count;
static int      g_refuse_next;

static int fake_download(void *user, int tmu, unsigned int address,
                         const void *data, unsigned int bytes)
{
    (void)user; (void)tmu; (void)data;
    if (g_refuse_next) { g_refuse_next = 0; return 0; }
    if (g_transfer_count < 4096) {
        g_transfers[g_transfer_count].address = address;
        g_transfers[g_transfer_count].bytes   = bytes;
        g_transfer_count++;
    }
    return 1;
}

/* --- Les invariants ----------------------------------------------------------- */

/* Les blocs vivants ne se recouvrent pas et tiennent dans [base, limit). */
static int no_overlap(const dkr_tmu *t)
{
    int i, j;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        const dkr_tmu_resident *a = &t->resident[i];
        if (!a->live) { continue; }
        if (a->address < t->base) { return 0; }
        if (a->address + a->bytes > t->limit) { return 0; }
        if ((a->address % DKR_TMU_GRANULARITY) != 0u) { return 0; }
        for (j = i + 1; j < DKR_TMU_MAX_RESIDENT; j++) {
            const dkr_tmu_resident *b = &t->resident[j];
            if (!b->live) { continue; }
            if (a->address < b->address + b->bytes &&
                b->address < a->address + a->bytes) {
                return 0;
            }
        }
    }
    return 1;
}

static int live_count(const dkr_tmu *t)
{
    int i, n = 0;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        if (t->resident[i].live) { n++; }
    }
    return n;
}

int main(void)
{
    dkr_tmu t;
    /* Les bornes réellement mesurées sur la carte : voir `win95-tmu.md`.
       0x1FFFF8 et non 0x200000 — il manque huit octets, et c'est précisément ce
       qui rend la réserve du haut de l'arbre nécessaire. */
    const unsigned int BASE = 0x00000000u, LIMIT = 0x001FFFF8u;

    g_out = fopen("D:\\TMUTEST.TXT", "w");

    /* --- Allocation élémentaire --------------------------------------------- */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        const unsigned int a = dkr_tmu_alloc(&t, 8192u);
        const unsigned int b = dkr_tmu_alloc(&t, 8192u);
        check("deux allocations reussissent",
              a != DKR_TMU_NONE && b != DKR_TMU_NONE);
        check("et donnent des adresses distinctes", a != b);
        check("alignees sur la granularite materielle",
              (a % DKR_TMU_GRANULARITY) == 0u && (b % DKR_TMU_GRANULARITY) == 0u);
        check("l'occupation suit", dkr_tmu_used(&t) == 16384u);
        dkr_tmu_free(&t, a, 8192u);
        dkr_tmu_free(&t, b, 8192u);
        check("et revient a zero apres liberation", dkr_tmu_used(&t) == 0u);
    }

    /* --- Le huitième octet manquant ------------------------------------------ *
     *
     * L'arbre couvre 2 Mio pleins, la carte n'en offre que 0x1FFFF8. Le bloc de
     * tête de 2 Mio ne doit donc **pas** être allouable : il déborderait de huit
     * octets. Le défaut serait rare — mémoire presque vide, une seule texture
     * enorme — donc découvert tard. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    check("le bloc de 2 Mio entier est refuse : il deborde de 8 octets",
          dkr_tmu_alloc(&t, 0x200000u) == DKR_TMU_NONE);
    check("mais celui de 1 Mio passe", dkr_tmu_alloc(&t, 0x100000u) != DKR_TMU_NONE);

    /* --- Aucune fragmentation externe ---------------------------------------- *
     *
     * C'est la propriete qui a justifie le buddy. On remplit de 64x64, on libere
     * une allocation sur deux, puis on redemande des 128x128 : chacun doit
     * trouver sa place dans deux 64x64 voisins fusionnes. Un allocateur par
     * classes de taille sans fusion echouerait ici, et c'est exactement le
     * scenario d'un changement de niveau. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        unsigned int got[256];
        int i, n = 0, reused = 0;
        for (i = 0; i < 256; i++) {
            got[i] = dkr_tmu_alloc(&t, 8192u);        /* 64x64 en 16 bits */
            if (got[i] != DKR_TMU_NONE) { n++; }
        }
        report("  64x64 places : %lu sur %lu demandes", (unsigned long)n, 256ul);
        /* **255, et non 256.** Deux Mio contiennent exactement 256 blocs de
           8 Kio, mais la carte n'en offre que 0x1FFFF8 : les huit derniers
           octets manquent, et ils tombent dans le dernier bloc de 8 Kio. Celui-ci
           est donc coupe, et n'est plus allouable en entier.
           Cette suite affirmait 256 et se trompait — c'est l'allocateur qui avait
           raison. Le meme huitieme octet a deja fait mentir une supposition plus
           haut dans ce fichier ; il vaut d'etre retenu. */
        check("la TMU tient 255 textures de 64x64 : les 8 octets manquants "
              "amputent le dernier bloc", n == 255);

        /* **Ce que coute vraiment la reserve : 128 octets, pas 8 Kio.**
         *
         * Le dernier bloc de 8 Kio n'est pas perdu — il est coupe. Seul le
         * dernier bloc de la taille minimale, celui qui contient reellement la
         * frontiere, est reserve. Le reste demeure disponible a grain plus fin,
         * et la distinction est loin d'etre academique : perdre 8 Kio par TMU
         * serait le prix de quatre textures 32x32. */
        {
            int small = 0;
            while (dkr_tmu_alloc(&t, DKR_TMU_MIN_BLOCK) != DKR_TMU_NONE) {
                small++;
            }
            report("  puis %lu blocs de %lu octets dans la queue",
                   (unsigned long)small, (unsigned long)DKR_TMU_MIN_BLOCK);
            check("la queue du dernier bloc reste utilisable a grain fin : "
                  "le cout de la reserve est de 128 octets, pas de 8 Kio",
                  small == 63);
        }
        dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
        for (i = 0; i < 256; i++) { got[i] = dkr_tmu_alloc(&t, 8192u); }
        for (i = 0; i < 256; i += 2) {
            if (got[i] != DKR_TMU_NONE) { dkr_tmu_free(&t, got[i], 8192u); }
        }
        for (i = 0; i < 128; i++) {
            if (dkr_tmu_alloc(&t, 8192u) != DKR_TMU_NONE) { reused++; }
        }
        check("les 128 blocs liberes se reprennent entierement", reused == 128);
    }

    /* La fusion elle-même : liberer *tout*, puis demander le plus gros bloc que
       la memoire permette. Sans fusion, l'arbre resterait en miettes. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        unsigned int got[256];
        int i;
        for (i = 0; i < 256; i++) { got[i] = dkr_tmu_alloc(&t, 8192u); }
        for (i = 0; i < 256; i++) { dkr_tmu_free(&t, got[i], 8192u); }
        check("apres 256 liberations, un bloc de 1 Mio est de nouveau possible",
              dkr_tmu_alloc(&t, 0x100000u) != DKR_TMU_NONE);
    }

    /* --- Le cache : succes et defauts ---------------------------------------- */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    g_transfer_count = 0;
    {
        static unsigned char data[8192];
        unsigned int a1, a2;
        dkr_tmu_begin_frame(&t);
        a1 = dkr_tmu_acquire(&t, 0x1111ull, data, 8192u);
        a2 = dkr_tmu_acquire(&t, 0x1111ull, data, 8192u);
        check("la meme cle rend la meme adresse", a1 == a2 && a1 != DKR_TMU_NONE);
        check("et n'a ete telechargee qu'une fois", g_transfer_count == 1);
        check("les compteurs distinguent succes et defaut",
              t.stats.hits == 1 && t.stats.misses == 1);
        check("le transfert porte la bonne adresse et la bonne taille",
              g_transfers[0].address == a1 && g_transfers[0].bytes == 8192u);
    }

    /* --- L'eviction, et son cas interessant ----------------------------------- *
     *
     * Le cas facile est « evincer le plus ancien ». Le cas qui compte est
     * l'inverse : **ne pas evincer ce dont l'image en cours a besoin**. Une image
     * qui demande plus que la TMU ne tient evincerait sinon ce qu'elle vient de
     * telecharger, et l'on paierait le bus a chaque texture pour n'afficher rien
     * de plus — le pire regime possible. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    g_transfer_count = 0;
    {
        static unsigned char data[8192];
        int i, placed = 0;
        dkr_tmu_begin_frame(&t);
        /* 300 textures de 8 Kio demandent 2,4 Mio : la TMU n'en tient que 256.
           Toutes etant protegees par l'image en cours, les 44 dernieres doivent
           **echouer** plutot que de chasser les precedentes. */
        for (i = 0; i < 300; i++) {
            if (dkr_tmu_acquire(&t, (unsigned long long)i, data, 8192u)
                != DKR_TMU_NONE) {
                placed++;
            }
        }
        report("  placees %lu, echecs %lu", (unsigned long)placed,
               t.stats.failures);
        check("l'image sature proprement plutot que de se chasser elle-meme",
              placed == 255 && t.stats.failures == 45);
        check("aucune eviction pendant l'image", t.stats.evictions == 0);
        check("les blocs vivants ne se recouvrent pas", no_overlap(&t));

        /* Image suivante : les protections tombent, l'eviction redevient
           possible, et une texture nouvelle trouve sa place. */
        dkr_tmu_begin_frame(&t);
        check("a l'image suivante, une texture nouvelle passe",
              dkr_tmu_acquire(&t, 0xDEADull, data, 8192u) != DKR_TMU_NONE);
        check("au prix d'une eviction", t.stats.evictions == 1);
        check("et les compteurs par image sont repartis de zero",
              t.stats.downloads_this_frame == 1);
    }

    /* --- Le moindre recemment utilise ----------------------------------------- */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        static unsigned char data[0x80000];
        unsigned int a, b, c;
        dkr_tmu_begin_frame(&t);
        a = dkr_tmu_acquire(&t, 1ull, data, 0x80000u);   /* 512 Kio chacune */
        b = dkr_tmu_acquire(&t, 2ull, data, 0x80000u);
        (void)dkr_tmu_acquire(&t, 3ull, data, 0x80000u);
        dkr_tmu_begin_frame(&t);
        /* On reemploie 1, ce qui rajeunit sa date. 2 devient la plus ancienne. */
        (void)dkr_tmu_acquire(&t, 1ull, data, 0x80000u);
        c = dkr_tmu_acquire(&t, 4ull, data, 0x80000u);
        check("la quatrieme texture trouve la place de la deuxieme",
              c != DKR_TMU_NONE && c == b);
        check("et la premiere, reemployee, est toujours la",
              dkr_tmu_acquire(&t, 1ull, data, 0x80000u) == a);
    }

    /* --- Un transfert refuse doit rendre le bloc ------------------------------ *
     *
     * Sinon la memoire fuit a chaque echec, et l'echec suivant arrive plus tot
     * que le precedent — une degradation qui s'accelere et qu'on diagnostique
     * mal. */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        static unsigned char data[8192];
        unsigned int before;
        dkr_tmu_begin_frame(&t);
        before = dkr_tmu_used(&t);
        g_refuse_next = 1;
        check("un transfert refuse rend DKR_TMU_NONE",
              dkr_tmu_acquire(&t, 7ull, data, 8192u) == DKR_TMU_NONE);
        check("et ne laisse pas le bloc alloue", dkr_tmu_used(&t) == before);
        check("l'echec est compte", t.stats.failures == 1);
    }

    /* --- Le changement de niveau ---------------------------------------------- */
    dkr_tmu_init(&t, 0, BASE, LIMIT, fake_download, 0);
    {
        static unsigned char data[8192];
        int i;
        dkr_tmu_begin_frame(&t);
        for (i = 0; i < 100; i++) {
            (void)dkr_tmu_acquire(&t, (unsigned long long)i, data, 8192u);
        }
        check("cent textures resident", live_count(&t) == 100);
        dkr_tmu_reset(&t);
        check("la remise a zero vide la TMU", dkr_tmu_used(&t) == 0u &&
                                              live_count(&t) == 0);
        check("mais les compteurs cumules survivent : ils diront a la fin si "
              "le portage a telecharge pendant les courses",
              t.stats.downloads == 100);
    }

    /* --- La ligne d'etat, lisible en jeu -------------------------------------- */
    {
        char line[160];
        dkr_tmu_format_status(&t, line, sizeof(line));
        printf("  etat : %s\n", line);
        if (g_out) { fprintf(g_out, "  etat : %s\n", line); }
        check("la ligne d'etat n'est pas vide", line[0] != 0);
    }

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
