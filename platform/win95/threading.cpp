/* E02-S01 — fils d'execution et synchronisation pour Windows 95.
 *
 * Le contrat, ce qui est perdu par rapport aux primitives standard, et le releve
 * des besoins reels d'`ultramodern` sont dans `threading.h` et dans
 * `docs/WIN95-THREADING.md`. Ce fichier ne contient que la mise en oeuvre.
 *
 * Deux implementations vivent ici :
 *
 *   - **Windows** — la cible. Uniquement des API que Windows 95 exporte *et*
 *     implemente ; la nuance n'est pas oratoire, voir la note sur
 *     `CreateSemaphoreW` plus bas.
 *
 *   - **POSIX** — un vehicule de test, et rien d'autre. Il existe pour que la
 *     suite de `tests/test_threading.cpp` s'execute aussi sur l'hote moderne,
 *     ou le cycle « modifier, executer, observer » coute une seconde au lieu
 *     d'un aller-retour vers la machine emulee. Il n'est pas une plate-forme
 *     supportee, et un test qui n'aurait passe que la n'a rien prouve de la
 *     cible : c'est pourquoi la meme suite est aussi construite en THREADS.EXE
 *     et executee sous Windows 95.
 */
#include "threading.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================== *
 * Etat commun aux deux implementations
 * ========================================================================== */

static dkr_threading_fatal_fn dkr_fatal_handler = 0;

/* Definie plus bas, une fois par implementation. Publique : voir threading.h. */
void dkr_threading_fatal(const char *message);

void dkr_threading_set_fatal_handler(dkr_threading_fatal_fn handler)
{
    dkr_fatal_handler = handler;
}

/* La correspondance des priorites. Fonction pure, sans dependance a Windows :
   elle se teste sur l'hote, et les constantes sont ecrites en clair pour que la
   table soit lisible sans consulter windows.h.

   Windows 95 expose sept classes de priorite de fil. `ultramodern` en distingue
   cinq. La correspondance est donc *injective* — aucun niveau ne s'ecrase — et
   la question de la perte ne se pose pas dans ce sens.

   Elle se pose dans l'autre : les priorites de la N64 vont de 0 a 255 et ne
   passent jamais par ici. Voir docs/WIN95-THREADING.md. */
#define DKR_W32_BELOW_NORMAL  (-1)
#define DKR_W32_NORMAL          0
#define DKR_W32_ABOVE_NORMAL    1
#define DKR_W32_HIGHEST         2
#define DKR_W32_TIME_CRITICAL  15

int dkr_thread_priority_to_win32(int priority)
{
    switch (priority) {
    case DKR_THREAD_PRIORITY_LOW:       return DKR_W32_BELOW_NORMAL;
    case DKR_THREAD_PRIORITY_NORMAL:    return DKR_W32_NORMAL;
    case DKR_THREAD_PRIORITY_HIGH:      return DKR_W32_ABOVE_NORMAL;
    case DKR_THREAD_PRIORITY_VERY_HIGH: return DKR_W32_HIGHEST;
    case DKR_THREAD_PRIORITY_CRITICAL:  return DKR_W32_TIME_CRITICAL;
    default:                            return DKR_THREAD_PRIORITY_INVALID;
    }
}

/* --- Emplacements locaux au fil, partie portable -------------------------- *
 *
 * Un seul emplacement systeme est consomme ; il designe ce bloc. Le compte
 * d'emplacements logiques est une constante du programme, pas une ressource du
 * systeme — ce qui compte sous Windows 95, ou le processus n'en a que 64 pour
 * tout le monde, libstdc++ et winpthreads compris.
 */
typedef struct {
    void *slots[DKR_TLS_SLOTS];
} dkr_tls_block;

/* Distributeur d'indices. Protege par un echange atomique plutot que par un
   verrou : `dkr_tls_reserve` peut etre appelee avant `dkr_threading_init`, donc
   avant qu'aucune section critique ne soit prete. */
static volatile long dkr_tls_next = 0;

static dkr_tls_block *dkr_tls_block_current(int create);

