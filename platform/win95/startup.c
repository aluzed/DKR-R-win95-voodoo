/* E01-S03 - startup for the Windows 95 target. See startup.h. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "startup.h"

/* The log lives next to the executable and not in the current directory:
   launched from the Start menu, a program inherits a current directory that has
   nothing to do with where the user will go looking for the file. */
#define DKR_LOG_NAME "DKR-BOOT.LOG"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static char   g_app[64] = "DKR-R";

static void write_raw(const char *s, DWORD n)
{
    DWORD written = 0;
    if (g_log != INVALID_HANDLE_VALUE) {
        WriteFile(g_log, s, n, &written, NULL);
        /* Immediate flush. Without it, the last line - the one that would say
           where the program died - would stay in the cache and vanish with the
           process. That is exactly the line that matters. */
        FlushFileBuffers(g_log);
    }
}

void dkr_win95_log(const char *message)
{
    if (!message) { return; }
    write_raw(message, (DWORD)strlen(message));
    write_raw("\r\n", 2);
}

void dkr_win95_log_num(const char *message, long value)
{
    char buf[32];
    int n;
    if (message) { write_raw(message, (DWORD)strlen(message)); }
    n = sprintf(buf, " %ld\r\n", value);
    write_raw(buf, (DWORD)n);
}

/* --- abnormal-exit cleanups ------------------------------------------------ *
 *
 * See startup.h for the why. Here, only the constraints of the context: we may
 * be called from an exception filter, hence without allocating anything, and the
 * `g_cleanups_done` guard stops a second pass - filter then normal exit, or two
 * threads crashing together - from replaying the cleanups.
 */
static dkr_win95_cleanup_fn g_cleanups[DKR_WIN95_MAX_CLEANUPS];
static int                  g_cleanup_count = 0;
static long                 g_cleanups_done = 0;

int dkr_win95_at_abnormal_exit(dkr_win95_cleanup_fn cleanup)
{
    if (!cleanup || g_cleanup_count >= DKR_WIN95_MAX_CLEANUPS) {
        return 0;
    }
    g_cleanups[g_cleanup_count++] = cleanup;
    return 1;
}

void dkr_win95_run_cleanups(void)
{
    int i;

    /* One pass only, however many callers there are. `lock cmpxchg` rather than
       a critical section: we may be here because the process is already damaged,
       and taking a lock would be the best way to end up stuck instead of dying
       cleanly. */
    if (!__sync_bool_compare_and_swap(&g_cleanups_done, 0, 1)) {
        return;
    }
    /* Reverse order of registration: a subsystem undone before the one it
       depends on. */
    for (i = g_cleanup_count - 1; i >= 0; i--) {
        g_cleanups[i]();
    }
}

/* --- exception filter ------------------------------------------------------
 *
 * Windows 95 does not have vectored handlers; `SetUnhandledExceptionFilter` is
 * the only hook, and it is enough: we do not try to recover from the exception,
 * only to leave a readable trace of it before dying.
 */
static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "invalid memory access";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "floating-point divide by zero";
    case EXCEPTION_FLT_INVALID_OPERATION: return "invalid floating-point operation";
    case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
    case EXCEPTION_IN_PAGE_ERROR:         return "paging error";
    default:                              return "unknown exception";
    }
}

