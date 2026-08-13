/* E02-S03 — quelle base de temps Windows 95 offre reellement.
 *
 * Le ticket dresse un tableau des sources candidates avec, pour chacune, une
 * resolution supposee. Ce programme le remplace par des mesures prises sur la
 * machine : `QueryPerformanceFrequency` « varie selon le chipset », et la
 * resolution de `timeGetTime` depend de `timeBeginPeriod`, dont l'effet reel
 * doit se constater.
 *
 * Ce qui est mesure, pour chaque source :
 *
 *   frequence      ce que le systeme annonce
 *   resolution     le plus petit ecart non nul observe entre deux lectures
 *                  consecutives — c'est la vraie granularite, pas celle annoncee
 *   monotonie      un recul, meme d'un pas, disqualifie une source
 *   cout           duree moyenne d'un appel
 *
 * **Le cout mesure ici n'est pas transposable au materiel reel.** L'emulation
 * d'86Box est fonctionnelle et non temporelle ; le chiffre dit l'ordre de
 * grandeur et le classement des sources entre elles, pas le budget d'un
 * Pentium II de 1998. C'est E09-S04 qui tranchera sur machine reelle.
 *
 * Ecrit son releve dans D:\CLOCK.TXT, lisible depuis l'hote.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

static FILE *out;

static void say(const char *fmt, ...)
{
    char    line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    if (out) { fputs(line, out); fflush(out); }
}

/* --- Resolution : le plus petit pas non nul reellement observe ------------- *
 *
 * On ne demande pas au systeme sa resolution, on la constate. Une source qui
 * annonce la microseconde et n'avance que toutes les 55 ms est le piege que
 * cette mesure existe pour eviter.
 */
static unsigned long resolution_ms_gettickcount(void)
{
    DWORD start = GetTickCount();
    DWORD now;
    do { now = GetTickCount(); } while (now == start);
    return (unsigned long)(now - start);
}

static unsigned long resolution_ms_timegettime(void)
{
    DWORD start = timeGetTime();
    DWORD now;
    do { now = timeGetTime(); } while (now == start);
    return (unsigned long)(now - start);
}

/* Pour QPC on rend le pas en unites de compteur, et on le convertit ensuite. */
static LONGLONG resolution_ticks_qpc(void)
{
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    do { QueryPerformanceCounter(&b); } while (b.QuadPart == a.QuadPart);
    return b.QuadPart - a.QuadPart;
}

/* --- Monotonie ------------------------------------------------------------ *
 *
 * Un recul, meme d'un seul pas, disqualifie une source : toute la simulation
 * repose sur une base qui n'en fait jamais. On echantillonne serre, la ou un
 * defaut se verrait.
 */
#define MONOTONIC_SAMPLES 200000

static long monotonic_faults_qpc(void)
{
    LARGE_INTEGER previous, now;
    long faults = 0;
    int  i;
    QueryPerformanceCounter(&previous);
    for (i = 0; i < MONOTONIC_SAMPLES; i++) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart < previous.QuadPart) { faults++; }
        previous = now;
    }
    return faults;
}

static long monotonic_faults_timegettime(void)
{
    DWORD previous = timeGetTime(), now;
    long  faults = 0;
    int   i;
    for (i = 0; i < MONOTONIC_SAMPLES; i++) {
        now = timeGetTime();
        if ((long)(now - previous) < 0) { faults++; }
        previous = now;
    }
    return faults;
}

/* --- Cout d'un appel ------------------------------------------------------ *
 *
 * La base est consultee plusieurs fois par image. Sur un Pentium II, un appel
 * systeme coûteux repete devient un poste de budget a part entiere (E08-S01).
 * La mesure exterieure passe par `timeGetTime`, dont on connait desormais la
 * resolution.
 */
#define COST_CALLS 200000

