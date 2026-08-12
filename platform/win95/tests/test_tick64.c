/* E01-S03 — le rebouclage de GetTickCount, simule.
 *
 * `GetTickCount` revient a zero apres 49,7 jours. Le defaut ne se rencontre
 * jamais en test et se rencontre chez un joueur : c'est precisement pour cela
 * que la logique d'accumulation a ete isolee en fonction pure, pilotable avec
 * des valeurs choisies.
 *
 * Ce test tourne sur l'hote, sans Windows ni emulateur — il n'appelle que
 * `dkr_tick64_step`.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../compat.h"

static int failures = 0;

static void expect(const char *what, unsigned long long got, unsigned long long want)
{
    if (got == want) {
        printf("  ok    %-46s %llu\n", what, got);
    } else {
        printf("  ECHEC %-46s attendu %llu, obtenu %llu\n", what, want, got);
        failures++;
    }
}

#define TICK_MAX 0xFFFFFFFFULL

int main(void)
{
    dkr_tick64_state st = { 0, 0 };

    puts("Rebouclage de GetTickCount (E01-S03)");

    /* Marche normale : la valeur 64 bits suit la valeur 32 bits. */
    expect("demarrage a zero",            dkr_tick64_step(&st, 0),          0);
    expect("progression simple",          dkr_tick64_step(&st, 1000),       1000);
    expect("progression",                 dkr_tick64_step(&st, 0x7FFFFFFF), 0x7FFFFFFFULL);

    /* Juste avant le rebouclage : 49,7 jours de fonctionnement. */
    expect("dernier tick avant retour",   dkr_tick64_step(&st, 0xFFFFFFFF), TICK_MAX);

    /* Le rebouclage. Sans traitement, le temps reculerait de 49 jours — et un
       calcul de duree rendrait une valeur enorme ou negative selon le type. */
    expect("premier tick apres retour",   dkr_tick64_step(&st, 0),          TICK_MAX + 1);
    expect("progression apres retour",    dkr_tick64_step(&st, 5000),       TICK_MAX + 1 + 5000);

    /* Deuxieme rebouclage : environ 99 jours. */
    dkr_tick64_step(&st, 0xFFFFFFFF);
    expect("deuxieme retour a zero",      dkr_tick64_step(&st, 0),          2 * (TICK_MAX + 1));

    /* Le temps ne recule jamais, quelle que soit la sequence. C'est la seule
       propriete dont depend un calcul de duree. */
    {
        dkr_tick64_state s2 = { 0, 0 };
        unsigned long seq[] = { 0, 100, 0xFFFFFF00, 0xFFFFFFFF, 0, 1, 0xFFFFFFFE, 0, 7 };
        unsigned long long prev = 0;
        int monotone = 1, i;
        for (i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])); i++) {
            unsigned long long now = dkr_tick64_step(&s2, seq[i]);
            if (now < prev) { monotone = 0; }
            prev = now;
        }
        expect("monotonie sur une sequence a deux retours", (unsigned long long)monotone, 1);
    }

    /* Limite connue, et assumee : si personne ne lit l'horloge pendant plus de
       49 jours, le rebouclage passe inapercu. On ne peut pas le detecter — deux
       lectures espacees de 49 jours et de 1 milliseconde sont indiscernables.
       Le test fige ce comportement pour qu'il reste un choix documente et non
       une surprise. Voir docs/WIN95-COMPAT.md. */
    {
        dkr_tick64_state s3 = { 0, 0 };
        dkr_tick64_step(&s3, 1000);
        expect("rebouclage manque si l'horloge n'est pas lue",
               dkr_tick64_step(&s3, 2000), 2000);
    }

    if (failures) {
        printf("\n%d verification(s) en echec\n", failures);
        return 1;
    }
    puts("\nToutes les verifications passent.");
    return 0;
}
