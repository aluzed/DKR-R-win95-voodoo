/* E02-S01 — epreuve de la couche de fils et de synchronisation.
 *
 * **Une seule source, deux executions.** Compilee pour l'hote elle s'appuie sur
 * le vehicule POSIX de `threading.cpp` ; compilee pour la cible elle devient
 * THREADS.EXE et s'execute sous Windows 95 emule. C'est deliberement le meme
 * fichier : un test de synchronisation qui ne tourne que sur l'hote ne prouve
 * rien de la cible, et deux fichiers differents finissent toujours par diverger.
 *
 *   platform/win95/tests/run-tests.sh              sur l'hote
 *   scripts/Push-To-Win95-VM.sh build/win95/bin/THREADS.EXE   sur la cible
 *
 * Mode d'endurance, pour les defauts qui ne sortent pas en dix secondes :
 *
 *   ./test_threading --stress 600                  dix minutes
 *
 * Ce que chaque epreuve etablit est ecrit devant elle. Une epreuve dont on ne
 * sait pas dire ce qu'elle prouve ne prouve rien.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../threading.h"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#else
#include <time.h>
#endif

/* ========================================================================== *
 * Harnais
 * ========================================================================== */

static int failures = 0;
static int checks   = 0;

/* Le compte rendu part sur la sortie standard *et* dans un fichier.
 *
 * Sur la cible, le fichier n'est pas un confort : il est le seul canal. Un
 * programme lance depuis Windows 95 ecrit dans une fenetre DOS qui se referme
 * avec lui, et la machine est pilotee par capture d'ecran depuis l'hote — lire
 * quarante lignes ainsi n'est pas praticable. Le fichier atterrit sur D:, que
 * l'hote relit directement dans l'image FAT16.
 *
 * Il est vide apres chaque ligne, pour la meme raison que le journal de
 * demarrage : si une epreuve fige la machine, c'est la derniere ligne ecrite
 * qui dira laquelle.
 */
static FILE *report_file = NULL;

static void emit(const char *fmt, ...)
{
    char    line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    fputs(line, stdout);
    fflush(stdout);
    if (report_file) {
        fputs(line, report_file);
        fflush(report_file);
    }
}

static void expect(const char *what, long long got, long long want)
{
    checks++;
    if (got == want) {
        emit("  ok    %-52s %lld\n", what, got);
    } else {
        emit("  FAIL  %-52s attendu %lld, obtenu %lld\n", what, want, got);
        failures++;
    }
}

static void expect_true(const char *what, int condition)
{
    checks++;
    if (condition) {
        emit("  ok    %s\n", what);
    } else {
        emit("  FAIL  %s\n", what);
        failures++;
    }
}

