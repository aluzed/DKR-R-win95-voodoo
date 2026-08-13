/* E02-S03 — epreuve de la base de temps.
 *
 * Comme les autres suites de `platform/win95`, une seule source pour les deux
 * cibles : sur l'hote elle s'appuie sur le vehicule POSIX, sur la machine elle
 * devient CLOCKT.EXE.
 *
 *   platform/win95/tests/run-tests.sh clock
 *   d:\clockt.exe                        sur la cible
 *   d:\clockt.exe --long 300             derive mesuree sur cinq minutes
 *
 * Le gros du fichier porte sur la conversion vers le compteur du VR4300, parce
 * que c'est la que se cache le defaut redoute par le ticket : une base qui
 * derive lentement ne casse rien de visible et fausse tous les chronometrages.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../clock.h"
#include "../compat.h"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#else
#include <time.h>
#endif

static int failures = 0;
static int checks   = 0;
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
    if (report_file) { fputs(line, report_file); fflush(report_file); }
}

static void expect(const char *what, unsigned long long got, unsigned long long want)
{
    checks++;
    if (got == want) {
        emit("  ok    %-50s %llu\n", what, got);
    } else {
        emit("  ECHEC %-50s attendu %llu, obtenu %llu\n", what, want, got);
        failures++;
    }
}

static void expect_true(const char *what, int cond)
{
    checks++;
    emit("  %s %s\n", cond ? "ok   " : "ECHEC", what);
    if (!cond) { failures++; }
}

/* ========================================================================== *
 * 1. La conversion vers le compteur du VR4300 — fonction pure
 * ========================================================================== *
 *
 * Ce qui est etabli : le rapport est exact, et il le reste sur des durees ou
 * l'ecriture naive aurait deborde depuis longtemps.
 *
 * Le compteur avance a 46 875 000 Hz quelle que soit la frequence de l'hote.
 * C'est ce rapport, et non la valeur instantanee, qui doit etre juste.
 */
static void test_vr4300_conversion(void)
{
    const unsigned long long pit = 1193180ULL;   /* la frequence mesuree */

    emit("Conversion vers le compteur du VR4300 (fonction pure)\n");

    expect("zero reste zero", dkr_clock_ticks_to_vr4300(0, pit), 0);

    /* Une seconde de PIT doit donner une seconde de VR4300. */
    expect("1 s de PIT -> 46 875 000",
           dkr_clock_ticks_to_vr4300(pit, pit), 46875000ULL);

    /* Dix minutes — la duree de l'epreuve d'endurance de E02-S01. */
    expect("600 s -> 28 125 000 000",
           dkr_clock_ticks_to_vr4300(pit * 600ULL, pit), 46875000ULL * 600ULL);

    /* Une frequence hote differente ne doit rien changer au resultat. */
    expect("frequence hote de 1 GHz, 1 s",
           dkr_clock_ticks_to_vr4300(1000000000ULL, 1000000000ULL), 46875000ULL);
    expect("frequence hote de 3,579545 MHz, 1 s",
           dkr_clock_ticks_to_vr4300(3579545ULL, 3579545ULL), 46875000ULL);

    /* **Le cas que l'ecriture naive rate.** `ticks * 46875000` deborde au-dela
       d'environ 46 heures ; on demande ici 100 heures. */
    {
        unsigned long long hours100 = pit * 3600ULL * 100ULL;
        expect("100 heures, sans debordement",
               dkr_clock_ticks_to_vr4300(hours100, pit), 46875000ULL * 3600ULL * 100ULL);
    }

    /* Et un an, pour que la marge soit dite plutot que supposee. */
    {
        unsigned long long year = pit * 3600ULL * 24ULL * 365ULL;
        expect("un an, sans debordement",
               dkr_clock_ticks_to_vr4300(year, pit),
               46875000ULL * 3600ULL * 24ULL * 365ULL);
    }

    /* Une frequence nulle ne doit pas diviser par zero. */
    expect("frequence nulle rendue sans exploser",
           dkr_clock_ticks_to_vr4300(1234, 0), 0);

    /* Monotonie de la conversion : un pas de plus ne peut pas donner moins. */
    {
        int ok = 1;
        unsigned long long previous = 0;
        for (unsigned long long t = 0; t < 5000; t++) {
            unsigned long long v = dkr_clock_ticks_to_vr4300(t, pit);
            if (v < previous) { ok = 0; break; }
            previous = v;
        }
        expect_true("la conversion est monotone", ok);
    }
}

/* ========================================================================== *
 * 2. Le rebouclage du repli 32 bits — simule, pas attendu
 * ========================================================================== *
 *
 * Ce qui est etabli : le repli `timeGetTime` survit a son passage a zero, qui
 * survient apres 49,7 jours. C'est le genre de defaut qu'on ne rencontre jamais
 * en developpement et toujours chez un joueur — d'ou la simulation.
 *
 * La logique est celle de `dkr_tick64_step`, deja couverte par E01-S03 ; ce qui
 * est verifie ici, c'est qu'elle tient bien sur les valeurs d'une horloge a la
 * milliseconde et que la duree ecoulee reste juste **a travers** le passage.
 */