int dkr_tls_reserve(void)
{
    long slot;
    do {
        slot = dkr_tls_next;
        if (slot >= DKR_TLS_SLOTS) {
            return -1;
        }
    } while (!__sync_bool_compare_and_swap(&dkr_tls_next, slot, slot + 1));
    return (int)slot;
}

void *dkr_tls_get(int slot)
{
    dkr_tls_block *b;
    if (slot < 0 || slot >= DKR_TLS_SLOTS) {
        return 0;
    }
    /* Pas de creation en lecture : un fil qui n'a jamais rien pose lit zero,
       ce qui est la valeur initiale attendue d'une variable locale au fil. */
    b = dkr_tls_block_current(0);
    return b ? b->slots[slot] : 0;
}

void dkr_tls_set(int slot, void *value)
{
    dkr_tls_block *b;
    if (slot < 0 || slot >= DKR_TLS_SLOTS) {
        return;
    }
    b = dkr_tls_block_current(1);
    if (b) {
        b->slots[slot] = value;
    }
}


/* --- Variable de condition, commune aux deux implementations -------------- *
 *
 * Elle ne repose que sur `dkr_mutex` et `dkr_sem`, que les deux backends
 * fournissent. Il n'y a donc **qu'une** implementation, et non deux a garder en
 * phase — ce qui compte pour le morceau du ticket qui etait annonce comme le
 * plus delicat. Le raisonnement de correction est dans `threading.h`.
 */
int dkr_condvar_init(dkr_condvar *cv)
{
    if (!cv) {
        return 0;
    }
    cv->waiters     = 0;
    cv->initialised = 0;
    if (!dkr_mutex_init(&cv->guard)) {
        return 0;
    }
    if (!dkr_sem_init(&cv->sem, 0)) {
        dkr_mutex_destroy(&cv->guard);
        return 0;
    }
    cv->initialised = 1;
    return 1;
}

void dkr_condvar_destroy(dkr_condvar *cv)
{
    if (!cv || !cv->initialised) {
        return;
    }
    dkr_sem_destroy(&cv->sem);
    dkr_mutex_destroy(&cv->guard);
    cv->initialised = 0;
}

void dkr_condvar_notify_one(dkr_condvar *cv)
{
    if (!cv || !cv->initialised) {
        return;
    }
    dkr_mutex_lock(&cv->guard);
    if (cv->waiters > 0) {
        cv->waiters--;
        dkr_sem_signal(&cv->sem, 1);
    }
    dkr_mutex_unlock(&cv->guard);
}

void dkr_condvar_notify_all(dkr_condvar *cv)
{
    long n;
    if (!cv || !cv->initialised) {
        return;
    }
    dkr_mutex_lock(&cv->guard);
    n = cv->waiters;
    if (n > 0) {
        cv->waiters = 0;
        dkr_sem_signal(&cv->sem, n);
    }
    dkr_mutex_unlock(&cv->guard);
}

/* Coeur commun des deux attentes. `ms` negatif — represente par `timed == 0` —
   signifie « sans echeance ». */
static int dkr_condvar_wait_impl(dkr_condvar *cv, dkr_mutex *external,
                                 int timed, unsigned long ms)
{
    int woken;

    if (!cv || !cv->initialised || !external) {
        return 0;
    }

    /* L'inscription se fait **avant** de relacher le verrou de l'appelant.
       C'est ce qui garantit qu'un signaleur, qui ne peut agir qu'apres avoir
       obtenu ce meme verrou ou le notre, voit toujours l'attendeur. */
    dkr_mutex_lock(&cv->guard);
    cv->waiters++;
    dkr_mutex_unlock(&cv->guard);

    dkr_mutex_unlock(external);

    woken = timed ? dkr_sem_wait_timeout(&cv->sem, ms)
                  : dkr_sem_wait(&cv->sem);

    if (!woken) {
        /* L'echeance est passee. Un signal a pu etre emis entre l'expiration et
           cet instant : le jeton serait alors depose et notre compteur deja
           decremente. Le laisser trainerait un reveil pour personne, et le
           prochain attendeur repartirait sans raison. On le reprend donc. */
        dkr_mutex_lock(&cv->guard);
        if (dkr_sem_try_wait(&cv->sem)) {
            woken = 1;
        } else {
            cv->waiters--;
        }
        dkr_mutex_unlock(&cv->guard);
    }

    /* Reprise du verrou de l'appelant dans tous les cas, expiration comprise :
       c'est le contrat de `std::condition_variable`, et l'appelant ecrit son
       code en le supposant. */
    dkr_mutex_lock(external);
    return woken;
}

