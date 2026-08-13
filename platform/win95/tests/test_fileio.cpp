/* E02-S05 — epreuve de l'ecriture durable.
 *
 * Ce qui est teste ici n'est pas « le fichier s'ecrit » — cela, un appel a
 * `fopen` le fait — mais **ce qui reste sur le disque quand l'ecriture est
 * interrompue**. C'est la seule propriete qui compte : sur une machine de 1998
 * sans onduleur, la coupure pendant une ecriture arrivera.
 *
 * Les interruptions sont donc simulees en fabriquant a la main les etats
 * intermediaires que la sequence traverse, puis en verifiant ce que la
 * relecture en tire. Attendre une vraie coupure de courant n'est pas un
 * protocole, pas plus qu'attendre 49,7 jours pour le rebouclage de E01-S03.
 *
 * Une seule source pour les deux cibles, comme les autres suites.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fileio.h"

#if defined(_WIN32)
#include <windows.h>
#include "../startup.h"
#define REMOVE(p) DeleteFileA(p)
#else
#include <unistd.h>
#define REMOVE(p) unlink(p)
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

static void expect_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) == 0) {
        emit("  ok    %-48s \"%s\"\n", what, got);
    } else {
        emit("  ECHEC %-48s attendu \"%s\", obtenu \"%s\"\n", what, want, got);
        failures++;
    }
}

static void expect_int(const char *what, long got, long want)
{
    checks++;
    if (got == want) {
        emit("  ok    %-48s %ld\n", what, got);
    } else {
        emit("  ECHEC %-48s attendu %ld, obtenu %ld\n", what, want, got);
        failures++;
    }
}

static void expect_true(const char *what, int cond)
{
    checks++;
    emit("  %s %s\n", cond ? "ok   " : "ECHEC", what);
    if (!cond) { failures++; }
}

/* --- Terrain d'essai ------------------------------------------------------- *
 *
 * Sur la cible, D: est le disque de transfert, en FAT16 — c'est-a-dire le
 * systeme de fichiers qui nous interesse. Sur l'hote, le repertoire courant.
 */
#if defined(_WIN32)
#define BASE "D:\\FIOT.DAT"
#define TMPF "D:\\FIOT.TMP"
#define BAKF "D:\\FIOT.BAK"
#else
#define BASE "fiot.dat"
#define TMPF "fiot.TMP"
#define BAKF "fiot.BAK"
#endif

static void clean(void)
{
    REMOVE(BASE); REMOVE(TMPF); REMOVE(BAKF);
}

static void put(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(content, f); fclose(f); }
}

static int exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Relit par la couche, et rend le contenu dans `out`. */
static dkr_file_result reread(char *out, size_t out_size, int *from_backup)
{
    size_t got = 0;
    dkr_file_result r = dkr_file_read_durable(BASE, out, out_size - 1,
                                              &got, from_backup);
    out[r == DKR_FILE_OK ? got : 0] = '\0';
    return r;
}

/* ========================================================================== *
 * 1. Le cas nominal
 * ========================================================================== */
static void test_nominal(void)
{
    char buf[128];
    int  from_backup = 0;

    emit("Ecriture et relecture\n");
    clean();

    expect_int("premiere ecriture",
               dkr_file_write_durable(BASE, "course-1", 8), DKR_FILE_OK);
    expect_int("relecture", reread(buf, sizeof(buf), &from_backup), DKR_FILE_OK);
    expect_str("contenu", buf, "course-1");
    expect_int("elle ne vient pas de la copie de secours", from_backup, 0);

    /* La seconde ecriture doit produire la copie de secours. */
    expect_int("seconde ecriture",
               dkr_file_write_durable(BASE, "course-2", 8), DKR_FILE_OK);
    expect_int("relecture", reread(buf, sizeof(buf), &from_backup), DKR_FILE_OK);
    expect_str("contenu a jour", buf, "course-2");
    expect_true("la copie de secours existe", exists(BAKF));
    expect_true("le fichier temporaire a disparu", !exists(TMPF));

    /* Et elle contient bien la version precedente. */
    {
        char bak[128]; size_t n = 0;
        FILE *f = fopen(BAKF, "rb");
        if (f) { n = fread(bak, 1, sizeof(bak) - 1, f); fclose(f); }
        bak[n] = '\0';
        expect_str("la copie de secours porte la version precedente",
                   bak, "course-1");
    }
}

