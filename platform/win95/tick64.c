/* E01-S03 — accumulation du rebouclage de GetTickCount.
 *
 * Isole de `compat.c` et sans dependance a Windows, pour deux raisons :
 *
 *  - le test `tests/test_tick64.c` le compile et le pilote sur l'hote, avec des
 *    valeurs choisies. Le rebouclage se produit apres 49,7 jours : il ne se
 *    rencontre jamais en test et se rencontre chez un joueur, donc il doit se
 *    simuler ;
 *  - la logique est la meme quelle que soit la plate-forme ; seule la source des
 *    ticks change.
 */
#include "compat.h"

unsigned long long dkr_tick64_step(dkr_tick64_state *state, unsigned long now32)
{
    if (now32 < state->last) {         /* le compteur 32 bits a reboucle */
        state->high++;
    }
    state->last = now32;
    return ((unsigned long long)state->high << 32) | (unsigned long long)now32;
}