int dkr_condvar_wait(dkr_condvar *cv, dkr_mutex *external)
{
    return dkr_condvar_wait_impl(cv, external, 0, 0);
}

int dkr_condvar_wait_timeout(dkr_condvar *cv, dkr_mutex *external,
                             unsigned long ms)
{
    return dkr_condvar_wait_impl(cv, external, 1, ms);
}


#if defined(_WIN32)

/* ========================================================================== *
 * Windows — la cible
 * ========================================================================== */

#include <windows.h>
#include <process.h>

#include "startup.h"

/* Le blob de `dkr_mutex` doit contenir une CRITICAL_SECTION. La verification
   est ici et non dans l'en-tete, pour que celui-ci reste sans windows.h. */
static_assert(sizeof(CRITICAL_SECTION) <= sizeof(((dkr_mutex *)0)->reserved),
              "dkr_mutex::reserved trop petit pour une CRITICAL_SECTION");

void dkr_threading_fatal(const char *message)
{
    if (dkr_fatal_handler) {
        dkr_fatal_handler(message);
        return;
    }
    /* Le journal de demarrage est vide apres chaque ligne : celle-ci survivra a
       l'arret qui suit. C'est le seul canal utilisable — la machine cible n'a
       pas de console, et un jeu plein ecran n'en aurait pas l'usage. */
    dkr_win95_log(message);
    ExitProcess(3);
}

/* --- Emplacements locaux au fil ------------------------------------------- */

static DWORD dkr_tls_index     = 0xFFFFFFFFu;
static DWORD dkr_main_thread   = 0;

static dkr_tls_block *dkr_tls_block_current(int create)
{
    dkr_tls_block *b;

    if (dkr_tls_index == 0xFFFFFFFFu) {
        return 0;                       /* couche non initialisee */
    }
    b = (dkr_tls_block *)TlsGetValue(dkr_tls_index);
    if (!b && create) {
        b = (dkr_tls_block *)calloc(1, sizeof(*b));
        if (b && !TlsSetValue(dkr_tls_index, b)) {
            /* Sans cela le bloc serait perdu : personne n'en garderait
               l'adresse, et l'appel suivant en allouerait un autre. */
            free(b);
            b = 0;
        }
    }
    return b;
}

void dkr_tls_release_current(void)
{
    dkr_tls_block *b;
    if (dkr_tls_index == 0xFFFFFFFFu) {
        return;
    }
    b = (dkr_tls_block *)TlsGetValue(dkr_tls_index);
    if (b) {
        free(b);
        TlsSetValue(dkr_tls_index, 0);
    }
}

/* --- Mise en service ------------------------------------------------------ */

int dkr_threading_init(void)
{
    if (dkr_tls_index != 0xFFFFFFFFu) {
        return 1;                       /* deja en service */
    }
    dkr_tls_index = TlsAlloc();
    if (dkr_tls_index == 0xFFFFFFFFu) {
        return 0;
    }
    dkr_main_thread = GetCurrentThreadId();
    return 1;
}

void dkr_threading_shutdown(void)
{
    if (dkr_tls_index == 0xFFFFFFFFu) {
        return;
    }
    /* Seul le bloc du fil appelant est libere : les blocs des autres fils sont
       liberes par eux-memes en fin de vie, et il n'existe pas de moyen sous
       Windows 95 d'aller liberer celui d'un fil tiers. Appeler cette fonction
       alors que d'autres fils tournent encore fuit donc leur bloc — 32 octets
       chacun. C'est ecrit plutot que corrige : la seule correction possible
       serait de tenir un registre global des blocs, dont le verrou serait pris
       a chaque acces TLS. */
    dkr_tls_release_current();
    TlsFree(dkr_tls_index);
    dkr_tls_index = 0xFFFFFFFFu;
    /* Le distributeur repart de zero : sans cela un cycle
       init / shutdown / init ne recupererait jamais ses emplacements, et le
       second cycle epuiserait le stock sans raison visible. */
    dkr_tls_next = 0;
}

