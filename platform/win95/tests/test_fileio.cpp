/* E02-S05 - the durable-write test.
 *
 * What is tested here is not "the file gets written" - a call to `fopen` does
 * that - but **what is left on the disk when the write is interrupted**. That is
 * the only property that counts: on a 1998 machine with no UPS, a power cut
 * during a write will happen.
 *
 * The interruptions are therefore simulated by building by hand the intermediate
 * states the sequence passes through, then checking what read-back makes of
 * them. Waiting for a real power cut is not a protocol, any more than waiting
 * 49.7 days for E01-S03's wraparound.
 *
 * One source for both targets, like the other suites.
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
        emit("  FAIL  %-48s expected \"%s\", got \"%s\"\n", what, want, got);
        failures++;
    }
}

static void expect_int(const char *what, long got, long want)
{
    checks++;
    if (got == want) {
        emit("  ok    %-48s %ld\n", what, got);
    } else {
        emit("  FAIL  %-48s expected %ld, got %ld\n", what, want, got);
        failures++;
    }
}

static void expect_true(const char *what, int cond)
{
    checks++;
    emit("  %s %s\n", cond ? "ok   " : "FAIL ", what);
    if (!cond) { failures++; }
}

/* --- Testing ground -------------------------------------------------------- *
 *
 * On the target, D: is the transfer disk, in FAT16 - that is, the file system we
 * care about. On the host, the current directory.
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

/* Reads back through the layer, and returns the content in `out`. */
static dkr_file_result reread(char *out, size_t out_size, int *from_backup)
{
    size_t got = 0;
    dkr_file_result r = dkr_file_read_durable(BASE, out, out_size - 1,
                                              &got, from_backup);
    out[r == DKR_FILE_OK ? got : 0] = '\0';
    return r;
}

/* ========================================================================== *
 * 1. The nominal case
 * ========================================================================== */
static void test_nominal(void)
{
    char buf[128];
    int  from_backup = 0;

    emit("Write and read back\n");
    clean();

    expect_int("first write",
               dkr_file_write_durable(BASE, "race-001", 8), DKR_FILE_OK);
    expect_int("read back", reread(buf, sizeof(buf), &from_backup), DKR_FILE_OK);
    expect_str("content", buf, "race-001");
    expect_int("it does not come from the backup copy", from_backup, 0);

    /* The second write must produce the backup copy. */
    expect_int("second write",
               dkr_file_write_durable(BASE, "race-002", 8), DKR_FILE_OK);
    expect_int("read back", reread(buf, sizeof(buf), &from_backup), DKR_FILE_OK);
    expect_str("content up to date", buf, "race-002");
    expect_true("the backup copy exists", exists(BAKF));
    expect_true("the temporary file is gone", !exists(TMPF));

    /* And it does hold the previous version. */
    {
        char bak[128]; size_t n = 0;
        FILE *f = fopen(BAKF, "rb");
        if (f) { n = fread(bak, 1, sizeof(bak) - 1, f); fclose(f); }
        bak[n] = '\0';
        expect_str("the backup copy carries the previous version",
                   bak, "race-001");
    }
}

/* ========================================================================== *
 * 2. A cut between renaming the old one and renaming the new one
 * ========================================================================== *
 *
 * This is **the** window of the sequence, the one Windows 95 imposes for want of
 * atomic replacement. We build the exact state it leaves: no final file, the
 * previous one in `.BAK`, the new complete one in `.TMP`.
 *
 * What is established: read-back returns the **previous** one, not the new one.
 * That is a choice, and a deliberate one - nothing proves the `.TMP` is
 * complete, and DKR-R's format carries no checksum. Losing the last race is
 * annoying; loading a truncated save would be discovered much later and much
 * worse.
 */
static void test_interrupted_between_renames(void)
{
    char buf[128];
    int  from_backup = -1;

    emit("Cut between the two renames\n");
    clean();

    put(BAKF, "race-001");          /* the previous one, known good */
    put(TMPF, "race-002");          /* the new one, complete but unproven */
    /* and no BASE: that is the whole problem */

    expect_int("read-back succeeds", reread(buf, sizeof(buf), &from_backup),
               DKR_FILE_OK);
    expect_str("it returns the previous one, not the new one", buf, "race-001");
    expect_int("and says so to the caller", from_backup, 1);
}

/* ========================================================================== *
 * 3. A cut while writing the temporary file
 * ========================================================================== *
 *
 * State: the final file is intact, a truncated `.TMP` is lying around. Read-back
 * must not notice it - the `.TMP` has no claim on the save.
 */