static unsigned long now_ms(void)
{
#if defined(_WIN32)
    return (unsigned long)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

/* ========================================================================== *
 * 1. Correspondance des priorites — fonction pure, sans fil ni Windows
 * ========================================================================== *
 *
 * Ce qui est etabli : la table est celle qui est ecrite dans la documentation,
 * elle est strictement croissante, et elle n'ecrase aucun niveau. Le ticket
 * annonce une correspondance « lossy » ; elle ne l'est pas dans ce sens — les
 * cinq niveaux d'`ultramodern` tiennent dans les sept classes de Win32. La
 * perte reelle est ailleurs, et n'est pas testable ici : voir
 * docs/WIN95-THREADING.md.
 */
static void test_priority_mapping(void)
{
    int i;
    int previous;

    emit("Correspondance des priorites (fonction pure)\n");

    expect("Low        -> THREAD_PRIORITY_BELOW_NORMAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_LOW),       -1);
    expect("Normal     -> THREAD_PRIORITY_NORMAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_NORMAL),     0);
    expect("High       -> THREAD_PRIORITY_ABOVE_NORMAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_HIGH),       1);
    expect("VeryHigh   -> THREAD_PRIORITY_HIGHEST",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_VERY_HIGH),  2);
    expect("Critical   -> THREAD_PRIORITY_TIME_CRITICAL",
           dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_CRITICAL),  15);

    /* Strictement croissante : sans cela l'ordre voulu ne serait pas l'ordre
       obtenu, et deux niveaux distincts pourraient s'ecraser. */
    previous = dkr_thread_priority_to_win32(DKR_THREAD_PRIORITY_LOW);
    for (i = DKR_THREAD_PRIORITY_NORMAL; i <= DKR_THREAD_PRIORITY_CRITICAL; i++) {
        int current = dkr_thread_priority_to_win32(i);
        expect_true("croissance stricte de la table", current > previous);
        previous = current;
    }

    /* Hors domaine : refuse plutot que de rendre un niveau arbitraire. */
    expect("entree hors domaine refusee",
           dkr_thread_priority_to_win32(99), DKR_THREAD_PRIORITY_INVALID);
    expect("entree negative refusee",
           dkr_thread_priority_to_win32(-1), DKR_THREAD_PRIORITY_INVALID);
}

/* ========================================================================== *
 * 2. Creation et jonction
 * ========================================================================== *
 *
 * Ce qui est etabli : le fil s'execute reellement, la jonction attend sa fin —
 * et non seulement son demarrage — et l'identifiant du fil cree differe de
 * celui du createur.
 */
static long          created_ran   = 0;
static unsigned long created_id    = 0;
static unsigned long creator_id    = 0;

static void created_thread_body(void *arg)
{
    (void)arg;
    dkr_sleep_ms(30);           /* la jonction doit attendre ceci */
    created_id  = dkr_thread_id();
    created_ran = 1;
}

static void test_create_join(void)
{
    dkr_thread *t;

    emit("Creation et jonction\n");

    creator_id  = dkr_thread_id();
    created_ran = 0;
    created_id  = 0;

    t = dkr_thread_start(created_thread_body, 0, 0);
    expect_true("le fil demarre", t != 0);
    if (!t) {
        return;
    }
    expect_true("la jonction reussit", dkr_thread_join(t) != 0);
    /* Lu apres la jonction : si `join` rendait la main trop tot, ceci vaudrait
       encore zero. C'est la propriete que le test cherche. */
    expect("le corps du fil s'est execute", created_ran, 1);
    expect_true("l'identifiant du fil cree differe du createur",
                created_id != 0 && created_id != creator_id);
}

/* ========================================================================== *
 * 3. Detachement — le descripteur relache pendant que le fil demarre
 * ========================================================================== *
 *
 * Ce qui est etabli : relacher le descripteur *avant* que le fil n'ait commence
 * ne lui retire rien sous les pieds.
 *
 * C'est le cas le plus defavorable, et il n'a rien de theorique. Sur un
 * monoprocesseur, le createur garde son quantum apres avoir demarre le fil : au
 * moment ou `dkr_thread_release` s'execute, le fil cree n'a en general pas
 * encore execute une seule instruction. Si le descripteur portait aussi la
 * fonction et son argument, le fil sauterait ensuite dans une case liberee — et
 * recyclee par le tas du CRT.
 *
 * L'epreuve compte les fils qui sont reellement alles au bout, et le sémaphore
 * la rend deterministe : sans lui, elle se terminerait avant eux et ne
 * prouverait rien.
 *
 * `ultramodern/src/timer.cpp` detache son fil de minuterie immediatement apres
 * l'avoir cree : ce chemin est emprunte pour de bon.
 */
#define DETACH_THREADS 8

typedef struct {
    dkr_sem      *done;
    volatile long *ran;
} detach_arg;

static void detach_body(void *arg)
{
    detach_arg *a = (detach_arg *)arg;
    __sync_fetch_and_add(a->ran, 1);
    dkr_sem_signal(a->done, 1);
}

static void test_detach(void)
{
    dkr_sem       done;
    detach_arg    arg;
    volatile long ran = 0;
    int           i;
    int           collected = 0;

    emit("Detachement pendant le demarrage\n");

    expect_true("semaphore de fin", dkr_sem_init(&done, 0) != 0);
    arg.done = &done;
    arg.ran  = &ran;

    for (i = 0; i < DETACH_THREADS; i++) {
        dkr_thread *t = dkr_thread_start(detach_body, &arg, 0);
        if (!t) {
            break;
        }
        /* Immediatement, sans laisser au fil la moindre chance d'avoir demarre. */
        dkr_thread_release(t);
    }

    for (i = 0; i < DETACH_THREADS; i++) {
        if (dkr_sem_wait_timeout(&done, 5000)) {
            collected++;
        }
    }
    expect("les huit fils detaches sont alles au bout", collected, DETACH_THREADS);
    expect("et ont tous execute leur corps", ran, DETACH_THREADS);
    dkr_sem_destroy(&done);
}

/* ========================================================================== *
 * 4. Exclusion mutuelle sous contention
 * ========================================================================== *
 *
 * Ce qui est etabli : deux fils qui incrementent un compteur non atomique sous
 * verrou n'en perdent aucun. Le compteur est volontairement un `long` nu, sans
 * `atomic` : c'est le verrou qui est teste, pas le processeur.
 */
#define CONTENTION_ROUNDS 20000

static dkr_mutex contention_mutex;
static long      contention_counter = 0;

static void contention_body(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < CONTENTION_ROUNDS; i++) {
        dkr_mutex_lock(&contention_mutex);
        contention_counter++;
        dkr_mutex_unlock(&contention_mutex);
    }
}

static void test_mutual_exclusion(void)
{
    dkr_thread *a;
    dkr_thread *b;

    emit("Exclusion mutuelle sous contention\n");

    expect_true("initialisation du verrou", dkr_mutex_init(&contention_mutex) != 0);
    contention_counter = 0;

    a = dkr_thread_start(contention_body, 0, 0);
    b = dkr_thread_start(contention_body, 0, 0);
    expect_true("deux fils demarres", a != 0 && b != 0);
    if (a) { dkr_thread_join(a); }
    if (b) { dkr_thread_join(b); }

    expect("aucun increment perdu", contention_counter, 2 * CONTENTION_ROUNDS);
    dkr_mutex_destroy(&contention_mutex);
}

/* ========================================================================== *
 * 4. Non-reentrance du verrou
 * ========================================================================== *
 *
 * Ce qui est etabli : la difference de comportement entre CRITICAL_SECTION et
 * `std::mutex` est neutralisee. La section critique de Win32 est recursive ;
 * `std::mutex` ne l'est pas et s'interbloque. Un code qui comptait sur cet
 * interblocage pour reveler un defaut le reverrait passer sans bruit.
 *
 * La couche detecte la reentrance et le signale. Le test intercepte le
 * signalement — sans cela il arreterait le processus, ce qui est le
 * comportement voulu en production mais peu commode ici.
 */
static int reentrancy_reported = 0;

static void capture_fatal(const char *message)
{
    (void)message;
    reentrancy_reported = 1;
}

static void test_reentrancy_detected(void)
{
    dkr_mutex m;

    emit("Non-reentrance du verrou\n");

    dkr_mutex_init(&m);
    reentrancy_reported = 0;
    dkr_threading_set_fatal_handler(capture_fatal);

    dkr_mutex_lock(&m);
    dkr_mutex_lock(&m);           /* la faute : un std::mutex s'interbloquerait */
    expect("la reentrance est signalee", reentrancy_reported, 1);

    /* Le second verrouillage n'a pas eu lieu : un seul deverrouillage. */
    dkr_mutex_unlock(&m);

    /* `try_lock` sur son propre verrou echoue, comme celui d'un std::mutex,
       mais sans rien signaler : l'appelant a deja prevu l'echec. */
    reentrancy_reported = 0;
    expect_true("try_lock prend un verrou libre", dkr_mutex_try_lock(&m) != 0);
    expect("try_lock refuse la reentrance", dkr_mutex_try_lock(&m), 0);
    expect("try_lock ne signale rien", reentrancy_reported, 0);
    dkr_mutex_unlock(&m);

    dkr_threading_set_fatal_handler(0);
    dkr_mutex_destroy(&m);
}

/* ========================================================================== *
 * 5. Semaphore — le signal qui precede l'attente n'est pas perdu
 * ========================================================================== *
 *
 * Ce qui est etabli, et c'est la propriete dont depend le demarrage des fils de
 * jeu d'`ultramodern` : `osCreateThread` peut signaler `running` *avant* que le
 * fil cree n'atteigne son `wait`. Un primitif a memoire nulle — un evenement a
 * reinitialisation automatique mal employe, une variable de condition sans
 * predicat — perdrait ce reveil, et le fil dormirait pour toujours.
 */
static void test_semaphore_signal_before_wait(void)
{
    dkr_sem s;
    int i;

    emit("Semaphore : signal avant attente\n");

    expect_true("initialisation", dkr_sem_init(&s, 0) != 0);

    /* Trois jetons deposes avant toute attente. */
    dkr_sem_signal(&s, 1);
    dkr_sem_signal(&s, 2);

    for (i = 0; i < 3; i++) {
        expect_true("le jeton depose d'avance est rendu",
                    dkr_sem_wait_timeout(&s, 1000) != 0);
    }
    /* Et pas un de plus : `signal(n)` reveille exactement n fois. */
    expect("aucun jeton surnumeraire", dkr_sem_try_wait(&s), 0);

    /* Un compte initial non nul est un depot d'avance, lui aussi. */
    dkr_sem_destroy(&s);
    expect_true("initialisation a 2", dkr_sem_init(&s, 2) != 0);
    expect_true("premier jeton initial", dkr_sem_try_wait(&s) != 0);
    expect_true("second jeton initial",  dkr_sem_try_wait(&s) != 0);
    expect("puis plus rien", dkr_sem_try_wait(&s), 0);
    dkr_sem_destroy(&s);
}

/* ========================================================================== *
 * 6. Semaphore — aller-retour strict, la forme exacte du planificateur
 * ========================================================================== *
 *
 * Ce qui est etabli : le motif d'`ultramodern` lui-meme. Chaque fil de jeu dort
 * sur son propre semaphore ; en reveiller un et se rendormir est ce que fait
 * `run_next_thread_and_wait`. Un seul fil court a la fois, et l'alternance doit
 * etre stricte.
 *
 * Le compteur partage est non atomique **a dessein** : si l'alternance se
 * relachait, les deux fils y toucheraient en meme temps et le total final le
 * dirait. Le test detecte donc a la fois le reveil perdu — il se bloquerait —
 * et le reveil de trop.
 */
/* Deux fils, deux semaphores, chacun rend la main a l'autre. */
typedef struct {
    dkr_sem      *mine;
    dkr_sem      *other;
    int           rounds;
    int           tag;          /* 0 ou 1 : quel fil doit courir a ce tour */
    volatile int *turn;
    volatile int *broken;
    volatile long *counter;
} pp_arg;

static void pp_body(void *arg)
{
    pp_arg *p = (pp_arg *)arg;
    int i;

    for (i = 0; i < p->rounds; i++) {
        if (!dkr_sem_wait(p->mine)) {
            *p->broken = 1;
            return;
        }
        /* Personne d'autre ne doit courir ici : c'est ce que l'alternance
           stricte garantit, et ce que le compteur non atomique verifie. */
        if (*p->turn != p->tag) {
            *p->broken = 1;
        }
        *p->counter = *p->counter + 1;   /* pas d'increment compose sur volatile */
        *p->turn = 1 - p->tag;
        dkr_sem_signal(p->other, 1);
    }
}

#define PINGPONG_ROUNDS 5000

static int run_pingpong(int rounds)
{
    dkr_sem  sem_a, sem_b;
    pp_arg   arg_a, arg_b;
    dkr_thread *ta;
    dkr_thread *tb;
    volatile int  turn   = 0;
    volatile int  broken = 0;
    volatile long counter = 0;

    if (!dkr_sem_init(&sem_a, 0) || !dkr_sem_init(&sem_b, 0)) {
        return -1;
    }

    arg_a.mine = &sem_a; arg_a.other = &sem_b; arg_a.rounds = rounds;
    arg_a.tag = 0; arg_a.turn = &turn; arg_a.broken = &broken;
    arg_a.counter = &counter;

    arg_b.mine = &sem_b; arg_b.other = &sem_a; arg_b.rounds = rounds;
    arg_b.tag = 1; arg_b.turn = &turn; arg_b.broken = &broken;
    arg_b.counter = &counter;

    ta = dkr_thread_start(pp_body, &arg_a, 0);
    tb = dkr_thread_start(pp_body, &arg_b, 0);
    if (!ta || !tb) {
        return -1;
    }

    /* Le coup d'envoi : un seul jeton, pour un seul fil. */
    dkr_sem_signal(&sem_a, 1);

    dkr_thread_join(ta);
    dkr_thread_join(tb);
    dkr_sem_destroy(&sem_a);
    dkr_sem_destroy(&sem_b);

    if (broken) {
        return -1;
    }
    return (counter == (long)rounds * 2) ? 0 : -1;
}

static void test_pingpong(void)
{
    emit("Semaphore : aller-retour strict (motif du planificateur)\n");
    expect("5000 allers-retours sans reveil perdu ni de trop",
           run_pingpong(PINGPONG_ROUNDS), 0);
}

/* ========================================================================== *
 * 7. Evenement a reinitialisation manuelle — reveil de tous
 * ========================================================================== *
 *
 * Ce qui est etabli : ce que le semaphore ne sait pas faire. Un `set` unique
 * libere tous les attendeurs, presents et a venir, sans que le signaleur ait a
 * connaitre leur nombre.
 */
#define EVENT_WAITERS 4

typedef struct {
    dkr_event    *event;
    volatile long *woken;
} ev_arg;

static void event_body(void *arg)
{
    ev_arg *a = (ev_arg *)arg;
    if (dkr_event_wait(a->event)) {
        __sync_fetch_and_add(a->woken, 1);
    }
}

static void test_event_broadcast(void)
{
    dkr_event   e;
    dkr_thread *threads[EVENT_WAITERS];
    ev_arg      arg;
    volatile long woken = 0;
    int i;

    emit("Evenement a reinitialisation manuelle\n");

    expect_true("initialisation, ferme", dkr_event_init(&e, 0) != 0);
    expect("un evenement ferme ne laisse pas passer",
           dkr_event_wait_timeout(&e, 20), 0);

    arg.event = &e;
    arg.woken = &woken;
    for (i = 0; i < EVENT_WAITERS; i++) {
        threads[i] = dkr_thread_start(event_body, &arg, 0);
    }
    dkr_sleep_ms(50);            /* laisser les quatre atteindre leur attente */

    dkr_event_set(&e);           /* un seul signal pour quatre attendeurs */
    for (i = 0; i < EVENT_WAITERS; i++) {
        if (threads[i]) { dkr_thread_join(threads[i]); }
    }
    expect("un seul set reveille les quatre", woken, EVENT_WAITERS);

    /* Il reste ouvert : c'est ce qui distingue « manuelle » d'« automatique ». */
    expect_true("l'evenement reste ouvert apres le reveil",
                dkr_event_wait_timeout(&e, 20) != 0);

    dkr_event_reset(&e);
    expect("apres reset, il refuse a nouveau",
           dkr_event_wait_timeout(&e, 20), 0);
    dkr_event_destroy(&e);
}

/* ========================================================================== *
 * 8. Variables locales au fil
 * ========================================================================== *
 *
 * Ce qui est etabli : chaque fil voit sa propre valeur, un fil qui n'a rien
 * pose lit zero, et les emplacements sont distribues sans collision. Les trois
 * emplacements correspondent aux trois `thread_local` de
 * `ultramodern/src/threads.cpp`.
 */
#define TLS_THREADS 6

static int tls_slot_a = -1;
static int tls_slot_b = -1;

typedef struct {
    int           value;
    volatile long *mismatches;
} tls_arg;

static void tls_body(void *arg)
{
    tls_arg *a = (tls_arg *)arg;
    int i;

    /* Avant toute ecriture : un fil neuf lit zero. */
    if (dkr_tls_get(tls_slot_a) != 0 || dkr_tls_get(tls_slot_b) != 0) {
        __sync_fetch_and_add(a->mismatches, 1);
    }

    dkr_tls_set(tls_slot_a, (void *)(uintptr_t)a->value);
    dkr_tls_set(tls_slot_b, (void *)(uintptr_t)(a->value * 7));

    /* Relire apres avoir laisse les autres fils ecrire les leurs : c'est
       l'isolement qui est teste, pas la memoire. */
    for (i = 0; i < 200; i++) {
        dkr_yield();
        if (dkr_tls_get(tls_slot_a) != (void *)(uintptr_t)a->value ||
            dkr_tls_get(tls_slot_b) != (void *)(uintptr_t)(a->value * 7)) {
            __sync_fetch_and_add(a->mismatches, 1);
            return;
        }
    }
}

static void test_tls(void)
{
    dkr_thread   *threads[TLS_THREADS];
    tls_arg       args[TLS_THREADS];
    volatile long mismatches = 0;
    int i;
    int extra;

    emit("Variables locales au fil\n");

    tls_slot_a = dkr_tls_reserve();
    tls_slot_b = dkr_tls_reserve();
    expect_true("deux emplacements distincts",
                tls_slot_a >= 0 && tls_slot_b >= 0 && tls_slot_a != tls_slot_b);

    /* Le fil principal aussi : il n'est pas un cas particulier. */
    dkr_tls_set(tls_slot_a, (void *)(uintptr_t)0xABCD);

    for (i = 0; i < TLS_THREADS; i++) {
        args[i].value      = i + 1;
        args[i].mismatches = &mismatches;
        threads[i] = dkr_thread_start(tls_body, &args[i], 0);
    }
    for (i = 0; i < TLS_THREADS; i++) {
        if (threads[i]) { dkr_thread_join(threads[i]); }
    }

    expect("aucun fil n'a vu la valeur d'un autre", mismatches, 0);
    expect("le fil principal a conserve la sienne",
           (long long)(uintptr_t)dkr_tls_get(tls_slot_a), 0xABCD);

    /* Le stock est fini et le dit. Mieux vaut un -1 franc qu'un emplacement
       silencieusement partage avec un autre usage. */
    extra = 0;
    while (dkr_tls_reserve() >= 0) {
        if (++extra > DKR_TLS_SLOTS + 4) {
            break;              /* garde-fou : le distributeur ne s'arrete pas */
        }
    }
    expect("le distributeur s'epuise au compte annonce",
           extra, DKR_TLS_SLOTS - 2);
}

/* ========================================================================== *
 * 9. Le bouchon CreateSemaphoreW est neutralise    (cible uniquement)
 * ========================================================================== *
 *
 * Ce qui est etabli : sur la machine, `CreateSemaphoreW` rend un semaphore qui
 * fonctionne — et non le zero que rendrait le bouchon de KERNEL32.
 *
 * Cette epreuve est la seule du fichier qui n'ait pas d'equivalent sur l'hote,
 * et elle en vaut la peine : c'est elle qui separe une machine ou
 * `ultramodern` peut tourner d'une machine ou son planificateur se disloque en
 * silence. Elle echouerait sur un Windows 95 sans le pont de `compat.c`, ce qui
 * est exactement ce qu'on lui demande de surveiller.
 *
 * L'appel passe par la variante large **volontairement**. C'est le seul endroit
 * du projet ou elle est appelee de propos delibere.
 */
#if defined(_WIN32)
static void test_wide_semaphore_shim(void)
{
    HANDLE h;
    LONG   previous = 0;

    emit("Bouchon CreateSemaphoreW neutralise\n");

    h = CreateSemaphoreW(NULL, 0, 16, NULL);
    expect_true("CreateSemaphoreW rend un descripteur", h != NULL);
    if (!h) {
        /* Sans descripteur, la suite ne mesurerait que des echecs derives. */
        return;
    }
    expect_true("le semaphore rendu se signale",
                ReleaseSemaphore(h, 1, &previous) != 0);
    expect("il etait bien a zero avant", (long long)previous, 0);
    expect("et le jeton se reprend",
           (long long)WaitForSingleObject(h, 1000), (long long)WAIT_OBJECT_0);
    CloseHandle(h);
}
#endif

/* ========================================================================== *
 * 10. Endurance
 * ========================================================================== *
 *
 * Les defauts de synchronisation sont rares et non deterministes. Une execution
 * de dix secondes ne les trouve pas ; c'est pourquoi le ticket demande dix
 * minutes sous charge, dans la machine emulee, et non une relecture.
 *
 * La boucle enchaine les deux motifs qui peuvent perdre un reveil — l'aller-
 * retour du planificateur et la contention sur verrou — et s'arrete a la
 * premiere anomalie plutot qu'a la fin du temps imparti.
 */
static int stress(unsigned long seconds)
{
    unsigned long start = now_ms();
    unsigned long elapsed;
    long          iterations = 0;

    emit("Endurance : %lu secondes\n", seconds);
    fflush(stdout);

    for (;;) {
        if (run_pingpong(500) != 0) {
            emit("  FAIL  aller-retour rompu au tour %ld\n", iterations);
            return 1;
        }

        contention_counter = 0;
        if (!dkr_mutex_init(&contention_mutex)) {
            emit("  FAIL  initialisation du verrou\n");
            return 1;
        }
        {
            dkr_thread *a = dkr_thread_start(contention_body, 0, 0);
            dkr_thread *b = dkr_thread_start(contention_body, 0, 0);
            if (!a || !b) {
                emit("  FAIL  creation de fil\n");
                return 1;
            }
            dkr_thread_join(a);
            dkr_thread_join(b);
        }
        dkr_mutex_destroy(&contention_mutex);
        if (contention_counter != 2 * CONTENTION_ROUNDS) {
            emit("  FAIL  increment perdu au tour %ld : %ld\n",
                   iterations, contention_counter);
            return 1;
        }

        iterations++;
        /* Soustraction non signee : elle reste juste si `GetTickCount`
           reboucle pendant l'epreuve, ce qui ne peut arriver qu'apres 49,7
           jours mais ne coute rien a couvrir ici. */
        elapsed = now_ms() - start;
        if ((elapsed / 1000u) >= seconds) {
            break;
        }
        if ((iterations % 20) == 0) {
            emit("  %lu s, %ld tours\n", elapsed / 1000u, iterations);
            fflush(stdout);
        }
    }

    emit("  ok    %ld tours sans reveil perdu ni interblocage\n", iterations);
    return 0;
}

/* ========================================================================== *
 * Point d'entree
 * ========================================================================== */

static void run_all(void)
{
    test_priority_mapping();
    test_create_join();
    test_detach();
    test_mutual_exclusion();
    test_reentrancy_detected();
    test_semaphore_signal_before_wait();
    test_pingpong();
    test_event_broadcast();
    test_tls();
#if defined(_WIN32)
    test_wide_semaphore_shim();
#endif
}

int main(int argc, char **argv)
{
    unsigned long stress_seconds = 0;
    int i;
    int rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stress") == 0 && i + 1 < argc) {
            stress_seconds = strtoul(argv[++i], 0, 10);
        }
    }