/* --- Fils ----------------------------------------------------------------- */

/* Deux allocations, et non une, parce qu'elles n'ont pas le meme proprietaire.
 *
 * `dkr_thread` appartient au createur, qui peut le relacher quand il veut —
 * c'est tout l'objet de `dkr_thread_release`. Le paquet de demarrage appartient
 * au fil cree, qui le recopie et le libere lui-meme.
 *
 * Les fondre en une seule structure serait une **utilisation apres liberation**,
 * et pas une theorique : sur un monoprocesseur, `_beginthreadex` rend la main au
 * createur qui garde son quantum, de sorte que le fil cree n'a en general pas
 * encore execute une seule instruction quand `dkr_thread_release` libere. Le fil
 * saute alors dans un `fn` recycle par le tas du CRT.
 *
 * Le cas n'est pas hypothetique : `ultramodern/src/timer.cpp` detache son fil de
 * minuterie immediatement apres l'avoir cree. */
typedef struct {
    dkr_thread_fn fn;
    void         *arg;
} dkr_thread_start_packet;

struct dkr_thread {
    HANDLE        handle;
    unsigned      id;
};

/* `_beginthreadex` plutot que `CreateThread` — une deviation deliberee de la
   lettre du ticket.
 *
 * `CreateThread` ne prepare pas l'etat par fil du CRT : `errno`, le tampon de
 * `strtok`, l'etat de `rand`. Un fil cree ainsi qui touche au CRT lit et ecrit
 * l'etat d'un autre fil, et le fuit a sa sortie. Les fils d'`ultramodern` y
 * touchent — ne serait-ce que par `debug_printf` et `std::string`.
 *
 * L'objection habituelle serait la dependance a MSVCRT.DLL. Elle est sans
 * objet : la table d'imports du temoin de E01-S03 la reclame deja pour
 * `__getmainargs`, `_initterm` et une vingtaine d'autres. `_beginthreadex` est
 * exportee par la MSVCRT.DLL de la machine de test — verifie contre sa table
 * d'exports, pas contre une documentation.
 *
 * `_beginthreadex` appelle `CreateThread`. L'esprit du ticket est tenu ; sa
 * lettre est corrigee. */
static unsigned __stdcall dkr_thread_trampoline(void *param)
{
    /* Recopie puis liberation immediate : a partir d'ici le fil ne touche plus
       a rien que le createur puisse liberer sous lui. */
    dkr_thread_start_packet packet = *(dkr_thread_start_packet *)param;
    free(param);

    packet.fn(packet.arg);

    /* Le bloc TLS appartient au fil : il meurt avec lui. Sans cela chaque fil
       de jeu cree et detruit en laisserait un derriere lui, et `ultramodern`
       en cree un par `osCreateThread`. */
    dkr_tls_release_current();
    return 0;
}

dkr_thread *dkr_thread_start(dkr_thread_fn fn, void *arg, unsigned long stack_bytes)
{
    dkr_thread              *t;
    dkr_thread_start_packet *packet;
    uintptr_t                h;

    if (!fn) {
        return 0;
    }
    t = (dkr_thread *)calloc(1, sizeof(*t));
    if (!t) {
        return 0;
    }
    packet = (dkr_thread_start_packet *)calloc(1, sizeof(*packet));
    if (!packet) {
        free(t);
        return 0;
    }
    packet->fn  = fn;
    packet->arg = arg;

    h = _beginthreadex(NULL, (unsigned)stack_bytes, dkr_thread_trampoline,
                       packet, 0, &t->id);
    if (h == 0) {
        free(packet);           /* le fil n'existe pas : personne ne le libere */
        free(t);
        return 0;
    }
    t->handle = (HANDLE)h;
    return t;
}

