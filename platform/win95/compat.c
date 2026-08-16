/* E01-S03 - Windows 95 API compatibility layer.
 *
 * GCC 13's standard library asks for six functions that Windows 95's KERNEL32
 * does not export. None is called by the project's own code: it is libstdc++ and
 * winpthreads that import them. But Windows 95 resolves *every* import at load
 * time, so their mere presence in the table is enough to stop the program from
 * starting - "linked to a missing export".
 *
 * This file supplies them. It sits before `libkernel32.a` in the linker's
 * resolution order, which then keeps these definitions rather than the import
 * declarations.
 *
 * The six gaps, and where they come from:
 *
 *   AddVectoredExceptionHandler      XP      libgcc, exception handling
 *   RemoveVectoredExceptionHandler   XP      likewise
 *   GetTickCount64                   Vista   winpthreads, monotonic clock
 *   IsDebuggerPresent                98/NT4  libstdc++, diagnostics
 *   SetProcessAffinityMask           NT      winpthreads, thread placement
 *   TryEnterCriticalSection          98/NT4  std::mutex::try_lock
 *
 * Five of the six are inconsequential. The sixth needs an explanation, below.
 */
#include <windows.h>

#include "compat.h"

#include <sys/stat.h>
#include <io.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* --- The four other MSVCRT gaps, and one from KERNEL32 --------------------- *
 *
 * Found at the game's first link, and all of the same family as `_fstat64`:
 * functions that later MSVCRTs added and Windows 95's does not have. Absent,
 * hence blocking at load time.
 *
 *   _wfopen_s, _wfreopen_s   libstdc++'s basic_file.o, opening by wide name
 *   _strtoi64, _strtoui64    64-bit conversion
 *   GetModuleHandleExW       libstdc++'s atexit_thread.o
 */

/* The two wide opens are never reached by the project's code: E02-S05
   established that handing a `path` to a stream opens through the wide API,
   which is stubbed here, and every call site now passes `.string()`. The import,
   though, remains - hence these definitions.

   They do not merely fail. Converting the name to narrow bytes and calling
   `fopen` is just as short to write, and makes the function correct for any name
   representable in the system code page. A silent failure would have been a trap
   for whoever called them one day without knowing. */
static int widen_to_ansi(const wchar_t *w, char *out, int out_size)
{
    BOOL used_default = FALSE;
    int  n;

    if (!w || !out || out_size <= 0) {
        return 0;
    }
    n = WideCharToMultiByte(CP_ACP, 0, w, -1, out, out_size, NULL, &used_default);
    /* A replacement character would designate a different file from the one
       asked for: better to refuse than to open the wrong one. */
    return (n > 0 && !used_default) ? 1 : 0;
}

int _wfopen_s(FILE **stream, const wchar_t *filename, const wchar_t *mode)
{
    char name[MAX_PATH], m[16];

    if (!stream) {
        return EINVAL;
    }
    *stream = NULL;
    if (!widen_to_ansi(filename, name, sizeof(name)) ||
        !widen_to_ansi(mode, m, sizeof(m))) {
        return EINVAL;
    }
    *stream = fopen(name, m);
    return *stream ? 0 : errno;
}

int _wfreopen_s(FILE **stream, const wchar_t *filename, const wchar_t *mode,
                FILE *old)
{
    char name[MAX_PATH], m[16];

    if (!stream) {
        return EINVAL;
    }
    *stream = NULL;
    if (!widen_to_ansi(filename, name, sizeof(name)) ||
        !widen_to_ansi(mode, m, sizeof(m))) {
        return EINVAL;
    }
    *stream = freopen(name, m, old);
    return *stream ? 0 : errno;
}

/* The parsing is written here rather than delegated to `strtoll`, and not out of
   taste: under mingw, `strtoll` is a redirection to `_strtoi64` **imported from
   MSVCRT**. Relying on it made the definition circular - it compiled, it linked,
   and the missing symbol reappeared in the import table with nothing reporting
   it. The import check saw it; reading the code did not.
 *
 * The contract followed is C99's `strtoull`: leading whitespace, optional sign,
 * `0x` prefix for base 16 and base 0 deduced, `endptr` set to the first
 * unconsumed character - and to `nptr` if nothing was consumed - and `ERANGE`
 * with saturation on overflow. */
