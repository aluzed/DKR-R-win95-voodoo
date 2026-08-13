/* E02-S02 — epreuve du pont C++, dans les formes exactes qu'`ultramodern` emploie.
 *
 * Le pont n'a pas a etre un `std::thread` complet : il a a etre correct sur les
 * quelques formes que le code appelant utilise vraiment. Chaque epreuve ci-dessous
 * reproduit donc une ligne reelle d'`ultramodern`, citee devant elle.
 *
 * Comme `test_threading.cpp`, une seule source pour les deux cibles.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../threading.hpp"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#endif

using dkr::win95::thread;
using dkr::win95::mutex;
using dkr::win95::lock_guard;
using dkr::win95::unique_lock;
using dkr::win95::condition_variable;
using dkr::win95::cv_status;

static int failures = 0;
static int checks   = 0;

static void expect(const char *what, long long got, long long want)
{
    checks++;
    if (got == want) {
        printf("  ok    %-52s %lld\n", what, got);
    } else {
        printf("  ECHEC %-52s attendu %lld, obtenu %lld\n", what, want, got);
        failures++;
    }
    fflush(stdout);
}

static void expect_true(const char *what, int cond)
{
    checks++;
    printf("  %s %s\n", cond ? "ok   " : "ECHEC", what);
    if (!cond) { failures++; }
    fflush(stdout);
}

/* ========================================================================== *
 * 1. Construction variadique, comme threads.cpp:273
 * ========================================================================== *
 *
 *   context->host_thread = std::thread{_thread_func, PASS_RDRAM t_, entrypoint,
 *                                      arg, t->context};
 *
 * Quatre arguments, de types differents, et une affectation par deplacement sur
 * un membre construit par defaut. C'est la forme la plus exigeante du fichier.
 */
static volatile long   v_sum   = 0;
static volatile long   v_calls = 0;

static void four_args(uint8_t *rdram, int id, const char *name, void *ctx)
{
    /* Les valeurs sont verifiees dans le fil, pas apres : c'est le passage
       d'arguments qui est teste. */
    if (rdram == (uint8_t *)0x1234 && id == 7 &&
        strcmp(name, "gfx") == 0 && ctx == (void *)0xABCD) {
        __sync_fetch_and_add(&v_sum, 1);
    }
    __sync_fetch_and_add(&v_calls, 1);
}

static void test_variadic_construction(void)
{
    thread t;                       /* membre construit par defaut */

    puts("Construction variadique et affectation par deplacement");
    expect_true("un fil construit par defaut n'est pas joignable", !t.joinable());

    v_sum = 0; v_calls = 0;
    t = thread{four_args, (uint8_t *)0x1234, 7, "gfx", (void *)0xABCD};
    expect_true("il devient joignable", t.joinable());
    t.join();
    expect_true("et ne l'est plus apres join", !t.joinable());

    expect("le corps a ete appele une fois", v_calls, 1);
    expect("les quatre arguments sont arrives intacts", v_sum, 1);
}

/* ========================================================================== *
 * 2. Les arguments sont copies, comme le fait std::thread
 * ========================================================================== *
 *
 * `events.cpp:576` passe `&gfx_thread_ready`, une variable locale — mais aussi
 * `rdram`, une valeur. Un pont qui garderait des references sur ses arguments
 * lirait une pile morte des que l'appelant rendrait la main.
 */
static volatile long copied_ok = 0;

static void takes_value(int by_value)
{
    if (by_value == 99) { __sync_fetch_and_add(&copied_ok, 1); }
}

static void test_arguments_are_copied(void)
{
    thread t;

    puts("Les arguments sont copies, pas referencees");
    copied_ok = 0;
    {
        int local = 99;
        t = thread{takes_value, local};
        local = 0;              /* si l'argument etait une reference, le fil lirait 0 */
    }
    t.join();
    expect("le fil a vu la valeur du moment de la construction", copied_ok, 1);
}

