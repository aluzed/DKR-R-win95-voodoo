/* E01-S03 - accumulating GetTickCount's wraparound.
 *
 * Kept apart from `compat.c` and free of any Windows dependency, for two
 * reasons:
 *
 *  - the test `tests/test_tick64.c` compiles and drives it on the host, with
 *    chosen values. The wraparound happens after 49.7 days: it is never met in
 *    testing and is met at a player's machine, so it has to be simulated;
 *  - the logic is the same on every platform; only the source of ticks changes.
 */
#include "compat.h"

unsigned long long dkr_tick64_step(dkr_tick64_state *state, unsigned long now32)
{
    if (now32 < state->last) {         /* the 32-bit counter wrapped */
        state->high++;
    }
    state->last = now32;
    return ((unsigned long long)state->high << 32) | (unsigned long long)now32;
}
