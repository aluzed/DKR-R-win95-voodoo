/* E01-S03 - startup for the Windows 95 target.
 *
 * To be called on the first line of `main`. Three things nobody else will do,
 * and whose absence is expensive:
 *
 *  1. A **startup log in a file**. There is no usable console on the target
 *     machine: a full-screen game that dies before its first display leaves
 *     nothing to read. The log is written next to the executable and flushed on
 *     every line, so that the last line survives the crash that interrupted it.
 *
 *  2. A **structured exception filter**. Without it, an invalid instruction or a
 *     faulty access produces a Windows 95 dialog box that names nothing usable.
 *     With it, the code and the address go into the log.
 *
 *  3. A **version check**. The floor chosen is Windows 95 (ADR 0002); refusing
 *     cleanly beats crashing on a missing API, and beats crashing at random
 *     later by a very wide margin.
 */
#ifndef DKR_WIN95_STARTUP_H
#define DKR_WIN95_STARTUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes, so that `main` can return something other than 1. */
enum {
    DKR_WIN95_STARTUP_OK          = 0,
    DKR_WIN95_STARTUP_TOO_OLD     = 2,   /* system older than the floor */
    DKR_WIN95_STARTUP_NO_LOG      = 3    /* log impossible to open */
};

/* Prepares the log and the exception filter, and checks the system version.
   Returns DKR_WIN95_STARTUP_OK, or an error code after showing a comprehensible
   message. `app_name` appears in the log and in the dialog boxes. */
int dkr_win95_startup(const char *app_name);

/* Writes one line to the startup log. No effect before `dkr_win95_startup`. The
   file is flushed after every line: a written line is a line that will survive
   the next crash. */
void dkr_win95_log(const char *message);

/* Likewise, with an integer appended - enough to trace an error code without
   pulling in printf. */
void dkr_win95_log_num(const char *message, long value);

/* Closes the log. Optional: the system will do it. */
void dkr_win95_shutdown(void);

/* --- Cleanups to run even on an abnormal exit ----------------------------- *
 *
 * Some settings outlive the process that made them, and leaving them in place
 * degrades the machine until reboot. E02-S03's `timeBeginPeriod` is one; Glide's
 * full-screen mode will be another (E05).
 *
 * The system does not undo them. They therefore have to be undone by hand,
 * including when we die on an exception - that is, from the filter installed by
 * `dkr_win95_startup`.
 *
 * The direction of the dependency is what forces this registry rather than a
 * direct call: `startup.c` is the bottom layer, and cannot know about the clock
 * without every witness that merely wants the log dragging `winmm` along with
 * it. So it is the clock that announces itself.
 *
 * Constraints of the context, because a cleanup called from an exception filter
 * runs inside an already damaged process: the function must allocate nothing,
 * wait for nothing, and tolerate being called while its own subsystem is half
 * destroyed. The registry is therefore fixed in size, with no allocation.
 *
 * Returns 1 if the cleanup was registered, 0 if the registry is full. */
typedef void (*dkr_win95_cleanup_fn)(void);

#define DKR_WIN95_MAX_CLEANUPS 8

int  dkr_win95_at_abnormal_exit(dkr_win95_cleanup_fn cleanup);

/* Runs the registered cleanups, in reverse order of registration, and once only
   however many times it is called. Called by the exception filter; to be called
   as well from any other abrupt-exit path - `dkr_threading_fatal` does. */
void dkr_win95_run_cleanups(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_STARTUP_H */