static LONG WINAPI on_unhandled(EXCEPTION_POINTERS *info)
{
    char buf[256];
    DWORD code = info->ExceptionRecord->ExceptionCode;

    /* **Close the diagnostic log before anything else.**
     *
     * The runtime redirects `stderr` to a file with no buffering, so the bytes go
     * to the system as they come. But Windows 95 only updates the **size in the
     * directory entry** on close: a process that dies leaves a zero-byte file
     * whose contents are nonetheless on the disk and lost to any tooling that
     * reads the table.
     *
     * Observed right here: a crash produced an empty `DKRR.LOG` while the trace
     * it contained was precisely what we were looking for. */
    fclose(stderr);

    dkr_win95_log("");
    dkr_win95_log("*** unhandled exception ***");
    sprintf(buf, "  code    : 0x%08lX (%s)", (unsigned long)code, exception_name(code));
    dkr_win95_log(buf);
    sprintf(buf, "  address : 0x%08lX",
            (unsigned long)(ULONG_PTR)info->ExceptionRecord->ExceptionAddress);
    dkr_win95_log(buf);

    /* **The faulting address and the registers, not only the code address.**
     *
     * `ExceptionAddress` says *where* the program stopped; it does not say *what
     * it was touching*. On a port whose entire guest address space is an indexed
     * array - `mov -0x7ffffff0(%ebp,%ecx,1),%edx` is the typical shape of
     * recompiled code, `ebp` carrying the RDRAM base and `ecx` the guest address
     * - it is the touched address that names the defect, and the registers that
     * say which guest address produced it.
     *
     * Without that, every fault needs a disassembly by hand to guess what was
     * missing. With it, it reads. */
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR what  = info->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR where = info->ExceptionRecord->ExceptionInformation[1];
        sprintf(buf, "  touching: 0x%08lX on %s",
                (unsigned long)where,
                (what == 0) ? "read" : (what == 1) ? "write" : "execute");
        dkr_win95_log(buf);
    }
    if (info->ContextRecord != NULL) {
        const CONTEXT *c = info->ContextRecord;
        sprintf(buf, "  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX",
                (unsigned long)c->Eax, (unsigned long)c->Ebx,
                (unsigned long)c->Ecx, (unsigned long)c->Edx);
        dkr_win95_log(buf);
        sprintf(buf, "  esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
                (unsigned long)c->Esi, (unsigned long)c->Edi,
                (unsigned long)c->Ebp, (unsigned long)c->Esp);
        dkr_win95_log(buf);
        /* The **guest** address, reconstructed: in recompiled code the RDRAM
           base lives in a register, and the touched address minus that base
           gives back the address the game believed it was reading. That is the
           one to compare against the N64's memory map. */
        if (info->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR where = info->ExceptionRecord->ExceptionInformation[1];
            sprintf(buf, "  guest  ~ 0x%08lX if the base is ebp,"
                         " 0x%08lX if it is ebx",
                    (unsigned long)(where - c->Ebp + 0x80000000u),
                    (unsigned long)(where - c->Ebx + 0x80000000u));
            dkr_win95_log(buf);
        }
    }

    /* **Dump the guest structure the registers designate.**
     *
     * In recompiled code one register carries the RDRAM base and the others
     * carry guest addresses in KSEG0 - recognisable by their 0x80 high byte. A
     * null-pointer fault says nothing about what should have been there; the
     * state of the neighbouring structure does.
     *
     * So we dump sixteen words from every register that looks like a guest
     * address, translating through the assumed base. The report becomes readable
     * without attaching a debugger to a machine that does not have one. */
    if (info->ContextRecord != NULL) {
        const CONTEXT *c = info->ContextRecord;
        const DWORD regs[6] = { c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi };
        const char  *names[6] = { "eax", "ebx", "ecx", "edx", "esi", "edi" };
        /* The RDRAM base is the register whose value is a plausible host pointer
           and whose distance from the touched address gives back KSEG0. */
        const DWORD base = c->Ebp;
        int r;
        int dumped = 0;
        for (r = 0; r < 6; r++) {
            const DWORD v = regs[r];
            if ((v & 0xFF000000u) != 0x80000000u) { continue; }
            if (dumped++ > 0) { break; }
            {
                const unsigned char *p =
                    (const unsigned char *)(base + (v - 0x80000000u));
                unsigned i;
                /* **Far enough to reach the fields that matter.**
                 *
                 * A first version dumped only four lines, and that misled: the
                 * first sixteen words of an `OSSched` are its two message
                 * templates, and `curRSPTask` lives at offset 0x274. Concluding
                 * "the structure is empty" from its header is concluding about
                 * something other than what is being looked at.
                 *
                 * Forty lines cover 640 bytes, which is enough for the N64
                 * operating system's structures. */
                sprintf(buf, "  %s -> 0x%08lX :", names[r], (unsigned long)v);
                dkr_win95_log(buf);
                for (i = 0; i < 40; i++) {
                    /* Big-endian: guest RDRAM is stored as it is, and displaying
                       it little-endian would make pointers unrecognisable. */
                    sprintf(buf, "    +%02X  %02X%02X%02X%02X %02X%02X%02X%02X "
                                 "%02X%02X%02X%02X %02X%02X%02X%02X",
                            i * 16,
                            p[i*16+0], p[i*16+1], p[i*16+2], p[i*16+3],
                            p[i*16+4], p[i*16+5], p[i*16+6], p[i*16+7],
                            p[i*16+8], p[i*16+9], p[i*16+10], p[i*16+11],
                            p[i*16+12], p[i*16+13], p[i*16+14], p[i*16+15]);
                    dkr_win95_log(buf);
                }
            }
        }
    }

    /* **The host stack, for the call chain.**
     *
     * Windows 95 does not have `StackWalk64`, and recompiled code has no usable
     * stack frame: `-fomit-frame-pointer` is the rule on functions generated by
     * the hundreds of thousands. What remains is the oldest and most robust
     * method - walk the stack and keep everything that looks like a code
     * address.
     *
     * This is not an exact call stack: dead values from earlier frames linger in
     * it. But on a fault where we do not know how we got there, a list of
     * candidates beats nothing by a very wide margin, and the addresses resolve
     * offline with `nm` on the executable. */
    if (info->ContextRecord != NULL) {
        const CONTEXT *c = info->ContextRecord;
        const DWORD *sp = (const DWORD *)c->Esp;
        DWORD base = 0, size = 0;
        MEMORY_BASIC_INFORMATION mbi;
        /* The code's bounds: without them we would keep any integer at all. */
        if (VirtualQuery((LPCVOID)(ULONG_PTR)c->Eip, &mbi, sizeof(mbi))) {
            base = (DWORD)(ULONG_PTR)mbi.AllocationBase;
            size = 0x00A00000u;           /* the image fits comfortably inside */
        }
        if (base != 0) {
            unsigned i, found = 0;
            dkr_win95_log("  stack (plausible code addresses):");
            for (i = 0; i < 256u && found < 16u; i++) {
                const DWORD v = sp[i];
                if (v > base && v < base + size) {
                    sprintf(buf, "    esp+%03X  0x%08lX",
                            i * 4u, (unsigned long)v);
                    dkr_win95_log(buf);
                    found++;
                }
            }
        }
    }

    /* `EXCEPTION_ILLEGAL_INSTRUCTION` deserves a word: on this target it is the
       symptom of an instruction later than the Pentium II having escaped
       E01-S01's check. Writing it here saves an hour of searching. */
    if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
        dkr_win95_log("  lead    : instruction outside the Pentium II set? "
                      "see tools/win95/check-instruction-set.sh");
    }

    /* Before the dialog box, not after: the user may leave it open for hours,
       and the settings to undo degrade the machine for as long as they hold. */
    dkr_win95_run_cleanups();
    dkr_win95_log("  abnormal-exit cleanups run");

    sprintf(buf, "%s stopped on a %s.\n\nDetails in " DKR_LOG_NAME ".",
            g_app, exception_name(code));
    MessageBoxA(NULL, buf, g_app, MB_ICONERROR | MB_OK);

    return EXCEPTION_EXECUTE_HANDLER;
}