int dkr_thread_join(dkr_thread *t)
{
    DWORD r;
    if (!t) {
        return 0;
    }
    r = WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    free(t);
    return r == WAIT_OBJECT_0;
}

void dkr_thread_release(dkr_thread *t)
{
    if (!t) {
        return;
    }
    CloseHandle(t->handle);
    free(t);
}

unsigned long dkr_thread_id(void)
{
    return (unsigned long)GetCurrentThreadId();
}

void dkr_thread_set_priority(dkr_thread_priority priority)
{
    int mapped = dkr_thread_priority_to_win32((int)priority);
    if (mapped == DKR_THREAD_PRIORITY_INVALID) {
        return;
    }
    SetThreadPriority(GetCurrentThread(), mapped);
}

void dkr_sleep_ms(unsigned long ms)
{
    Sleep(ms);
}

void dkr_yield(void)
{
    /* `Sleep(0)` rend la main aux fils de priorite au moins egale. Windows 95
       n'a pas `SwitchToThread`, qui cederait aussi aux fils moins prioritaires. */
    Sleep(0);
}

/* --- Exclusion mutuelle --------------------------------------------------- */

int dkr_mutex_init(dkr_mutex *m)
{
    if (!m) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    InitializeCriticalSection((LPCRITICAL_SECTION)m->reserved);
    m->initialised = 1;
    return 1;
}

void dkr_mutex_destroy(dkr_mutex *m)
{
    if (!m || !m->initialised) {
        return;
    }
    DeleteCriticalSection((LPCRITICAL_SECTION)m->reserved);
    m->initialised = 0;
}

/* La lecture de `owner` hors verrou est sure, et pour une raison precise : la
   seule valeur qui declenche l'alarme est notre propre identifiant, que nous
   sommes seuls a pouvoir y avoir ecrit. Un autre fil n'y ecrit que le sien ou
   zero, et l'ecriture d'un mot aligne de 32 bits n'est pas dechirable sur x86.
   Le test ne peut donc ni manquer une reentrance ni en inventer une. */
void dkr_mutex_lock(dkr_mutex *m)
{
    unsigned long me = (unsigned long)GetCurrentThreadId();

    if (m->owner == me) {
        dkr_threading_fatal("dkr_mutex : reentrance — un std::mutex se serait interbloque ici");
        return;
    }
    EnterCriticalSection((LPCRITICAL_SECTION)m->reserved);
    m->owner = me;
}

int dkr_mutex_try_lock(dkr_mutex *m)
{
    unsigned long me = (unsigned long)GetCurrentThreadId();

    if (m->owner == me) {
        return 0;       /* deja tenu par nous : `try_lock` d'un std::mutex echoue */
    }
    if (!TryEnterCriticalSection((LPCRITICAL_SECTION)m->reserved)) {
        return 0;
    }
    m->owner = me;
    return 1;
}

void dkr_mutex_unlock(dkr_mutex *m)
{
    /* L'ordre compte : effacer le proprietaire avant de rendre la section. Dans
       l'autre sens, un fil pourrait prendre la section et poser son
       identifiant, que nous effacerions ensuite. */
    m->owner = 0;
    LeaveCriticalSection((LPCRITICAL_SECTION)m->reserved);
}

/* --- Semaphore ------------------------------------------------------------ *
 *
 * `CreateSemaphoreA`, et jamais `CreateSemaphoreW`. Sous Windows 95 la seconde
 * est **exportee mais vide** : trois instructions qui rendent zero et posent
 * `ERROR_CALL_NOT_IMPLEMENTED`. Elle partage son adresse avec `CreateEventW`,
 * signe qu'aucune des deux n'a de code. Verifie au desassemblage de la
 * KERNEL32.DLL de la machine de test — voir docs/research/win95-blockers.md.
 *
 * Le piege est serieux parce qu'il est silencieux : le lien reussit, le
 * chargement reussit, le controle d'imports de E01-S04 est satisfait puisque le
 * symbole *est* exporte. Seule l'execution differe.
 */