/* ========================================================================== *
 * 3. detach, comme timer.cpp:144-145
 * ========================================================================== *
 *
 *   timer_context.thread = std::thread{ timer_thread, PASS_RDRAM1 };
 *   timer_context.thread.detach();
 *
 * Detache immediatement apres construction — le cas ou le descripteur meurt
 * avant que le fil n'ait demarre.
 */
static dkr_sem detach_done;

static void detached_body(int n)
{
    (void)n;
    dkr_sem_signal(&detach_done, 1);
}

static void test_detach(void)
{
    thread t;

    puts("Detachement immediat");
    dkr_sem_init(&detach_done, 0);

    t = thread{detached_body, 5};
    t.detach();
    expect_true("apres detach, plus joignable", !t.joinable());
    expect_true("le fil detache s'est bien execute",
                dkr_sem_wait_timeout(&detach_done, 5000) != 0);
    dkr_sem_destroy(&detach_done);
}

/* ========================================================================== *
 * 4. lock_guard sous ses deux formes, comme events.cpp et renderer_context.cpp
 * ========================================================================== *
 *
 *   std::lock_guard lock{ events_context.message_mutex };      (deduction)
 *   std::lock_guard<std::mutex> lock(graphic_config_mutex);    (explicite)
 */
#define GUARD_ROUNDS 20000

static mutex guard_mutex;
static long  guard_counter = 0;

static void guard_body(int form)
{
    for (int i = 0; i < GUARD_ROUNDS; i++) {
        if (form == 0) {
            lock_guard lock{ guard_mutex };            /* forme par deduction */
            guard_counter++;
        } else {
            lock_guard<mutex> lock(guard_mutex);       /* forme explicite */
            guard_counter++;
        }
    }
}

static void test_lock_guard(void)
{
    puts("lock_guard, deduction et forme explicite");
    guard_counter = 0;

    thread a{guard_body, 0};
    thread b{guard_body, 1};
    a.join();
    b.join();

    expect("aucun increment perdu", guard_counter, 2 * GUARD_ROUNDS);
}

/* ========================================================================== *
 * 5. Les fautes de cycle de vie sont signalees
 * ========================================================================== *
 *
 * `std::thread` appelle `std::terminate` si on detruit un fil joignable, ou si
 * on ecrase par affectation un fil joignable. Le pont doit avoir le meme
 * caractere : une faute bruyante, pas une fuite discrete.
 */
static int fatal_seen = 0;

static void capture_fatal(const char *m) { (void)m; fatal_seen++; }

static void noop(void) {}

static void test_lifetime_faults(void)
{
    puts("Fautes de cycle de vie");
    dkr_threading_set_fatal_handler(capture_fatal);

    /* Ecraser un fil encore joignable. */
    fatal_seen = 0;
    {
        thread t{noop};
        thread u{noop};
        t = static_cast<thread &&>(u);      /* t est encore joignable : faute */
        expect("l'ecrasement d'un fil joignable est signale", fatal_seen, 1);
        /* Le gestionnaire a rendu la main, donc l'affectation a eu lieu : `t`
           tient desormais le fil de `u`, et `u` est vide. Un seul join. */
        t.join();
    }

    /* join sur un fil deja joint. */
    fatal_seen = 0;
    {
        thread t{noop};
        t.join();
        t.join();
        expect("le double join est signale", fatal_seen, 1);
    }

    dkr_threading_set_fatal_handler(0);
}

/* ========================================================================== *
 * 6. Variable de condition — la file de messages du patch 0013
 * ========================================================================== *
 *
 * Reproduction litterale d'`ExternalMessageQueue` : un producteur depose sous
 * verrou puis `notify_one`, un consommateur attend sur `wait(lock, predicat)`.
 *
 * Ce qui est etabli : rien ne se perd. Le producteur emet ses N messages aussi
 * vite qu'il peut, sans se soucier de savoir si le consommateur est deja en
 * attente — c'est exactement la fenetre ou une variable de condition mal batie
 * perd un reveil, et ou le consommateur s'endormirait pour toujours.
 */
#define CV_MESSAGES 3000