static unsigned __int64 parse_u64(const char *s, char **end, int base,
                                  int *negative, int *overflow)
{
    const char        *p = s;
    const char        *digits_begin;
    unsigned __int64   value = 0;
    int                any = 0;

    *negative = 0;
    *overflow = 0;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
        p++;
    }
    if (*p == '+' || *p == '-') {
        *negative = (*p == '-');
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    if (base < 2 || base > 36) {
        if (end) { *end = (char *)s; }
        return 0;
    }

    digits_begin = p;
    for (; *p; p++) {
        int digit;
        if (*p >= '0' && *p <= '9')      { digit = *p - '0'; }
        else if (*p >= 'a' && *p <= 'z') { digit = *p - 'a' + 10; }
        else if (*p >= 'A' && *p <= 'Z') { digit = *p - 'A' + 10; }
        else                             { break; }
        if (digit >= base) {
            break;
        }
        if (value > (~(unsigned __int64)0 - (unsigned)digit) / (unsigned)base) {
            *overflow = 1;
        } else {
            value = value * (unsigned)base + (unsigned)digit;
        }
        any = 1;
    }
    /* Nothing consumable: `endptr` goes back to the start, prefix included. */
    if (end) { *end = (char *)(any ? p : s); }
    (void)digits_begin;
    return value;
}

__int64 _strtoi64(const char *s, char **end, int base)
{
    int negative = 0, overflow = 0;
    unsigned __int64 v;

    if (!s) {
        if (end) { *end = NULL; }
        return 0;
    }
    v = parse_u64(s, end, base, &negative, &overflow);
    if (negative) {
        if (overflow || v > 0x8000000000000000ULL) {
            errno = ERANGE;
            return (__int64)0x8000000000000000ULL;   /* LLONG_MIN */
        }
        return -(__int64)v;
    }
    if (overflow || v > 0x7FFFFFFFFFFFFFFFULL) {
        errno = ERANGE;
        return (__int64)0x7FFFFFFFFFFFFFFFULL;       /* LLONG_MAX */
    }
    return (__int64)v;
}

unsigned __int64 _strtoui64(const char *s, char **end, int base)
{
    int negative = 0, overflow = 0;
    unsigned __int64 v;

    if (!s) {
        if (end) { *end = NULL; }
        return 0;
    }
    v = parse_u64(s, end, base, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ~(unsigned __int64)0;
    }
    /* `strtoull` returns the modular negation, not an error. */
    return negative ? (unsigned __int64)(-(__int64)v) : v;
}

/* `strtoll` and `strtoull` come with them, and that is the part that was
 * missing.
 *
 * Under mingw these are not functions but redirections to `_strtoi64` and
 * `_strtoui64` **imported from MSVCRT**. Defining the previous two was therefore
 * not enough: `mod_manifest.cpp` and `mods.cpp` call `strtoll`, the redirection
 * was pulled from `libmsvcrt.a`, and the missing import came back through that
 * door.
 *
 * It is the same trap as the first version of `_strtoi64`, seen from another
 * side: on this target, one C library function can hide another, and only the
 * finished binary's import table says so. */
long long strtoll(const char *s, char **end, int base)
{
    return (long long)_strtoi64(s, end, base);
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    return (unsigned long long)_strtoui64(s, end, base);
}

/* Asked for by libstdc++'s `atexit_thread.o`, which uses it to pin the module
   carrying a thread-local destructor so that it is not unloaded before the
   thread ends.
 *
 * This binary is entirely static: there is no DLL to keep alive, and the module
 * carrying the code is the executable itself. Returning its handle is therefore
 * the right answer, not a makeshift one. The requested pinning has nothing to do
 * - an executable does not unload. */
BOOL WINAPI GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module)
{
    (void)flags;
    (void)name;
    if (!module) {
        return FALSE;
    }
    *module = GetModuleHandleA(NULL);
    return *module != NULL;
}