int dkr_sem_init(dkr_sem *s, long initial_count)
{
    if (!s) {
        return 0;
    }
    s->handle = (void *)CreateSemaphoreA(NULL, initial_count, 0x7FFFFFFF, NULL);
    return s->handle != 0;
}

void dkr_sem_destroy(dkr_sem *s)
{
    if (s && s->handle) {
        CloseHandle((HANDLE)s->handle);
        s->handle = 0;
    }
}

int dkr_sem_wait(dkr_sem *s)
{
    if (!s || !s->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)s->handle, INFINITE) == WAIT_OBJECT_0;
}

int dkr_sem_wait_timeout(dkr_sem *s, unsigned long ms)
{
    if (!s || !s->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)s->handle, ms) == WAIT_OBJECT_0;
}

int dkr_sem_try_wait(dkr_sem *s)
{
    if (!s || !s->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)s->handle, 0) == WAIT_OBJECT_0;
}

int dkr_sem_signal(dkr_sem *s, long count)
{
    if (!s || !s->handle || count <= 0) {
        return 0;
    }
    return ReleaseSemaphore((HANDLE)s->handle, count, NULL) != 0;
}

/* --- Evenement a reinitialisation manuelle -------------------------------- */

int dkr_event_init(dkr_event *e, int initially_set)
{
    if (!e) {
        return 0;
    }
    /* TRUE : reinitialisation manuelle. C'est ce qui distingue l'evenement du
       semaphore — il reveille tous les attendeurs et reste ouvert. */
    e->handle = (void *)CreateEventA(NULL, TRUE, initially_set ? TRUE : FALSE, NULL);
    return e->handle != 0;
}

void dkr_event_destroy(dkr_event *e)
{
    if (e && e->handle) {
        CloseHandle((HANDLE)e->handle);
        e->handle = 0;
    }
}

void dkr_event_set(dkr_event *e)
{
    if (e && e->handle) {
        SetEvent((HANDLE)e->handle);
    }
}

void dkr_event_reset(dkr_event *e)
{
    if (e && e->handle) {
        ResetEvent((HANDLE)e->handle);
    }
}

int dkr_event_wait(dkr_event *e)
{
    if (!e || !e->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)e->handle, INFINITE) == WAIT_OBJECT_0;
}

int dkr_event_wait_timeout(dkr_event *e, unsigned long ms)
{
    if (!e || !e->handle) {
        return 0;
    }
    return WaitForSingleObject((HANDLE)e->handle, ms) == WAIT_OBJECT_0;
}

#else

/* ========================================================================== *
 * POSIX — vehicule de test sur l'hote, pas une plate-forme supportee
 * ========================================================================== */

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static_assert(sizeof(pthread_mutex_t) <= sizeof(((dkr_mutex *)0)->reserved),
              "dkr_mutex::reserved trop petit pour un pthread_mutex_t");

void dkr_threading_fatal(const char *message)
{
    if (dkr_fatal_handler) {
        dkr_fatal_handler(message);
        return;
    }
    fprintf(stderr, "erreur fatale : %s\n", message);
    exit(3);
}

static pthread_key_t dkr_tls_key;
static int           dkr_tls_ready = 0;

static void dkr_tls_destructor(void *p)
{
    free(p);
}

static dkr_tls_block *dkr_tls_block_current(int create)
{
    dkr_tls_block *b;

    if (!dkr_tls_ready) {
        return 0;
    }
    b = (dkr_tls_block *)pthread_getspecific(dkr_tls_key);
    if (!b && create) {
        b = (dkr_tls_block *)calloc(1, sizeof(*b));
        if (b) {
            pthread_setspecific(dkr_tls_key, b);
        }
    }
    return b;
}

void dkr_tls_release_current(void)
{
    dkr_tls_block *b;
    if (!dkr_tls_ready) {
        return;
    }
    b = (dkr_tls_block *)pthread_getspecific(dkr_tls_key);
    if (b) {
        free(b);
        pthread_setspecific(dkr_tls_key, 0);
    }
}

int dkr_threading_init(void)
{
    if (dkr_tls_ready) {
        return 1;
    }
    if (pthread_key_create(&dkr_tls_key, dkr_tls_destructor) != 0) {
        return 0;
    }
    dkr_tls_ready = 1;
    return 1;
}