/* ========================================================================== *
 * 2. Coupure entre le renommage de l'ancienne et celui de la nouvelle
 * ========================================================================== *
 *
 * C'est **la** fenetre de la sequence, celle que Windows 95 impose faute de
 * remplacement atomique. On fabrique l'etat exact qu'elle laisse : pas de
 * fichier final, la precedente en `.BAK`, la nouvelle complete en `.TMP`.
 *
 * Ce qui est etabli : la relecture rend la **precedente**, et non la nouvelle.
 * C'est un choix, et il est delibere — rien ne prouve que le `.TMP` soit
 * complet, et le format de DKR-R ne porte pas de somme de controle. Perdre la
 * derniere course est desagreable ; charger une sauvegarde tronquee se
 * decouvrirait bien plus tard et bien plus mal.
 */
static void test_interrupted_between_renames(void)
{
    char buf[128];
    int  from_backup = -1;

    emit("Coupure entre les deux renommages\n");
    clean();

    put(BAKF, "course-1");          /* la precedente, connue bonne */
    put(TMPF, "course-2");          /* la nouvelle, complete mais non prouvee */
    /* et pas de BASE : c'est tout le probleme */

    expect_int("la relecture reussit", reread(buf, sizeof(buf), &from_backup),
               DKR_FILE_OK);
    expect_str("elle rend la precedente, pas la nouvelle", buf, "course-1");
    expect_int("et le dit a l'appelant", from_backup, 1);
}

/* ========================================================================== *
 * 3. Coupure pendant l'ecriture du temporaire
 * ========================================================================== *
 *
 * Etat : le fichier final est intact, un `.TMP` tronque traine. La relecture ne
 * doit pas s'en apercevoir — le `.TMP` n'a aucun droit sur la sauvegarde.
 */
static void test_interrupted_during_temp(void)
{
    char buf[128];
    int  from_backup = -1;

    emit("Coupure pendant l'ecriture du temporaire\n");
    clean();

    put(BASE, "course-1");
    put(TMPF, "cour");              /* tronque */

    expect_int("la relecture reussit", reread(buf, sizeof(buf), &from_backup),
               DKR_FILE_OK);
    expect_str("le fichier final l'emporte", buf, "course-1");
    expect_int("la copie de secours n'a pas servi", from_backup, 0);

    /* Et une ecriture suivante doit se remettre d'aplomb. */
    expect_int("l'ecriture suivante reussit",
               dkr_file_write_durable(BASE, "course-2", 8), DKR_FILE_OK);
    expect_int("relecture", reread(buf, sizeof(buf), &from_backup), DKR_FILE_OK);
    expect_str("contenu a jour", buf, "course-2");
}

/* ========================================================================== *
 * 4. Rien du tout, et erreurs distinguees
 * ========================================================================== */
static void test_absent_and_errors(void)
{
    char buf[128];

    emit("Absence et erreurs\n");
    clean();

    expect_int("un fichier absent se dit absent",
               reread(buf, sizeof(buf), NULL), DKR_FILE_ERR_NOT_FOUND);

    /* Les codes doivent etre distincts : « disque plein » et « support
       protege » n'appellent pas le meme geste chez le joueur. */
    expect_true("les codes d'erreur sont distincts",
                DKR_FILE_ERR_NO_SPACE != DKR_FILE_ERR_ACCESS &&
                DKR_FILE_ERR_ACCESS   != DKR_FILE_ERR_NOT_READY);
    expect_true("chacun porte un texte",
                strlen(dkr_file_result_text(DKR_FILE_ERR_NO_SPACE)) > 0 &&
                strlen(dkr_file_result_text(DKR_FILE_ERR_NOT_READY)) > 0);
}

/* ========================================================================== *
 * 5. Noms 8.3 — la fonction repond, elle ne corrige pas
 * ========================================================================== */