/* --- <fstream>: MSVCRT's `_fstat64` --------------------------------------- *
 *
 * The only gap that does not come from KERNEL32, and it is an expensive one:
 * merely including `<fstream>` makes the binary unloadable under Windows 95.
 *
 * libstdc++'s `basic_file.o` imports `__imp___fstat64`. Windows 95's MSVCRT.DLL
 * only exports the original `_fstat` family - the 64-bit variants arrived much
 * later. Measured: a binary that only includes `<cstdio>` loads, a binary that
 * includes `<fstream>` does not.
 *
 * The stakes reach beyond the test suites. Thirteen of the project's files use
 * `<fstream>`, including the core of `librecomp` - `recomp.cpp`, `pi.cpp`,
 * `sp.cpp`. Without this function, the game would not link for this target.
 *
 * What libstdc++ actually uses is narrow: `showmanyc()` asks for `st_mode` to
 * know whether the descriptor designates an ordinary file, and `st_size` to say
 * how many bytes are left to read. The rest of the structure is zeroed rather
 * than filled in by guesswork - a wrong date would be worse than a missing one,
 * because it would look like data.
 */

int _fstat64(int fd, struct _stat64 *st)
{
    HANDLE h;
    DWORD  type, low, high = 0;

    if (!st) {
        errno = EINVAL;
        return -1;
    }
    h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;

    type = GetFileType(h);
    if (type == FILE_TYPE_DISK) {
        st->st_mode = _S_IFREG | _S_IREAD | _S_IWRITE;
        low = GetFileSize(h, &high);
        if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
            errno = EBADF;
            return -1;
        }
        st->st_size = ((__int64)high << 32) | (__int64)low;
    } else if (type == FILE_TYPE_CHAR) {
        /* The console and NUL. `showmanyc` must then answer "I do not know",
           which it does as soon as this is not an ordinary file. */
        st->st_mode = _S_IFCHR;
    } else if (type == FILE_TYPE_PIPE) {
        st->st_mode = _S_IFIFO;
    } else {
        errno = EBADF;
        return -1;
    }
    return 0;
}


/* --- diagnostics --------------------------------------------------------- */

/* There is no debugger attached: the answer is always the same, and it is
   true. */
BOOL WINAPI IsDebuggerPresent(void)
{
    return FALSE;
}

/* --- thread placement ---------------------------------------------------- */

/* The target is single-processor. Accepting and doing nothing is the correct
   behaviour, not an approximation. */
BOOL WINAPI SetProcessAffinityMask(HANDLE process, DWORD_PTR mask)
{
    (void)process; (void)mask;
    return TRUE;
}

/* --- vectored exception handlers ----------------------------------------- */

/* Windows 95 only has `SetUnhandledExceptionFilter`, which is a single point and
   not a chain. libgcc uses it optionally and tests the return: returning NULL
   means "I could not register", which it knows how to handle. Lying by returning
   a non-null token would be worse - the following unregistration would bear on
   nothing. */
PVOID WINAPI AddVectoredExceptionHandler(ULONG first,
                                         PVECTORED_EXCEPTION_HANDLER handler)
{
    (void)first; (void)handler;
    return NULL;
}

ULONG WINAPI RemoveVectoredExceptionHandler(PVOID handle)
{
    (void)handle;
    return 0;
}

/* --- 64-bit monotonic clock ---------------------------------------------- */

/* `GetTickCount` returns to zero after 49.7 days. We accumulate the wraparounds
   to return a counter that does not.
 *
 * The logic is isolated as a pure function so that the wraparound can be tested
 * instead of waited out for seven weeks:
 * `platform/win95/tests/test_tick64.c` drives it with chosen values, on the
 * host, without Windows.
 *
 * Validity condition: being called at least once per 49-day period. A game loop
 * meets it comfortably; a program that slept longer between two readings would
 * miss a wraparound. That is a real limit, written down in
 * docs/WIN95-COMPAT.md.
 */
/* `dkr_tick64_step` lives in `tick64.c`: free of any Windows dependency, it can
   be driven by a host test. */


/* A spin lock rather than a critical section: `GetTickCount64` can be called
   from any thread, including during initialisation where no critical section is
   ready yet. `__sync_*` compiles to `lock cmpxchg`, with no system call. */
static volatile long   dkr_tick_lock  = 0;
static dkr_tick64_state dkr_tick_state = { 0, 0 };

ULONGLONG WINAPI GetTickCount64(void)
{
    unsigned long long v;
    while (!__sync_bool_compare_and_swap(&dkr_tick_lock, 0, 1)) { /* spin */ }
    v = dkr_tick64_step(&dkr_tick_state, (unsigned long)GetTickCount());
    __sync_lock_release(&dkr_tick_lock);
    return v;
}

