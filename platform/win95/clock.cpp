/* E02-S03 — base de temps monotone pour Windows 95.
 *
 * Le contrat, les mesures qui l'ont dicte et ce qui differe de ce que le ticket
 * supposait sont dans `clock.h` et `docs/research/win95-clock.md`. Ce fichier
 * ne contient que la mise en oeuvre.
 *
 * Comme `threading.cpp`, il porte deux implementations : Windows, la cible, et
 * un vehicule POSIX qui n'existe que pour faire tourner la meme suite de tests
 * sur l'hote. La conversion vers le compteur du VR4300 et l'accumulation des
 * 32 bits, elles, sont communes — ce sont des fonctions pures.
 */
#include "clock.h"
#include "compat.h"

#include <stddef.h>

/* ========================================================================== *
 * Commun : la conversion vers le compteur du VR4300
 * ========================================================================== */

unsigned long long dkr_clock_ticks_to_vr4300(unsigned long long ticks,
                                             unsigned long long frequency)
{
    unsigned long long whole, remainder;

    if (frequency == 0) {
        return 0;
    }
    /* `ticks * 46875000` deborderait au-dela d'environ 46 heures. On separe le
       quotient du reste : `remainder` est plus petit que `frequency`, donc son
       produit par 46 875 000 tient dans 64 bits avec une marge enorme tant que
       la frequence reste sous 393 GHz. */
    whole     = ticks / frequency;
    remainder = ticks % frequency;

    return whole * DKR_VR4300_COUNTER_HZ
         + (remainder * DKR_VR4300_COUNTER_HZ) / frequency;
}


#if defined(_WIN32)

/* ========================================================================== *
 * Windows — la cible
 * ========================================================================== */

#include <windows.h>
#include <mmsystem.h>

#include "startup.h"

static dkr_clock_source   clock_source    = DKR_CLOCK_SOURCE_NONE;
static unsigned long long clock_frequency = 0;
static unsigned long long clock_origin    = 0;
static int                period_begun    = 0;

/* Le seul geste qui doit survivre a un arret anormal. Isole de
   `dkr_clock_shutdown` parce qu'un nettoyage appele depuis un filtre
   d'exception doit faire le strict minimum : pas d'etat a remettre a zero, pas
   d'allocation, rien qui puisse bloquer. */
static void dkr_clock_release_period(void)
{
    if (period_begun) {
        timeEndPeriod(1);
        period_begun = 0;
    }
}

/* Etat d'accumulation du repli 32 bits. Voir `tick64.c` : meme raisonnement,
   meme fonction, un seul exemplaire. */
static dkr_tick64_state   timegettime_state = { 0, 0 };

/* --- Validation ------------------------------------------------------------ *
 *
 * Une source ne se retient pas parce qu'elle repond, mais parce qu'elle se
 * comporte. On lui demande donc de ne jamais reculer sur un echantillonnage
 * serre. C'est bon marche — quelques milliers de lectures — et cela ecarte au
 * lancement une source dont le defaut se manifesterait autrement en cours de
 * partie, sous la forme d'un chronometre qui saute.
 */
#define VALIDATION_SAMPLES 4096

static int qpc_is_sane(unsigned long long *frequency_out)
{
    LARGE_INTEGER freq, previous, now;
    int i;

    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
        return 0;
    }
    if (!QueryPerformanceCounter(&previous)) {
        return 0;
    }
    for (i = 0; i < VALIDATION_SAMPLES; i++) {
        if (!QueryPerformanceCounter(&now)) {
            return 0;
        }
        if (now.QuadPart < previous.QuadPart) {
            return 0;                       /* un recul suffit a la disqualifier */
        }
        previous = now;
    }
    *frequency_out = (unsigned long long)freq.QuadPart;
    return 1;
}

static unsigned long long qpc_raw(void)
{
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return (unsigned long long)v.QuadPart;
}

static unsigned long long timegettime_raw(void)
{
    /* 32 bits, rebouclage a 49,7 jours, accumule par la fonction de E01-S03. */
    return dkr_tick64_step(&timegettime_state, (unsigned long)timeGetTime());
}