#if defined(_WIN32)
    /* Sur la cible, le journal de demarrage est le seul canal qui survive a un
       arret brutal. Sans ecran ni console, c'est lui qui portera la derniere
       ligne ecrite avant un gel. */
    if (dkr_win95_startup("Epreuve fils et synchronisation") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
    /* Ouvert avant la premiere epreuve, pas apres la derniere : un compte rendu
       ecrit a la fin ne dit rien de l'epreuve qui a fige la machine.
       Deux noms, pour que l'endurance n'ecrase pas le resultat de la suite —
       les deux se lancent dans le meme demarrage de la machine. */
    report_file = fopen(stress_seconds > 0 ? "D:\\STRESS.LOG"
                                           : "D:\\THREADS.LOG", "w");
#else
    {
        const char *path = getenv("DKR_THREADS_LOG");
        if (path) {
            report_file = fopen(path, "w");
        }
    }
#endif

    if (!dkr_threading_init()) {
        emit("ECHEC : dkr_threading_init\n");
        return 2;
    }

    if (stress_seconds > 0) {
        rc = stress(stress_seconds);
    } else {
        run_all();
        emit("\n%d controles, %d echec(s)\n", checks, failures);
        rc = failures != 0;
    }

    dkr_threading_shutdown();

    emit("resultat : %s\n", rc == 0 ? "OK" : "ECHEC");
    if (report_file) {
        fclose(report_file);
        report_file = NULL;
    }
#if defined(_WIN32)
    dkr_win95_log(rc == 0 ? "epreuve : OK" : "epreuve : ECHEC");
#endif

    return rc;
}