static mutex              q_mutex;
static condition_variable q_cond;
static int                q_buffer[CV_MESSAGES];
static int                q_count    = 0;
static long               q_consumed = 0;
static long               q_sum      = 0;

static void cv_producer(int n)
{
    for (int i = 1; i <= n; i++) {
        {
            lock_guard lock{ q_mutex };
            q_buffer[q_count++] = i;
        }
        q_cond.notify_one();
    }
}

static void cv_consumer(int n)
{
    for (int i = 0; i < n; i++) {
        unique_lock lock{ q_mutex };
        q_cond.wait(lock, [] { return q_count > 0; });
        q_sum += q_buffer[--q_count];
        q_consumed++;
    }
}

static void test_condition_variable(void)
{
    puts("Variable de condition : producteur / consommateur");

    q_count = 0; q_consumed = 0; q_sum = 0;

    /* Le consommateur demarre en premier et attend a vide ; puis le producteur
       le double. Les deux ordres sont ainsi exerces. */
    thread c{cv_consumer, CV_MESSAGES};
    thread p{cv_producer, CV_MESSAGES};
    p.join();
    c.join();

    expect("tous les messages consommes", q_consumed, CV_MESSAGES);
    expect("et aucun perdu ni compte deux fois", q_sum,
           (long long)CV_MESSAGES * (CV_MESSAGES + 1) / 2);
}

/* ========================================================================== *
 * 7. Le signal emis avant l'attente n'est pas perdu
 * ========================================================================== *
 *
 * La fenetre dangereuse, isolee : le producteur depose **tout** avant que le
 * consommateur n'existe. Une variable de condition sans memoire — et une
 * variable de condition n'en a pas — ne reveillerait personne ; c'est le
 * predicat qui doit sauver l'attente. On verifie que la combinaison des deux
 * tient.
 */
static void test_notify_before_wait(void)
{
    puts("Signal avant attente");

    q_count = 0; q_consumed = 0; q_sum = 0;

    cv_producer(10);                /* tout est depose, personne n'attend */
    q_cond.notify_all();            /* et le reveil part dans le vide */

    thread c{cv_consumer, 10};      /* le consommateur n'arrive qu'apres */
    c.join();

    expect("les dix messages sont retrouves", q_consumed, 10);
    expect("somme intacte", q_sum, 55);
}

/* ========================================================================== *
 * 8. notify_all reveille tous les attendeurs
 * ========================================================================== */
static mutex              bc_mutex;
static condition_variable bc_cond;
static bool               bc_open  = false;
static volatile long      bc_woken = 0;

static void bc_waiter(int unused)
{
    (void)unused;
    unique_lock lock{ bc_mutex };
    bc_cond.wait(lock, [] { return bc_open; });
    __sync_fetch_and_add(&bc_woken, 1);
}

static void test_notify_all(void)
{
    puts("notify_all");
    bc_open = false; bc_woken = 0;

    thread w1{bc_waiter, 1};
    thread w2{bc_waiter, 2};
    thread w3{bc_waiter, 3};
    dkr_sleep_ms(60);               /* les trois atteignent leur attente */

    {
        lock_guard lock{ bc_mutex };
        bc_open = true;
    }
    bc_cond.notify_all();

    w1.join(); w2.join(); w3.join();
    expect("un seul notify_all reveille les trois", bc_woken, 3);
}

/* ========================================================================== *
 * 9. wait_for expire, et rend le verrou repris
 * ========================================================================== *
 *
 * C'est la forme de `wait_external_message` : une boucle qui reteste sa
 * condition toutes les millisecondes. Deux choses doivent tenir — l'expiration
 * doit se produire quand rien n'arrive, et le verrou doit etre **repris** au
 * retour, sans quoi le `while` de l'appelant lirait sa condition sans
 * protection.
 */
static mutex              to_mutex;
static condition_variable to_cond;
static bool               to_flag = false;