static double cost_ns(void (*fn)(void), int calls)
{
    DWORD start, elapsed;
    int   i;
    /* Un tour a vide d'abord, pour ne pas mesurer le premier defaut de cache. */
    for (i = 0; i < 1000; i++) { fn(); }
    start = timeGetTime();
    for (i = 0; i < calls; i++) { fn(); }
    elapsed = timeGetTime() - start;
    return (elapsed * 1000000.0) / (double)calls;   /* ms -> ns par appel */
}

static void call_gettickcount(void) { volatile DWORD v = GetTickCount(); (void)v; }
static void call_timegettime(void)  { volatile DWORD v = timeGetTime();  (void)v; }
static void call_qpc(void)          { LARGE_INTEGER v; QueryPerformanceCounter(&v); }

int main(void)
{
    LARGE_INTEGER freq, dummy;
    TIMECAPS      caps;
    BOOL          has_qpc;
    unsigned long res_gtc, res_tgt_before, res_tgt_after;
    LONGLONG      res_qpc_ticks;

    out = fopen("D:\\CLOCK.TXT", "w");
    say("Sources de temps de Windows 95 — mesure, non supposition\n\n");

    /* --- Ce que le systeme annonce ---------------------------------------- */

    has_qpc = QueryPerformanceFrequency(&freq) && freq.QuadPart > 0
              && QueryPerformanceCounter(&dummy);
    if (has_qpc) {
        say("QueryPerformanceFrequency : %ld Hz\n", (long)freq.QuadPart);
    } else {
        say("QueryPerformanceFrequency : ABSENTE ou incoherente\n");
    }

    if (timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR) {
        say("timeGetDevCaps            : periode de %lu a %lu ms\n",
            (unsigned long)caps.wPeriodMin, (unsigned long)caps.wPeriodMax);
    } else {
        say("timeGetDevCaps            : echec\n");
    }

    /* --- Resolution reellement observee ------------------------------------ */

    say("\nResolution observee (plus petit pas non nul)\n");

    res_gtc = resolution_ms_gettickcount();
    say("  GetTickCount            : %lu ms\n", res_gtc);

    res_tgt_before = resolution_ms_timegettime();
    say("  timeGetTime  (avant)    : %lu ms\n", res_tgt_before);

    /* C'est ici que se joue le seul reglage du ticket : `timeBeginPeriod(1)`
       doit faire tomber la granularite a la milliseconde. S'il n'y parvient
       pas, tout le reste du choix change. */
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        res_tgt_after = resolution_ms_timegettime();
        say("  timeGetTime  (apres timeBeginPeriod(1)) : %lu ms\n", res_tgt_after);
    } else {
        res_tgt_after = res_tgt_before;
        say("  timeBeginPeriod(1)      : REFUSE\n");
    }

    if (has_qpc) {
        res_qpc_ticks = resolution_ticks_qpc();
        say("  QueryPerformanceCounter : %ld pas = %.3f us\n",
            (long)res_qpc_ticks,
            (double)res_qpc_ticks * 1000000.0 / (double)freq.QuadPart);
    }

    /* --- Monotonie --------------------------------------------------------- */

    say("\nMonotonie sur %d lectures consecutives\n", MONOTONIC_SAMPLES);
    if (has_qpc) {
        say("  QueryPerformanceCounter : %ld recul(s)\n", monotonic_faults_qpc());
    }
    say("  timeGetTime             : %ld recul(s)\n", monotonic_faults_timegettime());

    /* --- Cout -------------------------------------------------------------- */

    say("\nCout par appel — ordre de grandeur sous emulation, NON transposable\n");
    say("  GetTickCount            : %.0f ns\n", cost_ns(call_gettickcount, COST_CALLS));
    say("  timeGetTime             : %.0f ns\n", cost_ns(call_timegettime, COST_CALLS));
    if (has_qpc) {
        say("  QueryPerformanceCounter : %.0f ns\n", cost_ns(call_qpc, COST_CALLS));
    }

    /* Relache le reglage : sous Windows 9x, un `timeBeginPeriod` laisse en place
       degrade tout le systeme jusqu'au redemarrage. */
    timeEndPeriod(1);

    say("\nreleve termine\n");
    if (out) { fclose(out); }
    return 0;
}