static void test_8dot3(void)
{
    emit("Reconnaissance des noms 8.3\n");

    expect_true("SAUVE.DAT",      dkr_file_name_is_8dot3("SAUVE.DAT"));
    expect_true("HUITCARS.DAT",   dkr_file_name_is_8dot3("HUITCARS.DAT"));
    expect_true("sans extension",  dkr_file_name_is_8dot3("SAUVE"));
    expect_true("extension courte", dkr_file_name_is_8dot3("SAUVE.D"));

    expect_true("neuf caracteres refuses",
                !dkr_file_name_is_8dot3("NEUFCARSX.DAT"));
    expect_true("extension de quatre refusee",
                !dkr_file_name_is_8dot3("SAUVE.DKRS"));
    expect_true("deux points refuses",
                !dkr_file_name_is_8dot3("SAUVE.DAT.BAK"));
    expect_true("espace refuse",
                !dkr_file_name_is_8dot3("MA SAUVE.DAT"));
    expect_true("caractere interdit refuse",
                !dkr_file_name_is_8dot3("SAUVE?.DAT"));
    expect_true("nom vide refuse", !dkr_file_name_is_8dot3(""));

    /* Les noms que la couche fabrique doivent eux-memes tenir : c'est ce qui
       permet de rester utilisable sur un volume sans noms longs. */
    expect_true("FIOT.DAT, FIOT.TMP et FIOT.BAK tiennent",
                dkr_file_name_is_8dot3("FIOT.DAT") &&
                dkr_file_name_is_8dot3("FIOT.TMP") &&
                dkr_file_name_is_8dot3("FIOT.BAK"));
}

/* ========================================================================== *
 * 6. Emplacement et assemblage de chemins
 * ========================================================================== */
static void test_paths(void)
{
    char dir[300], joined[300];

    emit("Emplacement et chemins\n");

    expect_int("le repertoire de l'application se trouve",
               dkr_file_app_directory(dir, sizeof(dir)), DKR_FILE_OK);
    expect_true("il n'est pas vide", strlen(dir) > 0);
    emit("  repertoire : %s\n", dir);

    expect_int("assemblage", dkr_file_join(joined, sizeof(joined), dir, "SAUVE.DAT"),
               DKR_FILE_OK);
    emit("  chemin     : %s\n", joined);
    expect_true("le nom assemble se termine bien",
                strstr(joined, "SAUVE.DAT") != NULL);

    /* Un separateur en trop dans le repertoire ne doit pas en produire deux. */
#if defined(_WIN32)
    expect_int("repertoire avec separateur final",
               dkr_file_join(joined, sizeof(joined), "D:\\JEU\\", "S.DAT"), DKR_FILE_OK);
    expect_str("un seul separateur", joined, "D:\\JEU\\S.DAT");
#else
    expect_int("repertoire avec separateur final",
               dkr_file_join(joined, sizeof(joined), "/jeu/", "S.DAT"), DKR_FILE_OK);
    expect_str("un seul separateur", joined, "/jeu/S.DAT");
#endif

    /* Un tampon trop court se refuse plutot que de tronquer en silence. */
    {
        char small[8];
        expect_int("tampon trop court refuse",
                   dkr_file_join(small, sizeof(small), "D:\\UN\\REPERTOIRE\\LONG",
                                 "SAUVE.DAT"), DKR_FILE_ERR_PATH);
    }
}

/* ========================================================================== */

int main(void)
{
    int rc;

#if defined(_WIN32)
    if (dkr_win95_startup("Epreuve fichiers") != DKR_WIN95_STARTUP_OK) {
        return 2;
    }
    report_file = fopen("D:\\FILEIOT.LOG", "w");
#endif

    test_nominal();
    test_interrupted_between_renames();
    test_interrupted_during_temp();
    test_absent_and_errors();
    test_8dot3();
    test_paths();
    clean();

    emit("\n%d controles, %d echec(s)\n", checks, failures);
    rc = failures != 0;
    if (report_file) { fclose(report_file); report_file = NULL; }
    return rc;
}
