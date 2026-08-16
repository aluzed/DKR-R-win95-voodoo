/* E02-S02 - does `thread_local` work under Windows 95?
 *
 * GCC implements `thread_local` through a .tls section and the PE's TLS
 * directory. Windows 9x is reputed not to process that directory; if it does
 * not, every thread shares the same slot, and the three thread-locals in
 * `ultramodern/src/threads.cpp` - `thread_self` among them - tread on each
 * other. That would be a silent and catastrophic defect.
 *
 * We do not assume it: two threads each write their own value, synchronise to
 * guarantee the interleaving, then read theirs back.
 */
#include <windows.h>
#include <stdio.h>

thread_local int         tl_value = 0;
thread_local const char *tl_name  = "none";

static HANDLE written, reread;
static int    seen_by_thread = -1;
static const char *name_seen  = "?";

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    tl_value = 222;                 /* the thread sets ITS value */
    tl_name  = "thread";
    SetEvent(written);              /* the main thread can write its own */
    WaitForSingleObject(reread, 5000);
    seen_by_thread = tl_value;      /* must be 222, not 111 */
    name_seen      = tl_name;
    return 0;
}

int main(void)
{
    HANDLE th; DWORD tid; FILE *f;
    int main_ok, thread_ok;

    written = CreateEventA(NULL, TRUE, FALSE, NULL);
    reread  = CreateEventA(NULL, TRUE, FALSE, NULL);

    tl_value = 111;
    tl_name  = "main";

    th = CreateThread(NULL, 0, worker, NULL, 0, &tid);
    WaitForSingleObject(written, 5000);   /* the thread has written 222 */

    /* If TLS is not isolated, our 111 was overwritten by the thread's 222. */
    main_ok = (tl_value == 111);
    SetEvent(reread);
    WaitForSingleObject(th, 5000);
    thread_ok = (seen_by_thread == 222);

    f = fopen("D:\\TLSPROBE.TXT", "w");
    if (f) {
        fprintf(f, "thread_local under Windows 95\n");
        fprintf(f, "  main thread    : expected 111, read %d  (%s)\n",
                tl_value, tl_name);
        fprintf(f, "  created thread : expected 222, read %d  (%s)\n",
                seen_by_thread, name_seen);
        fprintf(f, "  verdict        : %s\n",
                (main_ok && thread_ok) ? "ISOLATED - thread_local works"
                                       : "SHARED - thread_local is unusable");
        fclose(f);
    }
    printf("main %d, thread %d -> %s\n", tl_value, seen_by_thread,
           (main_ok && thread_ok) ? "ISOLATED" : "SHARED");
    return (main_ok && thread_ok) ? 0 : 1;
}