int dkr_clock_init(void)
{
    unsigned long long frequency = 0;

    if (clock_source != DKR_CLOCK_SOURCE_NONE) {
        return 1;                           /* deja en service */
    }

    /* `timeBeginPeriod(1)` d'abord, parce que le repli en depend et parce que
       la mesure sur la cible montre qu'il ne coute rien. Sur cette machine il
       ne change rien non plus — `timeGetTime` rend deja la milliseconde — mais
       rien ne garantit qu'il en aille de meme ailleurs. */
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        period_begun = 1;
        /* Un `timeBeginPeriod` laisse en place degrade tout le systeme jusqu'au
           redemarrage, et survit donc au processus. Il ne suffit pas de le
           relacher a l'arret normal : on s'annonce aupres du filtre
           d'exceptions, pour qu'il soit defait meme si l'on meurt. */
        dkr_win95_at_abnormal_exit(&dkr_clock_release_period);
    }

    if (qpc_is_sane(&frequency)) {
        clock_source    = DKR_CLOCK_SOURCE_QPC;
        clock_frequency = frequency;
        clock_origin    = qpc_raw();
    } else {
        /* Le repli n'est pas un pis-aller silencieux : il est nomme dans le
           journal, parce qu'une partie qui tourne sur une horloge a la
           milliseconde plutot qu'a la microseconde se comporte differemment et
           qu'il faut pouvoir le savoir sans deviner. */
        dkr_win95_log("horloge : QueryPerformanceCounter ecartee, repli timeGetTime");
        clock_source    = DKR_CLOCK_SOURCE_TIMEGETTIME;
        clock_frequency = 1000;
        timegettime_state.high = 0;
        timegettime_state.last = 0;
        clock_origin    = timegettime_raw();
    }

    dkr_win95_log_num("horloge : frequence (Hz)", (long)clock_frequency);
    return 1;
}

void dkr_clock_shutdown(void)
{
    /* Sous Windows 9x, un `timeBeginPeriod` laisse en place degrade tout le
       systeme jusqu'au redemarrage — y compris apres la fin du processus. Le
       relacher n'est donc pas une politesse. */
    dkr_clock_release_period();
    clock_source    = DKR_CLOCK_SOURCE_NONE;
    clock_frequency = 0;
}

unsigned long long dkr_clock_now(void)
{
    unsigned long long raw;

    switch (clock_source) {
    case DKR_CLOCK_SOURCE_QPC:         raw = qpc_raw();          break;
    case DKR_CLOCK_SOURCE_TIMEGETTIME: raw = timegettime_raw();  break;
    default:                           return 0;
    }
    return raw - clock_origin;
}

#else

/* ========================================================================== *
 * POSIX — vehicule de test, pas une plate-forme supportee
 * ========================================================================== */

#include <time.h>

static dkr_clock_source   clock_source    = DKR_CLOCK_SOURCE_NONE;
static unsigned long long clock_frequency = 0;
static unsigned long long clock_origin    = 0;

static unsigned long long monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL
         + (unsigned long long)ts.tv_nsec;
}

int dkr_clock_init(void)
{
    if (clock_source != DKR_CLOCK_SOURCE_NONE) {
        return 1;
    }
    clock_source    = DKR_CLOCK_SOURCE_QPC;   /* l'equivalent le plus proche */
    clock_frequency = 1000000000ULL;
    clock_origin    = monotonic_ns();
    return 1;
}

void dkr_clock_shutdown(void)
{
    clock_source    = DKR_CLOCK_SOURCE_NONE;
    clock_frequency = 0;
}

unsigned long long dkr_clock_now(void)
{
    if (clock_source == DKR_CLOCK_SOURCE_NONE) {
        return 0;
    }
    return monotonic_ns() - clock_origin;
}

#endif /* _WIN32 */


/* ========================================================================== *
 * Commun : ce qui se deduit de `dkr_clock_now`
 * ========================================================================== */

dkr_clock_source dkr_clock_source_in_use(void) { return clock_source; }
unsigned long long dkr_clock_frequency(void)   { return clock_frequency; }

const char *dkr_clock_source_name(void)
{
    switch (clock_source) {
    case DKR_CLOCK_SOURCE_QPC:         return "QueryPerformanceCounter";
    case DKR_CLOCK_SOURCE_TIMEGETTIME: return "timeGetTime";
    default:                           return "aucune";
    }
}

unsigned long long dkr_clock_now_us(void)
{
    unsigned long long ticks = dkr_clock_now();
    if (clock_frequency == 0) {
        return 0;
    }
    /* Meme precaution de debordement que pour le VR4300, et pour la meme
       raison : le produit naif plafonnerait a quelques heures. */
    return (ticks / clock_frequency) * 1000000ULL
         + ((ticks % clock_frequency) * 1000000ULL) / clock_frequency;
}

unsigned long long dkr_clock_vr4300_count(void)
{
    return dkr_clock_ticks_to_vr4300(dkr_clock_now(), clock_frequency);
}