/* --- version check --------------------------------------------------------- */

static int check_version(void)
{
    OSVERSIONINFOA v;
    char buf[256];

    v.dwOSVersionInfoSize = sizeof(v);
    if (!GetVersionExA(&v)) {
        /* Not knowing is no reason to refuse: on a system too old to answer, we
           would already have failed at load time. */
        dkr_win95_log("system version: undetermined, carrying on");
        return DKR_WIN95_STARTUP_OK;
    }

    sprintf(buf, "system: platform %lu, version %lu.%lu build %lu",
            (unsigned long)v.dwPlatformId, (unsigned long)v.dwMajorVersion,
            (unsigned long)v.dwMinorVersion, (unsigned long)(v.dwBuildNumber & 0xFFFF));
    dkr_win95_log(buf);
    if (v.szCSDVersion[0]) { dkr_win95_log(v.szCSDVersion); }

    /* Win32s is a 32-bit layer laid over Windows 3.1: it has neither threads nor
       half of KERNEL32. The refusal must be explicit. */
    if (v.dwPlatformId == VER_PLATFORM_WIN32s) {
        MessageBoxA(NULL,
                    "Win32s on Windows 3.1 is not supported.\n\n"
                    "This program requires Windows 95 or later.",
                    g_app, MB_ICONERROR | MB_OK);
        dkr_win95_log("REFUSED: Win32s");
        return DKR_WIN95_STARTUP_TOO_OLD;
    }

    /* Windows 95 is 4.0. Any version 4.0 and above will do, NT included - the
       binary runs there too, which makes development less painful. */
    if (v.dwMajorVersion < 4) {
        sprintf(buf, "Windows %lu.%lu is older than Windows 95.\n\n"
                     "This program requires Windows 95 or later.",
                (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion);
        MessageBoxA(NULL, buf, g_app, MB_ICONERROR | MB_OK);
        dkr_win95_log("REFUSED: system older than Windows 95");
        return DKR_WIN95_STARTUP_TOO_OLD;
    }

    return DKR_WIN95_STARTUP_OK;
}

/* --- startup ---------------------------------------------------------------- */

static void log_path_next_to_exe(char *out, DWORD cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, cap);
    if (n == 0 || n >= cap) {
        strcpy(out, DKR_LOG_NAME);       /* fallback: current directory */
        return;
    }
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/') { n--; }
    out[n] = '\0';
    if (n + sizeof(DKR_LOG_NAME) >= cap) { out[0] = '\0'; }
    strcat(out, DKR_LOG_NAME);
}

