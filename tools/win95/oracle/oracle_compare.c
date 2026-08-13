/* E01-S05 — l'arithmetique du code recompile, comparee a l'oracle 64 bits.
 *
 * Le C produit par N64Recomp manipule les registres du VR4300 comme des entiers
 * de 64 bits. Sur l'hote, ce sont des registres machine ; en 32 bits, chaque
 * operation devient une paire, et les divisions et decalages passent par les
 * auxiliaires de libgcc — `__divdi3`, `__moddi3`, `__ashrdi3`. Le compilateur
 * s'en charge, mais « le compilateur s'en charge » n'est pas une verification.
 *
 * Le ticket demande donc mieux qu'une relecture : « un test cible sur quelques
 * fonctions arithmetiques du jeu, compare a la sortie de l'oracle 64 bits ».
 * C'est ce que fait ce programme.
 *
 * ## Comment
 *
 * Chaque fonction retenue est executee sur un etat **entierement determine** :
 * une RDRAM remplie par un generateur a graine fixe, et un contexte dont chaque
 * registre recoit une valeur tiree du meme generateur. On resume ensuite l'etat
 * final — contexte complet plus la RDRAM — par une empreinte FNV-1a de 64 bits.
 *
 * Le meme programme, compile pour l'hote 64 bits et pour la cible 32 bits, doit
 * produire **les memes empreintes**. Toute divergence est un defaut de portage,
 * et l'empreinte dit laquelle des fonctions l'a produit.
 *
 * ## Les registres pointent dans la RDRAM, a dessein
 *
 * Les acces memoire du code genere sont de la forme
 * `rdram + (registre + decalage) - 0xFFFFFFFF80000000`. Pour qu'ils tombent
 * dans la zone allouee, les registres recoivent donc une valeur voisine de
 * `0xFFFFFFFF80000000`, decalee de quelques kilo-octets. Sans cela le programme
 * lirait n'importe ou et la comparaison ne porterait sur rien.
 *
 * Certaines fonctions calculent malgre tout une adresse hors zone. Elles ne sont
 * pas ecartees : la faute est **rattrapee**, et « cette fonction fait faute »
 * devient une observation comme une autre, qui doit elle aussi concorder entre
 * les deux cibles.
 *
 * ## Limite connue, et ou elle mord
 *
 * Le rattrapage repose sur `signal(SIGSEGV)`. Sur l'hote c'est fiable, une pile
 * de secours reglant meme le debordement de pile. **Sous Windows 95, il ne
 * l'est pas** : la faute d'`obj_animate` est bien delivree, celle de
 * `func_8001CD28` ne l'est pas et le processus meurt. Ce n'est pas un
 * debordement de pile — une reserve de 64 Mio ne change rien — mais une faute
 * que le CRT de mingw ne traduit pas en signal sur cette cible.
 *
 * La comparaison s'arrete donc a la premiere fonction de ce genre. Sur les
 * fonctions atteintes, elle **concorde exactement**, fautes comprises.
 *
 * Pour aller au bout il faudra un pilote qui reprend : le programme note dans un
 * fichier la fonction qu'il s'apprete a executer, et une relance repart apres
 * elle. Cela fonctionne sur les deux cibles et ne demande aucune acrobatie
 * d'exception — ce qui vaut mieux que de faire dependre le harnais de ce que
 * Windows 95 veut bien delivrer.
 */
/* `sigsetjmp` est POSIX et non ISO : en `-std=c17` strict, l'en-tete le cache.
   Le harnais se compile a la meme norme que le code recompile, on demande donc
   explicitement l'extension plutot que de relacher la norme. */
#if !defined(_WIN32)
#  define _GNU_SOURCE 1
#endif

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

#include "recomp.h"

/* --- Terrain ------------------------------------------------------------- *
 *
 * 8 Mio, la RDRAM d'une N64 avec Expansion Pak — la meme quantite dont E00-S01
 * a verifie qu'elle s'alloue sous Windows 95.
 */
#define RDRAM_SIZE   (8u * 1024u * 1024u)
#define RDRAM_BASE   0xFFFFFFFF80000000ULL
/* Les registres visent le milieu de la zone : un decalage negatif du code
   genere reste alors dans les clous. */