void dkr_threading_shutdown(void)
{
    if (!dkr_tls_ready) {
        return;
    }
    dkr_tls_release_current();
    pthread_key_delete(dkr_tls_key);
    dkr_tls_ready = 0;
    dkr_tls_next  = 0;          /* idem : voir la branche Windows */
}

/* Meme partage de propriete que sur la cible, et pour la meme raison : le
   createur peut relacher son descripteur avant que le fil n'ait demarre. */
typedef struct {
    dkr_thread_fn fn;
    void         *arg;
} dkr_thread_start_packet;

struct dkr_thread {
    pthread_t handle;
};

static void *dkr_thread_trampoline(void *param)
{
    dkr_thread_start_packet packet = *(dkr_thread_start_packet *)param;
    free(param);

    packet.fn(packet.arg);

    dkr_tls_release_current();
    return 0;
}

dkr_thread *dkr_thread_start(dkr_thread_fn fn, void *arg, unsigned long stack_bytes)
{
    dkr_thread              *t;
    dkr_thread_start_packet *packet;

    /* Non honoree ici, honoree sur la cible : une regression de taille de pile
       ne se manifestera donc que sur la machine. C'est ecrit dans threading.h. */
    (void)stack_bytes;

    if (!fn) {
        return 0;
    }
    t = (dkr_thread *)calloc(1, sizeof(*t));
    if (!t) {
        return 0;
    }
    packet = (dkr_thread_start_packet *)calloc(1, sizeof(*packet));
    if (!packet) {
        free(t);
        return 0;
    }
    packet->fn  = fn;
    packet->arg = arg;
    if (pthread_create(&t->handle, 0, dkr_thread_trampoline, packet) != 0) {
        free(packet);
        free(t);
        return 0;
    }
    return t;
}

int dkr_thread_join(dkr_thread *t)
{
    int r;
    if (!t) {
        return 0;
    }
    r = pthread_join(t->handle, 0);
    free(t);
    return r == 0;
}

void dkr_thread_release(dkr_thread *t)
{
    if (!t) {
        return;
    }
    pthread_detach(t->handle);
    free(t);
}

unsigned long dkr_thread_id(void)
{
    /* Suffisant pour ce dont la couche se sert : comparer deux fils. */
    return (unsigned long)(uintptr_t)pthread_self();
}

void dkr_thread_set_priority(dkr_thread_priority priority)
{
    /* L'hote n'est pas la cible : la priorite n'y est pas appliquee, seule la
       table de correspondance est testee — et elle l'est en fonction pure. */
    (void)priority;
}

void dkr_sleep_ms(unsigned long ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* reprise */
    }
}

void dkr_yield(void)
{
    sched_yield();
}

int dkr_mutex_init(dkr_mutex *m)
{
    if (!m) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    if (pthread_mutex_init((pthread_mutex_t *)m->reserved, 0) != 0) {
        return 0;
    }
    m->initialised = 1;
    return 1;
}

void dkr_mutex_destroy(dkr_mutex *m)
{
    if (!m || !m->initialised) {
        return;
    }
    pthread_mutex_destroy((pthread_mutex_t *)m->reserved);
    m->initialised = 0;
}

void dkr_mutex_lock(dkr_mutex *m)
{
    unsigned long me = dkr_thread_id();

    if (m->owner == me) {
        dkr_threading_fatal("dkr_mutex : reentrance — un std::mutex se serait interbloque ici");
        return;
    }
    pthread_mutex_lock((pthread_mutex_t *)m->reserved);
    m->owner = me;
}

int dkr_mutex_try_lock(dkr_mutex *m)
{
    unsigned long me = dkr_thread_id();

    if (m->owner == me) {
        return 0;
    }
    if (pthread_mutex_trylock((pthread_mutex_t *)m->reserved) != 0) {
        return 0;
    }
    m->owner = me;
    return 1;
}

void dkr_mutex_unlock(dkr_mutex *m)
{
    m->owner = 0;
    pthread_mutex_unlock((pthread_mutex_t *)m->reserved);
}