int dkr_win95_startup(const char *app_name)
{
    char path[MAX_PATH + 32];
    char buf[MAX_PATH + 64];
    int rc;

    if (app_name && *app_name) {
        strncpy(g_app, app_name, sizeof(g_app) - 1);
        g_app[sizeof(g_app) - 1] = '\0';
    }

    /* `CreateFileA`, not `fopen`: the log must be able to open before any CRT
       initialisation, since its very purpose is to diagnose what fails early.
       And `...A`, never `...W` - under Windows 9x the Unicode family is a stub
       that fails with ERROR_CALL_NOT_IMPLEMENTED. */
    log_path_next_to_exe(path, sizeof(path));
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE) {
        /* A log that cannot be opened is not fatal in itself, but it announces a
           permissions or path problem better reported at once than discovered
           later with no trace. */
        sprintf(buf, "Cannot write the startup log:\n%s", path);
        MessageBoxA(NULL, buf, g_app, MB_ICONWARNING | MB_OK);
        return DKR_WIN95_STARTUP_NO_LOG;
    }

    dkr_win95_log("=== startup log ===");
    dkr_win95_log(g_app);
    dkr_win95_log(path);

    SetUnhandledExceptionFilter(on_unhandled);
    dkr_win95_log("exception filter installed");

    rc = check_version();
    if (rc != DKR_WIN95_STARTUP_OK) { return rc; }

    dkr_win95_log("startup complete");
    return DKR_WIN95_STARTUP_OK;
}

void dkr_win95_shutdown(void)
{
    if (g_log != INVALID_HANDLE_VALUE) {
        dkr_win95_log("=== end ===");
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}
