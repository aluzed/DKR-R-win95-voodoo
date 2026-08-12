/* E00-S02 — Temoin T3a : le modele d'execution tel que `ultramodern` l'ecrit.
 *
 * Deux fils, un mutex, une variable de condition, plus une hierarchie de classes
 * avec exceptions et RTTI. C'est exactement la forme du code a porter, et donc
 * le temoin qui decide si `ultramodern` se patche ou se reecrit.
 *
 * Ne se compile qu'avec une bibliotheque standard C++11 : Open Watcom n'a pas
 * <thread>. Voir t3b.cpp pour l'equivalent Win32. */
#include <thread>
#include <mutex>
#include <condition_variable>
#include <typeinfo>
#include <cstdio>
#include <cstring>
#include <windows.h>

struct Base        { virtual ~Base() {} virtual int kind() const { return 0; } };
struct Derived : Base { int kind() const { return 1; } };
struct Boom { const char *what() const { return "exception rattrapee"; } };

static std::mutex m;
static std::condition_variable cv;
static bool ready = false;
static int counter = 0;

int main()
{
    char msg[512];
    const char *rtti = "?";
    const char *exc  = "non";

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
    rtti = (typeid(*p) == typeid(Derived)) ? "ok" : "ECHEC";

    try { throw Boom(); } catch (const Boom &b) { exc = b.what(); }

    sprintf(msg, "T3a std::thread\r\n  compteur : %d / 1000\r\n"
                 "  RTTI     : %s\r\n  exception: %s\r\n", counter, rtti, exc);
    FILE *f = fopen("D:\\T3A.TXT", "wb");
    if (f) { fwrite(msg, 1, strlen(msg), f); fclose(f); }
    MessageBoxA(NULL, msg, "Temoin T3a", 0x40);
    return 0;
}
