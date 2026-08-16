/* E00-S02 - Witness T3a: the execution model as `ultramodern` writes it.
 *
 * Two threads, a mutex, a condition variable, plus a class hierarchy with
 * exceptions and RTTI. It is exactly the shape of the code to be ported, and
 * therefore the witness that decides whether `ultramodern` is patched or
 * rewritten.
 *
 * Only compiles with a C++11 standard library: Open Watcom has no <thread>. See
 * t3b.cpp for the Win32 equivalent. */
#include <thread>
#include <mutex>
#include <condition_variable>
#include <typeinfo>
#include <cstdio>
#include <cstring>
#include <windows.h>

struct Base        { virtual ~Base() {} virtual int kind() const { return 0; } };
struct Derived : Base { int kind() const { return 1; } };
struct Boom { const char *what() const { return "exception caught"; } };

static std::mutex m;
static std::condition_variable cv;
static bool ready = false;
static int counter = 0;

int main()
{
    char msg[512];
    const char *rtti = "?";
    const char *exc  = "no";

    std::thread producer([] {
        for (int i = 0; i < 1000; i++) { std::lock_guard<std::mutex> g(m); counter++; }
        { std::lock_guard<std::mutex> g(m); ready = true; }
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [] { return ready; });
    }
    producer.join();

    Derived d;
    Base *p = &d;
    rtti = (typeid(*p) == typeid(Derived)) ? "ok" : "FAILED";

    try { throw Boom(); } catch (const Boom &b) { exc = b.what(); }

    sprintf(msg, "T3a std::thread\r\n  counter  : %d / 1000\r\n"
                 "  RTTI     : %s\r\n  exception: %s\r\n", counter, rtti, exc);
    FILE *f = fopen("D:\\T3A.TXT", "wb");
    if (f) { fwrite(msg, 1, strlen(msg), f); fclose(f); }
    MessageBoxA(NULL, msg, "Witness T3a", 0x40);
    return 0;
}
