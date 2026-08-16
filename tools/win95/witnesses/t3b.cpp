/* E00-S02 - Witness T3b: the same execution model, but written on the Win32
 * primitives Windows 95 actually has.
 *
 * CreateThread and CRITICAL_SECTION instead of std::thread and std::mutex; a
 * manual event instead of a condition variable. Exceptions and RTTI are kept
 * identical - they do not depend on the threading model.
 *
 * Compiles with both candidates, which makes it the point of comparison. */
#include <typeinfo>
#include <stdio.h>
#include <string.h>
#include <windows.h>

struct Base           { virtual ~Base() {} virtual int kind() const { return 0; } };
struct Derived : Base { int kind() const { return 1; } };
struct Boom { const char *what() const { return "exception caught"; } };

static CRITICAL_SECTION cs;
static HANDLE done_event;
static int counter = 0;

static DWORD WINAPI producer(LPVOID)
{
    for (int i = 0; i < 1000; i++) {
        EnterCriticalSection(&cs);
        counter++;
        LeaveCriticalSection(&cs);
    }
    SetEvent(done_event);
    return 0;
}

int main(void)
{
    char msg[512];
    const char *rtti = "?";
    const char *exc  = "no";
    DWORD tid = 0;

    InitializeCriticalSection(&cs);
    done_event = CreateEventA(NULL, TRUE, FALSE, NULL);

    HANDLE th = CreateThread(NULL, 0, producer, NULL, 0, &tid);
    if (!th) { MessageBoxA(NULL, "T3b: CreateThread failed", "T3b", 0x10); return 1; }

    WaitForSingleObject(done_event, 5000);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(done_event);
    DeleteCriticalSection(&cs);

    Derived d;
    Base *p = &d;
    rtti = (typeid(*p) == typeid(Derived)) ? "ok" : "FAILED";

    try { throw Boom(); } catch (const Boom &b) { exc = b.what(); }

    sprintf(msg, "T3b Win32\r\n  counter  : %d / 1000\r\n"
                 "  RTTI     : %s\r\n  exception: %s\r\n", counter, rtti, exc);
    FILE *f = fopen("D:\\T3B.TXT", "wb");
    if (f) { fwrite(msg, 1, strlen(msg), f); fclose(f); }
    MessageBoxA(NULL, msg, "Witness T3b", 0x40);
    return 0;
}