static void test_wraparound(void)
{
    dkr_tick64_state st = { 0, 0 };
    const unsigned long before = 0xFFFFFF00UL;   /* 256 ms avant le passage */

    emit("Rebouclage du repli 32 bits (simule)\n");

    expect("depart", dkr_tick64_step(&st, before), (unsigned long long)before);

    /* Juste avant. */
    expect("255 ms plus tard, avant le passage",
           dkr_tick64_step(&st, 0xFFFFFFFFUL), 0xFFFFFFFFULL);

    /* Et le passage lui-meme : la valeur 32 bits recule, la 64 bits avance. */
    expect("1 ms plus tard, apres le passage",
           dkr_tick64_step(&st, 0x00000000UL), 0x100000000ULL);
    expect("puis 1000 ms",
           dkr_tick64_step(&st, 1000UL), 0x100000000ULL + 1000ULL);

    /* La duree ecoulee de part et d'autre du passage doit etre juste : c'est
       la propriete qui compte pour un chronometre de course. */
    {
        unsigned long long start = (unsigned long long)before;
        unsigned long long end   = 0x100000000ULL + 1000ULL;
        expect("duree juste a travers le passage", end - start, 256ULL + 1000ULL);
    }

    /* Un second passage, pour verifier que l'accumulation ne se contente pas
       d'un seul. */
    {
        dkr_tick64_state s2 = { 0, 0 };
        dkr_tick64_step(&s2, 0xFFFFFFFFUL);
        dkr_tick64_step(&s2, 0x00000000UL);
        dkr_tick64_step(&s2, 0xFFFFFFFFUL);
        expect("deux passages", dkr_tick64_step(&s2, 0x00000000UL), 0x200000000ULL);
    }
}

/* ========================================================================== *
 * 3. La source retenue au lancement
 * ========================================================================== */
static void test_source_selection(void)
{
    emit("Selection de la source\n");

    expect_true("l'horloge demarre", dkr_clock_init() != 0);
    expect_true("une source est retenue",
                dkr_clock_source_in_use() != DKR_CLOCK_SOURCE_NONE);
    expect_true("sa frequence est non nulle", dkr_clock_frequency() > 0);
    emit("  source : %s a %llu Hz\n",
         dkr_clock_source_name(), dkr_clock_frequency());

    /* Deux appels successifs ne doivent jamais reculer. */
    {
        int ok = 1;
        unsigned long long previous = dkr_clock_now();
        for (int i = 0; i < 200000; i++) {
            unsigned long long now = dkr_clock_now();
            if (now < previous) { ok = 0; break; }
            previous = now;
        }
        expect_true("200 000 lectures sans un seul recul", ok);
    }
}

/* ========================================================================== *
 * 4. Le rapport tient sur une duree, pas sur un instant
 * ========================================================================== *
 *
 * Ce qui est etabli : le compteur du VR4300 avance bien a 46,875 MHz par
 * rapport au temps mur. Le ticket insiste, a raison, pour que ce controle porte
 * sur une duree : une derive lente est invisible sur un instantane.
 *
 * La tolerance est large a dessein. La machine est emulee, et l'ordonnanceur de
 * Windows 95 n'est pas temps reel ; ce que cette epreuve attrape, c'est une
 * erreur de rapport — un facteur deux, une division inversee — et non une
 * imprecision de quelques pour cent.
 */
static void test_ratio_over_time(unsigned long seconds)
{
    unsigned long long c0, c1, us0, us1;
    double expected, measured, error_pct;

    emit("Rapport du compteur VR4300 sur %lu s\n", seconds);

    us0 = dkr_clock_now_us();
    c0  = dkr_clock_vr4300_count();
#if defined(_WIN32)
    Sleep(seconds * 1000);
#else
    {
        struct timespec ts;
        ts.tv_sec  = (time_t)seconds;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
    }
#endif
    c1  = dkr_clock_vr4300_count();
    us1 = dkr_clock_now_us();

    expected  = (double)(us1 - us0) * 46.875;      /* us * 46,875 cycles/us */
    measured  = (double)(c1 - c0);
    error_pct = expected > 0 ? (measured - expected) * 100.0 / expected : 100.0;

    emit("  ecoule : %llu us, compteur : %llu cycles\n", us1 - us0, c1 - c0);
    emit("  attendu : %.0f cycles, ecart : %+.4f %%\n", expected, error_pct);

    expect_true("le rapport tient a 1 % pres",
                error_pct > -1.0 && error_pct < 1.0);
}

/* ========================================================================== */

int main(int argc, char **argv)
{
    unsigned long long_seconds = 0;
    int rc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--long") == 0 && i + 1 < argc) {
            long_seconds = strtoul(argv[++i], 0, 10);
        }
    }

#if defined(_WIN32)
    if (dkr_win95_startup("Epreuve de l'horloge") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
    report_file = fopen(long_seconds > 0 ? "D:\\CLOCKLNG.LOG" : "D:\\CLOCKT.LOG", "w");
#endif

    test_vr4300_conversion();
    test_wraparound();
    test_source_selection();
    test_ratio_over_time(long_seconds > 0 ? long_seconds : 3);

    emit("\n%d controles, %d echec(s)\n", checks, failures);
    rc = failures != 0;

    dkr_clock_shutdown();
    if (report_file) { fclose(report_file); report_file = NULL; }
    return rc;
}
