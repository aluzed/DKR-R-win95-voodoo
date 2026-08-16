/* E01-S03 - witness exercising the whole platform layer.
 *
 * The ticket's acceptance criterion: "a witness executable using the whole layer
 * starts under emulated Windows 95". It therefore exercises, in order:
 *
 *   startup          log to a file, exception filter, version
 *   the six APIs     the ones Windows 95 does not export
 *   the 64-bit clock with the wraparound already covered by a host test
 *   threads          two threads, a critical section, an event
 *
 * It writes its result to D: like the other witnesses, and displays it.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "compat.h"
#include "startup.h"

static CRITICAL_SECTION cs;
static HANDLE done_event;
static long   counter = 0;

static DWORD WINAPI worker(LPVOID param)
{
    int i;
    (void)param;
    for (i = 0; i < 2000; i++) {
        EnterCriticalSection(&cs);
        counter++;
        LeaveCriticalSection(&cs);
    }
    SetEvent(done_event);
    return 0;
}

int main(void)
{
    char report[1024];
    int  n = 0;
    int  rc;
    HANDLE th;
    DWORD tid = 0;
    ULONGLONG t0, t1;
    BOOL tried;
    FILE *f;

    rc = dkr_win95_startup("Platform witness");
    if (rc != DKR_WIN95_STARTUP_OK) {
        return rc;                       /* the message has already been shown */
    }

    n += sprintf(report + n, "Witness for the Win95 platform layer\r\n");
    n += sprintf(report + n, "====================================\r\n");

    /* --- the six APIs Windows 95 does not export -------------------------- */
    dkr_win95_log("checking the compatibility APIs");

    n += sprintf(report + n, "IsDebuggerPresent      : %s\r\n",
                 IsDebuggerPresent() ? "true (unexpected)" : "false");
    n += sprintf(report + n, "SetProcessAffinityMask : %s\r\n",
                 SetProcessAffinityMask(GetCurrentProcess(), 1) ? "accepted" : "refused");

    /* --- 64-bit clock ----------------------------------------------------- */
    t0 = GetTickCount64();
    Sleep(120);
    t1 = GetTickCount64();
    n += sprintf(report + n, "GetTickCount64         : %lu ms elapsed\r\n",
                 (unsigned long)(t1 - t0));
    dkr_win95_log_num("clock delta (ms)", (long)(t1 - t0));

    /* --- critical sections and threads ------------------------------------ */
    InitializeCriticalSection(&cs);
    done_event = CreateEventA(NULL, TRUE, FALSE, NULL);

    /* The critical section is free: the attempt must succeed. This is the only
       place where `TryEnterCriticalSection` is genuinely exercised - Windows 95
       does not export it, and its first version in this layer froze the whole
       machine. */
    tried = TryEnterCriticalSection(&cs);
    if (tried) { LeaveCriticalSection(&cs); }
    n += sprintf(report + n, "TryEnterCriticalSection: %s\r\n",
                 tried ? "free lock taken" : "FAILED");

    th = CreateThread(NULL, 0, worker, NULL, 0, &tid);
    if (!th) {
        dkr_win95_log("CreateThread failed");
        n += sprintf(report + n, "threads                : creation FAILED\r\n");
    } else {
        int i;
        for (i = 0; i < 2000; i++) {     /* real contention between two threads */
            EnterCriticalSection(&cs);
            counter++;
            LeaveCriticalSection(&cs);
        }
        WaitForSingleObject(done_event, 10000);
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
        n += sprintf(report + n, "two threads, 4000 turns: counter = %ld / 4000\r\n",
                     counter);
    }
    CloseHandle(done_event);
    DeleteCriticalSection(&cs);
    dkr_win95_log_num("final counter", counter);

    n += sprintf(report + n, "\r\nstartup log: DKR-BOOT.LOG\r\n");

    f = fopen("D:\\PLATFORM.TXT", "wb");
    if (f) { fwrite(report, 1, (size_t)n, f); fclose(f); }

    MessageBoxA(NULL, report, "Platform witness", MB_ICONINFORMATION | MB_OK);
    dkr_win95_shutdown();
    return counter == 4000 ? 0 : 1;
}
