/* E01-S03 - Windows 95 API compatibility layer.
 *
 * This file does not need to be included for the layer to act: the missing
 * functions are supplied to the linker, and redirected to our implementations
 * through the `__imp__X@n` pointers. The rest of the project does not notice,
 * which is the point - no other ticket should have to care.
 *
 * It exposes what has to be testable or explicitly callable.
 *
 * The semantics lost by each workaround is written down in
 * `docs/WIN95-COMPAT.md`. A workaround whose difference is not written down is a
 * bug waiting to happen.
 */
#ifndef DKR_WIN95_COMPAT_H
#define DKR_WIN95_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- APIs the layer supplies and the headers hide ------------------------- *
 *
 * The toolchain sets `_WIN32_WINNT=0x0400` so that an API later than Windows 95
 * fails at compile time rather than at load time (E01-S01). The guard does not
 * tell an API we might use by accident from one this layer supplies: it hides
 * both.
 *
 * We therefore have to redeclare what we implement. The declaration is
 * conditioned on the guard's value, so as not to conflict on a target where the
 * system header already provides it.
 *
 * The other three - `IsDebuggerPresent`, `SetProcessAffinityMask` and
 * `TryEnterCriticalSection` - are declared unguarded by mingw, despite being
 * absent from Windows 95: nothing to redeclare for them.
 */
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600)
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
#endif

/* --- 64-bit monotonic clock ---------------------------------------------- *
 *
 * `GetTickCount` returns to zero after 49.7 days. The case is never met in
 * testing and is met at a player who leaves their machine on - exactly the kind
 * of defect one does not find by looking for it.
 *
 * The accumulation logic is isolated here as a pure function, so that the
 * wraparound can be simulated in a test instead of being waited out for seven
 * weeks.
 */
typedef struct {
    unsigned long high;   /* number of wraparounds observed */
    unsigned long last;   /* last 32-bit value seen */
} dkr_tick64_state;

/* Advances the state with a 32-bit reading and returns the 64-bit counter.
   `now32` is what `GetTickCount` returned. */
unsigned long long dkr_tick64_step(dkr_tick64_state *state, unsigned long now32);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_COMPAT_H */