static void test_interrupted_during_temp(void)
{
    char buf[128];
    int  from_backup = -1;

    emit("Cut while writing the temporary file\n");
    clean();

    put(BASE, "race-001");
    put(TMPF, "race");              /* truncated */

    expect_int("read-back succeeds", reread(buf, sizeof(buf), &from_backup),
               DKR_FILE_OK);
    expect_str("the final file wins", buf, "race-001");
    expect_int("the backup copy was not used", from_backup, 0);

    /* And a following write must put things right again. */
    expect_int("the following write succeeds",
               dkr_file_write_durable(BASE, "race-002", 8), DKR_FILE_OK);
    expect_int("read back", reread(buf, sizeof(buf), &from_backup), DKR_FILE_OK);
    expect_str("content up to date", buf, "race-002");
}

/* ========================================================================== *
 * 4. Nothing at all, and errors told apart
 * ========================================================================== */
static void test_absent_and_errors(void)
{
    char buf[128];

    emit("Absence and errors\n");
    clean();

    expect_int("an absent file reports itself absent",
               reread(buf, sizeof(buf), NULL), DKR_FILE_ERR_NOT_FOUND);

    /* The codes must be distinct: "disk full" and "write-protected medium" do
       not call for the same action from the player. */
    expect_true("the error codes are distinct",
                DKR_FILE_ERR_NO_SPACE != DKR_FILE_ERR_ACCESS &&
                DKR_FILE_ERR_ACCESS   != DKR_FILE_ERR_NOT_READY);
    expect_true("each carries a text",
                strlen(dkr_file_result_text(DKR_FILE_ERR_NO_SPACE)) > 0 &&
                strlen(dkr_file_result_text(DKR_FILE_ERR_NOT_READY)) > 0);
}

/* ========================================================================== *
 * 5. 8.3 names - the function answers, it does not correct
 * ========================================================================== */
static void test_8dot3(void)
{
    emit("Recognising 8.3 names\n");

    expect_true("SAVE.DAT",         dkr_file_name_is_8dot3("SAVE.DAT"));
    expect_true("EIGHTCHR.DAT",     dkr_file_name_is_8dot3("EIGHTCHR.DAT"));
    expect_true("no extension",     dkr_file_name_is_8dot3("SAVE"));
    expect_true("short extension",  dkr_file_name_is_8dot3("SAVE.D"));

    expect_true("nine characters refused",
                !dkr_file_name_is_8dot3("NINECHARS.DAT"));
    expect_true("four-letter extension refused",
                !dkr_file_name_is_8dot3("SAVE.DKRS"));
    expect_true("two dots refused",
                !dkr_file_name_is_8dot3("SAVE.DAT.BAK"));
    expect_true("space refused",
                !dkr_file_name_is_8dot3("MY SAVE.DAT"));
    expect_true("forbidden character refused",
                !dkr_file_name_is_8dot3("SAVE?.DAT"));
    expect_true("empty name refused", !dkr_file_name_is_8dot3(""));

    /* The names the layer builds must themselves fit: that is what keeps it
       usable on a volume without long names. */
    expect_true("FIOT.DAT, FIOT.TMP and FIOT.BAK fit",
                dkr_file_name_is_8dot3("FIOT.DAT") &&
                dkr_file_name_is_8dot3("FIOT.TMP") &&
                dkr_file_name_is_8dot3("FIOT.BAK"));
}

/* ========================================================================== *
 * 6. Location and path joining
 * ========================================================================== */
static void test_paths(void)
{
    char dir[300], joined[300];

    emit("Location and paths\n");

    expect_int("the application's directory is found",
               dkr_file_app_directory(dir, sizeof(dir)), DKR_FILE_OK);
    expect_true("it is not empty", strlen(dir) > 0);
    emit("  directory: %s\n", dir);

    expect_int("join", dkr_file_join(joined, sizeof(joined), dir, "SAVE.DAT"),
               DKR_FILE_OK);
    emit("  path     : %s\n", joined);
    expect_true("the joined name ends properly",
                strstr(joined, "SAVE.DAT") != NULL);

    /* One separator too many in the directory must not produce two. */
#if defined(_WIN32)
    expect_int("directory with a trailing separator",
               dkr_file_join(joined, sizeof(joined), "D:\\GAME\\", "S.DAT"), DKR_FILE_OK);
    expect_str("a single separator", joined, "D:\\GAME\\S.DAT");
#else
    expect_int("directory with a trailing separator",
               dkr_file_join(joined, sizeof(joined), "/game/", "S.DAT"), DKR_FILE_OK);
    expect_str("a single separator", joined, "/game/S.DAT");
#endif

    /* A buffer that is too short refuses rather than truncating in silence. */
    {
        char small[8];
        expect_int("buffer too short refused",
                   dkr_file_join(small, sizeof(small), "D:\\A\\LONG\\DIRECTORY",
                                 "SAVE.DAT"), DKR_FILE_ERR_PATH);
    }
}

/* ========================================================================== */

int main(void)
{
    int rc;

#if defined(_WIN32)
    if (dkr_win95_startup("File I/O test") != DKR_WIN95_STARTUP_OK) {
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

    emit("\n%d checks, %d failure(s)\n", checks, failures);
    rc = failures != 0;
    if (report_file) { fclose(report_file); report_file = NULL; }
    return rc;
}
