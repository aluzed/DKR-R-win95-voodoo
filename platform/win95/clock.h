/* E02-S03 - monotonic time base for Windows 95.
 *
 * The whole simulation depends on it: frame pacing, race timing, audio
 * scheduling. A base that drifts slowly breaks nothing visible and falsifies
 * everything.
 *
 * ## What the machine actually offers
 *
 * Measured on the target, not a compatibility table - the full reading is in
 * `docs/research/win95-clock.md`:
 *
 *   source                    resolution   monotonic      relative cost
 *   GetTickCount                   9 ms    -              1
 *   timeGetTime                    1 ms    0 backsteps    ~135
 *   QueryPerformanceCounter      4.19 us   0 backsteps    ~104
 *
 * Two results contradict what the ticket assumed, and change the design:
 *
 *  1. **`timeBeginPeriod(1)` is useless here.** `timeGetTime` already returns
 *     the millisecond before any adjustment. The call is made and released all
 *     the same, because nothing guarantees it will be so on another machine, and
 *     because a setting left in place degrades the whole system until reboot.
 *
 *  2. **`GetTickCount` is two orders of magnitude cheaper** than the other two.
 *     It reads a variable in shared memory; `QueryPerformanceCounter` reads the
 *     PIT through I/O accesses, and `timeGetTime` goes through `winmm`.
 *     Resolution is paid for, and the figure goes into E08-S01's budget.
 *
 * ## The frequency says where the counter comes from
 *
 * `QueryPerformanceFrequency` returns **1,193,180 Hz**, that is, the frequency
 * of the 8254 PIT. So this is not the processor's cycle counter, and one fact
 * follows that is better known before than after: **the low 32 bits of that
 * counter wrap in exactly 60 minutes.**
 *
 * The API returns 64 bits and Windows 95 extends the counter; this layer
 * therefore relies on the full value. But the `timeGetTime` fallback is a 32-bit
 * counter that wraps after 49.7 days, and it is accumulated - by the same pure
 * function as E01-S03's `GetTickCount64`, whose problem this is exactly.
 */
#ifndef DKR_WIN95_CLOCK_H
#define DKR_WIN95_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* The source chosen at startup. Returned so that the startup log can name it:
   when a machine behaves oddly, knowing which clock it runs on is the first
   question. */
typedef enum {
    DKR_CLOCK_SOURCE_NONE = 0,
    DKR_CLOCK_SOURCE_QPC,          /* QueryPerformanceCounter */
    DKR_CLOCK_SOURCE_TIMEGETTIME   /* fallback */
} dkr_clock_source;

/* Selects and **validates** the source. A source that announces itself and then
   misbehaves is discarded in favour of the fallback, rather than producing
   erratic behaviour in game. Returns 1 if a usable source was chosen. */
int              dkr_clock_init(void);
void             dkr_clock_shutdown(void);   /* releases timeBeginPeriod */

dkr_clock_source dkr_clock_source_in_use(void);
const char      *dkr_clock_source_name(void);

/* Ticks per second of the chosen source. */
unsigned long long dkr_clock_frequency(void);

/* Time elapsed since `dkr_clock_init`, in the source's ticks. Monotonic. */
unsigned long long dkr_clock_now(void);

/* The same, in microseconds. */
unsigned long long dkr_clock_now_us(void);

/* --- The VR4300 cycle counter --------------------------------------------- *
 *
 * DKR measures time through this counter, which advances at 46.875 MHz - half
 * the N64 processor's frequency - **independently** of the host's frequency. It
 * is that ratio which must be exact; a slow drift there would falsify every race
 * timing without breaking anything visible.
 */
#define DKR_VR4300_COUNTER_HZ 46875000ULL

unsigned long long dkr_clock_vr4300_count(void);

/* The conversion, isolated as a pure function so that it can be tested on the
   host without Windows - and so that its freedom from overflow is demonstrated
   rather than assumed.
 *
 * The naive `ticks * 46875000 / frequency` overflows as soon as `ticks` exceeds
 * 2^63 / 46,875,000, that is, about 46 hours of play. So we separate the
 * quotient from the remainder: the remainder is smaller than the frequency, and
 * its product fits comfortably. */
unsigned long long dkr_clock_ticks_to_vr4300(unsigned long long ticks,
                                             unsigned long long frequency);

/* --- Accumulating a 32-bit source ----------------------------------------- *
 *
 * The `timeGetTime` fallback wraps after 49.7 days. The accumulation logic is
 * that of `dkr_tick64_step` (E01-S03): it is reused and not copied, because a
 * second instance of the same reasoning always ends up diverging from the
 * first. See `platform/win95/tick64.c`.
 */

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_CLOCK_H */