static void test_wait_for_timeout(void)
{
    puts("wait_for : expiration et reprise du verrou");

    {
        unique_lock lock{ to_mutex };
        cv_status s = to_cond.wait_for(lock, std::chrono::milliseconds{30});
        expect("expire quand rien n'arrive",
               (long long)(s == cv_status::timeout), 1);
        expect_true("le verrou est repris au retour", lock.owns_lock());
        /* Preuve independante : si le verrou n'etait pas tenu, ce try_lock
           depuis ce meme fil reussirait au lieu d'echouer. */
        expect("le verrou est bien tenu par nous", (long long)to_mutex.try_lock(), 0);
    }

    /* Et il expire *non* quand un signal arrive. */
    to_flag = false;
    thread s{[](int) {
        dkr_sleep_ms(20);
        { lock_guard lock{ to_mutex }; to_flag = true; }
        to_cond.notify_one();
    }, 0};
    {
        unique_lock lock{ to_mutex };
        cv_status st = to_cond.wait_for(lock, std::chrono::milliseconds{4000});
        expect("ne rend pas timeout quand le signal arrive",
               (long long)(st == cv_status::no_timeout), 1);
        expect_true("et la condition est vraie", to_flag);
    }
    s.join();
}

/* ========================================================================== *
 * 10. Passage de relais repete — la fenetre du reveil perdu, martelee
 * ========================================================================== *
 *
 * Les epreuves precedentes exercent la variable de condition, mais mal la
 * fenetre qui la rend delicate : celle entre le relachement du verrou de
 * l'appelant et l'inscription de l'attendeur. Un signal emis la doit etre
 * conserve ; s'il tombe, l'attendeur dort pour toujours.
 *
 * Dans un flot producteur/consommateur ordinaire, un reveil perdu passe
 * inapercu — le message suivant en emet un autre, qui rattrape. Le seul moyen
 * de le rendre visible est de faire en sorte qu'il n'y ait **jamais** de message
 * suivant : un relais strict, un message a la fois, ou le consommateur doit
 * necessairement s'endormir et ou rien d'autre ne viendra le reveiller.
 *
 * Chaque tour retraverse la fenetre. Un ordonnancement fautif finit par tomber
 * dedans, et l'epreuve ne se termine plus.
 */
#define HANDOFF_ROUNDS 20000

static mutex              ho_mutex;
static condition_variable ho_to_consumer;
static condition_variable ho_to_producer;
static int                ho_item  = 0;      /* 0 : vide, 1 : plein */
static long               ho_taken = 0;

static void handoff_consumer(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        unique_lock lock{ ho_mutex };
        ho_to_consumer.wait(lock, [] { return ho_item != 0; });
        ho_item = 0;
        ho_taken++;
        ho_to_producer.notify_one();
    }
}

static void test_handoff(void)
{
    puts("Passage de relais strict (fenetre du reveil perdu)");
    ho_item = 0; ho_taken = 0;

    thread c{handoff_consumer, HANDOFF_ROUNDS};

    for (int i = 0; i < HANDOFF_ROUNDS; i++) {
        unique_lock lock{ ho_mutex };
        ho_to_producer.wait(lock, [] { return ho_item == 0; });
        ho_item = 1;
        ho_to_consumer.notify_one();
    }
    c.join();

    expect("20000 relais sans reveil perdu", ho_taken, HANDOFF_ROUNDS);
}

/* ========================================================================== */

int main(void)
{
#if defined(_WIN32)
    if (dkr_win95_startup("Epreuve du pont C++") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
#endif
    if (!dkr_threading_init()) {
        puts("ECHEC : dkr_threading_init");
        return 2;
    }

    test_variadic_construction();
    test_arguments_are_copied();
    test_detach();
    test_lock_guard();
    test_lifetime_faults();
    test_condition_variable();
    test_notify_before_wait();
    test_notify_all();
    test_wait_for_timeout();
    test_handoff();

    printf("\n%d controles, %d echec(s)\n", checks, failures);
    dkr_threading_shutdown();

#if defined(_WIN32)
    {
        FILE *f = fopen("D:\\THRCPP.LOG", "w");
        if (f) {
            fprintf(f, "pont C++ : %d controles, %d echec(s)\n", checks, failures);
            fclose(f);
        }
    }
#endif
    return failures != 0;
}