#define REG_POINT    (RDRAM_BASE + (RDRAM_SIZE / 2))

static uint8_t *rdram;

/* --- Une RDRAM encadree de pages interdites -------------------------------- *
 *
 * Toutes les fonctions ne restent pas dans la zone : certaines calculent une
 * adresse a partir d'un registre qui, dans une vraie partie, pointerait
 * ailleurs. Avec une simple allocation, ces ecritures abiment le tas et le
 * programme meurt bien plus tard, en un endroit sans rapport — ce qui s'est
 * produit, et qui donnait a croire que la quatrieme fonction etait fautive.
 *
 * On reserve donc largement, on ne rend accessible que la fenetre de 8 Mio, et
 * tout ce qui deborde tombe sur une page interdite. La faute devient immediate,
 * rattrapable, et attribuee a la bonne fonction. C'est aussi le schema que
 * `librecomp` emploie pour de bon — reserver, valider une fenetre.
 */
#define GUARD_BYTES  (64u * 1024u * 1024u)

static uint8_t *reserve_rdram(void)
{
#if defined(_WIN32)
    uint8_t *base = (uint8_t *)VirtualAlloc(NULL, GUARD_BYTES * 2 + RDRAM_SIZE,
                                            MEM_RESERVE, PAGE_NOACCESS);
    if (!base) {
        return NULL;
    }
    if (!VirtualAlloc(base + GUARD_BYTES, RDRAM_SIZE, MEM_COMMIT, PAGE_READWRITE)) {
        return NULL;
    }
    return base + GUARD_BYTES;
#else
    uint8_t *base = (uint8_t *)mmap(NULL, GUARD_BYTES * 2 + RDRAM_SIZE,
                                    PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return NULL;
    }
    if (mprotect(base + GUARD_BYTES, RDRAM_SIZE, PROT_READ | PROT_WRITE) != 0) {
        return NULL;
    }
    return base + GUARD_BYTES;
#endif
}

/* Generateur a graine fixe. Volontairement trivial et ecrit ici plutot
   qu'emprunte a la bibliotheque : `rand` differe d'une implementation a
   l'autre, et l'un des deux cotes de la comparaison n'est pas Linux. */
static uint64_t rng_state;

static void rng_seed(uint64_t seed) { rng_state = seed; }