int dkr_sem_init(dkr_sem *s, long initial_count)
{
    sem_t *sem;
    if (!s) {
        return 0;
    }
    sem = (sem_t *)calloc(1, sizeof(*sem));
    if (!sem || sem_init(sem, 0, (unsigned)initial_count) != 0) {
        free(sem);
        s->handle = 0;
        return 0;
    }
    s->handle = sem;
    return 1;
}

void dkr_sem_destroy(dkr_sem *s)
{
    if (s && s->handle) {
        sem_destroy((sem_t *)s->handle);
        free(s->handle);
        s->handle = 0;
    }
}

int dkr_sem_wait(dkr_sem *s)
{
    int r;
    if (!s || !s->handle) {
        return 0;
    }
    while ((r = sem_wait((sem_t *)s->handle)) == -1 && errno == EINTR) {
        /* reprise */
    }
    return r == 0;
}

int dkr_sem_wait_timeout(dkr_sem *s, unsigned long ms)
{
    struct timespec ts;
    int r;

    if (!s || !s->handle) {
        return 0;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec  += 1;
    }
    while ((r = sem_timedwait((sem_t *)s->handle, &ts)) == -1 && errno == EINTR) {
        /* reprise */
    }
    return r == 0;
}

int dkr_sem_try_wait(dkr_sem *s)
{
    if (!s || !s->handle) {
        return 0;
    }
    return sem_trywait((sem_t *)s->handle) == 0;
}

int dkr_sem_signal(dkr_sem *s, long count)
{
    long i;
    if (!s || !s->handle || count <= 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (sem_post((sem_t *)s->handle) != 0) {
            return 0;
        }
    }
    return 1;
}

/* L'evenement a reinitialisation manuelle n'existe pas en POSIX : il se
   reconstitue avec un verrou, une variable de condition et un drapeau. C'est
   exactement la construction que la cible n'a *pas* a faire, puisque Windows 95
   offre l'objet en propre. */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             set;
} dkr_posix_event;

int dkr_event_init(dkr_event *e, int initially_set)
{
    dkr_posix_event *ev;
    if (!e) {
        return 0;
    }
    ev = (dkr_posix_event *)calloc(1, sizeof(*ev));
    if (!ev) {
        e->handle = 0;
        return 0;
    }
    pthread_mutex_init(&ev->mutex, 0);
    pthread_cond_init(&ev->cond, 0);
    ev->set   = initially_set ? 1 : 0;
    e->handle = ev;
    return 1;
}

void dkr_event_destroy(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_cond_destroy(&ev->cond);
    pthread_mutex_destroy(&ev->mutex);
    free(ev);
    e->handle = 0;
}

void dkr_event_set(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_mutex_lock(&ev->mutex);
    ev->set = 1;
    pthread_cond_broadcast(&ev->cond);
    pthread_mutex_unlock(&ev->mutex);
}

void dkr_event_reset(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_mutex_lock(&ev->mutex);
    ev->set = 0;
    pthread_mutex_unlock(&ev->mutex);
}

int dkr_event_wait(dkr_event *e)
{
    dkr_posix_event *ev;
    if (!e || !e->handle) {
        return 0;
    }
    ev = (dkr_posix_event *)e->handle;
    pthread_mutex_lock(&ev->mutex);
    while (!ev->set) {
        pthread_cond_wait(&ev->cond, &ev->mutex);
    }
    pthread_mutex_unlock(&ev->mutex);
    return 1;
}

int dkr_event_wait_timeout(dkr_event *e, unsigned long ms)
{
    dkr_posix_event *ev;
    struct timespec ts;
    int ok;

    if (!e || !e->handle) {
        return 0;
    }
    ev = (dkr_posix_event *)e->handle;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec  += 1;
    }
    pthread_mutex_lock(&ev->mutex);
    while (!ev->set) {
        if (pthread_cond_timedwait(&ev->cond, &ev->mutex, &ts) == ETIMEDOUT) {
            break;
        }
    }
    ok = ev->set;
    pthread_mutex_unlock(&ev->mutex);
    return ok;
}

#endif /* _WIN32 */
