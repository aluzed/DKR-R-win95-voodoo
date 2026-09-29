/* E08-S01 - a sampling profiler for Windows 95.
 *
 * The frame budget's timers see only what they wrap, and at fine grain their own
 * cost distorts what they measure; 14% of the processor was left that no timer
 * saw. A sampler sees everything that runs in user mode, the Glide driver
 * included. A thread at THREAD_PRIORITY_TIME_CRITICAL wakes every millisecond,
 * suspends each registered thread in turn, reads its EIP with GetThreadContext
 * and resumes it.
 *
 * Windows 95 has no OpenThread, so threads cannot be found by id after the
 * fact: they are registered as they are created (`dkr_thread_start`), and the
 * main thread registers itself.
 *
 * DKR_TRACE_SAMPLER=1 samples for 60 s, DKR_TRACE_SAMPLER=<n> for n seconds,
 * then closes its file and stops, well before the harness quits the game.
 * DKR_TRACE_SAMPLER_DELAY=<n> starts sampling n seconds late, past the loading.
 * DKR_TRACE_SAMPLER_DELAY=race+<n> starts n seconds after the renderer first sees
 * `gGameMode` read INGAME: a route into a race is driven by hand, one confirmed
 * press at a time, and no fixed delay can be set for it in advance. It
 * writes `D:\SAMPLES.BIN`: a header listing the loaded
 * modules (Toolhelp32: base, size, name), then records of thread index and EIP.
 * `tools/win95/sampler_report.py` turns it into a profile by module and by
 * function, using the executable's own symbols.
 */
#ifndef DKR_WIN95_SAMPLER_H
#define DKR_WIN95_SAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers a thread for sampling. `handle` is duplicated; the caller keeps its
   own. No-op when the sampler is off. */
void dkr_sampler_register_thread(void *handle);

/* Registers the calling thread. */
void dkr_sampler_register_current_thread(void);

/* Starts the sampler if DKR_TRACE_SAMPLER is set. Returns 1 if it runs. */
int dkr_sampler_start(void);

/* Whether the sampler is waiting for a race (DKR_TRACE_SAMPLER_DELAY=race+<n>).
   Cheap enough to call once per display list. */
int dkr_sampler_waits_for_race(void);

/* Called when the game is seen in a race; starts the countdown once. */
void dkr_sampler_race_started(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_SAMPLER_H */