static uint64_t rng_next(void)
{
    /* xorshift64. Identique partout, sans dependre du CRT. */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* --- Empreinte ------------------------------------------------------------ */

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv1a(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < size; i++) {
        hash ^= (uint64_t)p[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

/* --- Rattrapage des fautes ------------------------------------------------ *
 *
 * `signal` plutot que SEH : mingw n'offre pas `__try` en C sur i686, et le CRT
 * traduit deja l'acces fautif en SIGSEGV. Le meme code vaut donc pour les deux
 * cibles, ce qui est exactement ce qu'on veut d'un harnais de comparaison.
 */
/* Sous POSIX, `longjmp` depuis un gestionnaire laisse le signal **bloque** : la
   seconde faute tue alors le processus, et l'on croirait a tort que la premiere
   fonction fautive est la derniere. `sigsetjmp` avec sauvegarde du masque est la
   seule forme correcte. Windows n'a pas de masque de signaux, et `setjmp` y
   suffit — d'ou les deux ecritures. */
#if defined(_WIN32)
#  define FAULT_SETJMP(buf)   setjmp(buf)
#  define FAULT_LONGJMP(buf)  longjmp((buf), 1)
typedef jmp_buf fault_buf;
#else
#  define FAULT_SETJMP(buf)   sigsetjmp((buf), 1)
#  define FAULT_LONGJMP(buf)  siglongjmp((buf), 1)
typedef sigjmp_buf fault_buf;
#endif

static fault_buf fault_return;
static volatile int fault_armed;

static void on_fault(int sig)
{
    (void)sig;
    if (fault_armed) {
        fault_armed = 0;
        FAULT_LONGJMP(fault_return);
    }
    _exit(3);
}

/* --- Le contexte, pose de facon reproductible ----------------------------- */

static void prepare(recomp_context *ctx, uint64_t seed)
{
    size_t i;
    uint64_t *slot;

    rng_seed(seed);

    /* La RDRAM d'abord : c'est elle que les fonctions liront. */
    for (i = 0; i < RDRAM_SIZE; i += 8) {
        uint64_t v = rng_next();
        memcpy(rdram + i, &v, 8);
    }

    memset(ctx, 0, sizeof(*ctx));

    /* Les 32 registres generaux. r0 reste nul — c'est le registre cable a zero
       du MIPS, et lui donner une valeur produirait des resultats que le
       materiel ne produit jamais. */
    slot = &ctx->r1;
    for (i = 0; i < 31; i++) {
        /* Une valeur voisine du point de visee, pour que les acces memoire
           tombent dans la zone, avec assez d'entropie dans les bits bas pour
           que l'arithmetique ait quelque chose a se mettre sous la dent. */
        slot[i] = REG_POINT + (int64_t)(int16_t)(rng_next() & 0x1FFF);
    }

    /* Les registres flottants, en tant que motifs binaires : ce sont les
       conversions entier <-> flottant qui divergent le plus volontiers entre
       x87 et SSE, et il faut donc leur donner de quoi diverger.

       On evite les motifs qui font un NaN ou un infini : leur propagation est
       une question de comportement flottant et non de portage 32 bits, et elle
       noierait le signal qu'on cherche. Les exposants sont donc bornes. */
    {
        fpr *f = &ctx->f0;
        for (i = 0; i < 32; i++) {
            uint64_t v = rng_next();
            v &= 0x3FFFFFFFFFFFFFFFULL;      /* exposant modeste */
            v |= 0x3F00000000000000ULL;      /* et non denormalise */
            f[i].u64 = v;
        }
    }

    /* `f_odd` n'est pas un tableau mais un **pointeur** vers la moitie haute de
       f0 : c'est par lui que le code genere atteint les demi-registres impairs
       en mode 32 bits du MIPS. Le laisser nul, comme le faisait la premiere
       version de ce harnais, fait sauter le programme avant sa premiere ligne.
       librecomp le pose de la meme facon (`recomp.cpp:471`). */
    ctx->f_odd = &ctx->f0.u32h;
}

/* L'empreinte porte sur l'etat **architectural**, champ par champ, et jamais sur
 * la structure brute. Deux raisons, et chacune suffirait :
 *
 *  - `recomp_context` contient `f_odd`, un **pointeur**. Sa valeur change d'une
 *    execution a l'autre ; resumer la structure entiere donnait donc une
 *    empreinte differente a chaque essai, y compris sur la meme machine.
 *  - Sa taille et sa disposition **different entre 32 et 64 bits**, a cause de
 *    ce meme pointeur et du remplissage. Une empreinte de la structure brute
 *    n'aurait jamais pu se comparer entre les deux cibles, ce qui est pourtant
 *    tout l'objet de ce programme.
 *
 * Chaque quantite est donc resumee dans une largeur fixe et un ordre fixe.
 */
static uint64_t digest(const recomp_context *ctx)
{
    uint64_t h = FNV_OFFSET;
    const gpr *r = &ctx->r0;
    const fpr *f = &ctx->f0;
    size_t i;

    for (i = 0; i < 32; i++) {
        uint64_t v = (uint64_t)r[i];
        h = fnv1a(h, &v, sizeof(v));
    }
    for (i = 0; i < 32; i++) {
        uint64_t v = f[i].u64;
        h = fnv1a(h, &v, sizeof(v));
    }
    {
        uint64_t hi = ctx->hi, lo = ctx->lo;
        uint32_t status = ctx->status_reg;
        uint8_t  mode = ctx->mips3_float_mode;
        h = fnv1a(h, &hi, sizeof(hi));
        h = fnv1a(h, &lo, sizeof(lo));
        h = fnv1a(h, &status, sizeof(status));
        h = fnv1a(h, &mode, sizeof(mode));
    }
    /* Toute la RDRAM, pour attraper une ecriture au mauvais endroit autant
       qu'un mauvais calcul. */
    h = fnv1a(h, rdram, RDRAM_SIZE);
    return h;
}

/* --- Les fonctions soumises ------------------------------------------------ */

#define DKR_ORACLE_FN(name) void name(uint8_t *rdram, recomp_context *ctx);
#include "functions.inc"
#undef DKR_ORACLE_FN

typedef void (*oracle_fn)(uint8_t *, recomp_context *);

static const struct {
    const char *name;
    oracle_fn   fn;
} entries[] = {
#define DKR_ORACLE_FN(name) { #name, name },
#include "functions.inc"
#undef DKR_ORACLE_FN
};

#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

int main(void)
{
    recomp_context ctx;
    size_t i;
    unsigned faults = 0;
    FILE *report;

    rdram = reserve_rdram();
    if (!rdram) {
        printf("ECHEC : RDRAM impossible a reserver\n");
        return 2;
    }

    /* Une pile de secours, sans quoi le debordement de pile est irrattrapable :
       le gestionnaire aurait besoin de la pile qui vient justement de manquer.
       C'est ce qui tuait le harnais sur `func_8001CD28`, et le faisait passer
       pour un defaut du code recompile alors que c'en est une propriete — cette
       fonction descend profond, et l'etat aleatoire du harnais l'y pousse. */
#if !defined(_WIN32)
    {
        static char alt[262144];
        stack_t ss;
        struct sigaction sa;

        ss.ss_sp    = alt;
        ss.ss_size  = sizeof(alt);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_fault;
        sa.sa_flags   = SA_ONSTACK | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS,  &sa, NULL);
        sigaction(SIGFPE,  &sa, NULL);
    }
#else
    signal(SIGSEGV, on_fault);
    signal(SIGFPE, on_fault);
#endif

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("# empreintes du code recompile — %u fonctions\n",
           (unsigned)ENTRY_COUNT);
    printf("# %u bits\n", (unsigned)(sizeof(void *) * 8));

    for (i = 0; i < ENTRY_COUNT; i++) {
        uint64_t h;

        /* Une graine par fonction, derivee de son rang : deux executions du
           programme posent le meme etat, et deux fonctions n'en partagent pas
           un. */
        prepare(&ctx, 0x9E3779B97F4A7C15ULL ^ (uint64_t)(i + 1));

        /* Le nom est annonce **avant** l'appel : si la fonction sort du cadre
           sans que la faute soit rattrapee, la derniere ligne du releve la
           nomme quand meme. Sans cela on cherche longtemps. */
        fprintf(stderr, "  -> %s\n", entries[i].name);
        fault_armed = 1;
        if (FAULT_SETJMP(fault_return) == 0) {
            entries[i].fn(rdram, &ctx);
            fault_armed = 0;
            h = digest(&ctx);
            printf("%-32s %016llX\n", entries[i].name, (unsigned long long)h);
        } else {
            /* Une faute est une observation, pas un echec : elle doit
               simplement se produire des deux cotes. */
            faults++;
            printf("%-32s FAUTE\n", entries[i].name);
        }
        fflush(stdout);
    }

    printf("# %u faute(s) sur %u\n", faults, (unsigned)ENTRY_COUNT);

#if defined(_WIN32)
    report = fopen("D:\\ORACLE.TXT", "w");
#else
    report = fopen("oracle-host.txt", "w");
#endif
    if (report) {
        /* Le releve est reecrit dans le fichier, pour etre compare octet a
           octet entre les deux cibles. */
        for (i = 0; i < ENTRY_COUNT; i++) {
            uint64_t h;
            prepare(&ctx, 0x9E3779B97F4A7C15ULL ^ (uint64_t)(i + 1));
            fault_armed = 1;
            if (FAULT_SETJMP(fault_return) == 0) {
                entries[i].fn(rdram, &ctx);
                fault_armed = 0;
                h = digest(&ctx);
                fprintf(report, "%-32s %016llX\n", entries[i].name,
                        (unsigned long long)h);
            } else {
                fprintf(report, "%-32s FAUTE\n", entries[i].name);
            }
        }
        fclose(report);
    }

    return 0;
}