/* --- critical sections ---------------------------------------------------- *
 *
 * Windows 95 offers no reliable way to *attempt* entry into a critical section,
 * and the first version of this file simply returned FALSE - a legitimate answer
 * under the contract, since every caller must allow for failure.
 *
 * It froze the whole machine. `winpthreads` loops on `TryEnterCriticalSection`
 * to take its locks; a perpetual failure gives a busy wait which, under
 * Windows 95, starves the scheduler to the point where even the taskbar clock
 * stops. The symptom is spectacular and the lesson is worth keeping: a
 * "legitimate" stub is not a harmless stub.
 *
 * We therefore supply **all five** critical-section functions, which lets us
 * dispose freely of CRITICAL_SECTION's 24 bytes: since the whole binary goes
 * through us, their meaning belongs to us alone.
 *
 *   LockCount      -> lock word: 0 free, 1 taken
 *   RecursionCount -> reentrancy depth
 *   OwningThread   -> owning thread's identifier
 *   LockSemaphore  -> wake-up semaphore
 *
 * The atomic exchange goes through `__sync_bool_compare_and_swap`, which GCC
 * translates to `lock cmpxchg` - a 486 instruction, with no system call. That is
 * what makes the whole thing possible: Windows 95 does not export
 * `InterlockedCompareExchange`, but the processor does know how to do it.
 */
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    cs->DebugInfo      = NULL;
    cs->LockCount      = 0;
    cs->RecursionCount = 0;
    cs->OwningThread   = NULL;
    cs->LockSemaphore  = CreateSemaphoreA(NULL, 0, 0x7FFFFFFF, NULL);
    cs->SpinCount      = 0;
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    DWORD me = GetCurrentThreadId();

    if ((DWORD)(ULONG_PTR)cs->OwningThread == me) {   /* already the owner */
        cs->RecursionCount++;
        return TRUE;
    }
    if (__sync_bool_compare_and_swap(&cs->LockCount, 0, 1)) {
        cs->OwningThread   = (HANDLE)(ULONG_PTR)me;
        cs->RecursionCount = 1;
        return TRUE;
    }
    return FALSE;
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    /* A timed wait rather than an infinite one: if a wake-up is lost between the
       test and the wait, the loop catches it on the next turn instead of
       sleeping forever. */
    while (!TryEnterCriticalSection(cs)) {
        if (cs->LockSemaphore) {
            WaitForSingleObject(cs->LockSemaphore, 1);
        } else {
            Sleep(0);
        }
    }
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    if (--cs->RecursionCount > 0) {
        return;
    }
    cs->OwningThread = NULL;
    __sync_lock_release(&cs->LockCount);
    if (cs->LockSemaphore) {
        ReleaseSemaphore(cs->LockSemaphore, 1, NULL);
    }
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    if (cs->LockSemaphore) {
        CloseHandle(cs->LockSemaphore);
        cs->LockSemaphore = NULL;
    }
    cs->LockCount      = 0;
    cs->RecursionCount = 0;
    cs->OwningThread   = NULL;
}


/* --- CreateSemaphoreW: exported, but empty (E02-S01) ---------------------- *
 *
 * This one is not of the same kind as the previous six. Those six were missing
 * from the export table, and their absence is loud: the program does not start,
 * and Windows names the symbol. `CreateSemaphoreW`, by contrast, *is* exported.
 * It simply does nothing:
 *
 *     0x03500a:  33 c0              xor  eax,eax     ; return 0
 *                b1 04              mov  cl,0x4      ; stub index
 *                e9 06 c3 fc ff     jmp  0x1319      ; common tail
 *     0x001319:  51                 push ecx
 *                68 78 00 00 00     push 0x78        ; ERROR_CALL_NOT_IMPLEMENTED
 *                e8 be c7 00 00     call SetLastError
 *
 * It shares its address with `CreateEventW`, which leaves no doubt: neither has
 * any code. Measured on the test machine's KERNEL32.DLL.
 *
 * Why this matters: `moodycamel::LightweightSemaphore` calls it, and it is the
 * blocking primitive *all* of `ultramodern`'s scheduler rests on - each game
 * thread's `running` semaphore, and every `BlockingConcurrentQueue`. With a null
 * handle, both sides break, and differently:
 *
 *   - `wait()`  -> `WaitForSingleObject(NULL, INFINITE)` fails instead of
 *     blocking. `ultramodern` ignores the return: the thread carries on as if it
 *     had been woken. The game threads, which must run one at a time, then all
 *     run at once.
 *   - `signal()` -> `while (!ReleaseSemaphore(NULL, ...));` - a loop that never
 *     ends. It is the same scheduler starvation as the first version of
 *     `TryEnterCriticalSection`, and the same symptom: the whole machine
 *     freezes.
 *
 * E01-S04's import check can see nothing here, since the symbol is properly
 * exported. That is why it is now paired with a list of known stubs
 * (`tools/win95/exports/stubs.json`).
 *
 * The workaround is immediate: `CreateSemaphoreA` exists and works. The name,
 * when there is one, is converted. `ultramodern` sets none - its semaphores are
 * anonymous - but returning an anonymous semaphore where the caller asked for a
 * named one would break sharing between processes without saying so.
 */
HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES attributes,
                               LONG initial_count, LONG maximum_count,
                               LPCWSTR name)
{
    /* MAX_PATH is the maximum length of a kernel object name: a shorter buffer
       would make the layer fail where the original API would have succeeded. */
    char  narrow[MAX_PATH + 1];
    char *narrow_name = NULL;
    BOOL  substituted = FALSE;

    if (name) {
        /* The buffer bounds the conversion: beyond it, WideCharToMultiByte
           fails with ERROR_INSUFFICIENT_BUFFER rather than writing out of
           bounds. We leave its error code in place instead of setting another -
           it says precisely what happened, and that is all the caller will be
           able to read.

           `substituted` is not an ornament. Without it, a character absent from
           the system code page silently becomes "?", and two different wide
           names collapse onto the same narrow name: two processes would believe
           they were opening distinct semaphores and would share one. Since
           converting the name has no purpose other than preserving that
           sharing, a substitution empties it of meaning - and we fail rather
           than lie. */
        int n = WideCharToMultiByte(CP_ACP, 0, name, -1, narrow,
                                    (int)sizeof(narrow), NULL, &substituted);
        if (n <= 0) {
            return NULL;
        }
        if (substituted) {
            SetLastError(ERROR_INVALID_NAME);
            return NULL;
        }
        narrow_name = narrow;
    }
    return CreateSemaphoreA(attributes, initial_count, maximum_count, narrow_name);
}


/* --- redirecting the import pointers -------------------------------------- *
 *
 * Defining the functions is not enough. `winpthreads` and `libstdc++` are
 * compiled with `__declspec(dllimport)`: their calls do not target the `_X@n`
 * symbol but the `__imp__X@n` pointer, which the `libkernel32.a` import library
 * would normally supply - and which would designate a function Windows 95 does
 * not have.
 *
 * We therefore define those pointers ourselves, making them designate our
 * implementations. The name carries the full stdcall decoration, argument size
 * included, hence the @0, @4 and @8 suffixes.
 *
 * To be linked with `-Wl,--whole-archive`: without it the archive is only
 * consulted at the point where it appears on the command line, before
 * `libwinpthread` has introduced the references - and `libkernel32.a`, placed
 * last by the compiler's specs, would win.
 */
#define REDIRECT(name, deco)                                                   \
    void *const __imp_##name __asm__("__imp__" #name deco) = (void *)&name

REDIRECT(IsDebuggerPresent,              "@0");
REDIRECT(InitializeCriticalSection,      "@4");
REDIRECT(EnterCriticalSection,           "@4");
REDIRECT(LeaveCriticalSection,           "@4");
REDIRECT(DeleteCriticalSection,          "@4");
REDIRECT(GetTickCount64,                 "@0");
REDIRECT(SetProcessAffinityMask,         "@8");
REDIRECT(TryEnterCriticalSection,        "@4");
REDIRECT(AddVectoredExceptionHandler,    "@8");
REDIRECT(RemoveVectoredExceptionHandler, "@4");
REDIRECT(CreateSemaphoreW,               "@16");
/* cdecl: no argument-size suffix, unlike the KERNEL32 functions above. */
REDIRECT(_fstat64,                       "");
REDIRECT(_wfopen_s,                      "");
REDIRECT(_wfreopen_s,                    "");
REDIRECT(_strtoi64,                      "");
REDIRECT(_strtoui64,                     "");
REDIRECT(strtoll,                        "");
REDIRECT(strtoull,                       "");
REDIRECT(GetModuleHandleExW,             "@12");
