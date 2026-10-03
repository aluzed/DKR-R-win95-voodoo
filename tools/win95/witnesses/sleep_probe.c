/* E02-S02 - how long Windows 95's Sleep really waits.
 *
 * ultramodern waits with Sleep on this target (timer.cpp's _WIN32 branch), so
 * the time granularity the scheduler reaches is Sleep's, not the clock's. Each
 * request is made 200 times and timed with QueryPerformanceCounter (the
 * 8254, 1,193,180 Hz on the test machine); the minimum, mean and maximum are
 * printed in microseconds, with timeBeginPeriod(1) and without.
 *
 *   SLEEPT.EXE > D:\SLEEPT.TXT
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

static void run(const char *label, LARGE_INTEGER hz)
{
    static const DWORD requests[] = {0, 1, 2, 5, 10, 16};
    int r;
    printf("%s\n", label);
    for (r = 0; r < (int)(sizeof(requests) / sizeof(requests[0])); r++) {
        double lo = 1e30, hi = 0.0, sum = 0.0;
        int i;
        for (i = 0; i < 200; i++) {
            LARGE_INTEGER a, b;
            double us;
            QueryPerformanceCounter(&a);
            Sleep(requests[r]);
            QueryPerformanceCounter(&b);
            us = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)hz.QuadPart;
            if (us < lo) { lo = us; }
            if (us > hi) { hi = us; }
            sum += us;
        }
        printf("  Sleep(%2lu): min %8.0f us  mean %8.0f us  max %8.0f us\n",
               (unsigned long)requests[r], lo, sum / 200.0, hi);
    }
}

int main(void)
{
    LARGE_INTEGER hz;
    if (!QueryPerformanceFrequency(&hz) || hz.QuadPart == 0) {
        printf("no performance counter\n");
        return 1;
    }
    printf("QueryPerformanceFrequency: %lu Hz\n", (unsigned long)hz.QuadPart);
    run("default timer period:", hz);
    timeBeginPeriod(1);
    run("after timeBeginPeriod(1):", hz);
    timeEndPeriod(1);
    return 0;
}
