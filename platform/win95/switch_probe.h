/* E08-S01 - what one thread handoff costs on this machine.
 *
 * The frame budget leaves about 16% of the processor unmeasured, and the
 * sampler puts a quarter of it on the idle guest thread, around the mutexes,
 * condition variables and semaphores that hand the guest token from thread to
 * thread. A handoff is kernel work that no user-mode timer around the game's
 * code can see. This probe measures its unit costs once, at start-up, so that
 * a count of handoffs can be turned into time.
 *
 * DKR_PROBE_SWITCH=1 runs it before the game starts and prints `[boot][switch]`.
 * Every loop is bounded both in iterations and in time.
 */
#ifndef DKR_WIN95_SWITCH_PROBE_H
#define DKR_WIN95_SWITCH_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

void dkr_switch_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_SWITCH_PROBE_H */
