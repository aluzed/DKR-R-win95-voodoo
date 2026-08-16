/* E02-S05 - what Windows 95 really allows for writing a save file.
 *
 * The ticket makes two claims better checked than assumed, because the whole
 * design of the atomic write depends on them:
 *
 *   "`MoveFileEx` with replacement is not available there, so the sequence must
 *    be written by hand"
 *   "the file system may be FAT16 in 8.3"
 *
 * `MoveFileExA` **is** exported by the machine's KERNEL32, and is not a stub. What
 * remains is whether it accepts `MOVEFILE_REPLACE_EXISTING`, which is not the
 * same question - Windows 9x accepts functions whose flags it ignores.
 *
 * The report lands in D:\FILEIO.TXT. Drive D: is FAT16, so it really is the file
 * system we care about.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

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

/* Writes a file with a known content. Returns 1 on success. */
static int write_file(const char *path, const char *content)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
    return written == (DWORD)strlen(content);
}

/* Reads a file back and compares. Returns 1 if the content is the expected one. */
static int read_matches(const char *path, const char *expected)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    char  buf[256];
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    buf[got] = '\0';
    return strcmp(buf, expected) == 0;
}

static int file_exists(const char *path)
{
    return GetFileAttributesA(path) != 0xFFFFFFFFu;
}

int main(void)
{
    out = fopen("D:\\FILEIO.TXT", "w");
    say("Writing files under Windows 95 - measured, not assumed\n\n");

    /* --- 1. Does MoveFileExA accept replacement? --------------------------- *
     *
     * This is the question that decides the shape of the atomic write. If it
     * accepts it, the replacement is a single operation; otherwise one must delete
     * then rename, and the window between the two is where a power cut destroys
     * the save.
     */
    say("1. Replacing an existing file\n");

    write_file("D:\\FIOOLD.TMP", "old");
    write_file("D:\\FIONEW.TMP", "new");

    if (MoveFileExA("D:\\FIONEW.TMP", "D:\\FIOOLD.TMP", MOVEFILE_REPLACE_EXISTING)) {
        say("   MoveFileExA(REPLACE_EXISTING) : SUCCEEDS\n");
        say("   content after replacement     : %s\n",
            read_matches("D:\\FIOOLD.TMP", "new") ? "new (correct)"
                                                   : "INCORRECT");
        say("   the source is indeed gone     : %s\n",
            file_exists("D:\\FIONEW.TMP") ? "NO" : "yes");
    } else {
        say("   MoveFileExA(REPLACE_EXISTING) : FAILS, error %lu\n",
            (unsigned long)GetLastError());
        say("   -> the manual sequence is necessary\n");
    }

    /* And without the flag, for comparison: `MoveFileA` must refuse to overwrite,
       which is the documented behaviour. */
    write_file("D:\\FIOOLD.TMP", "old");
    write_file("D:\\FIONEW.TMP", "new");
    say("   MoveFileA onto an existing target : %s\n",
        MoveFileA("D:\\FIONEW.TMP", "D:\\FIOOLD.TMP") ? "succeeds (unexpected)"
                                                        : "refuses (expected)");
    DeleteFileA("D:\\FIONEW.TMP");
    DeleteFileA("D:\\FIOOLD.TMP");

    /* --- 2. Long names on FAT16 -------------------------------------------- *
     *
     * The ticket asks that the names fit in 8.3 "or that the behaviour on FAT16 be
     * checked rather than assumed". Let us check it: this Windows 95 has long
     * names (VFAT), and the question is whether a long name survives the write /
     * read-back / enumeration round trip.
     */
    say("\n2. Long file names on the FAT16 volume\n");

    {
        const char *longname = "D:\\RaceSaveFile-Player1.dkrsave";
        if (write_file(longname, "content")) {
            say("   creating a long name          : succeeds\n");
            say("   reading back by the same name : %s\n",
                read_matches(longname, "content") ? "succeeds" : "FAILS");

            /* The equivalent short name, as the system manufactures it. */
            {
                char shortname[MAX_PATH];
                DWORD n = GetShortPathNameA(longname, shortname, sizeof(shortname));
                if (n > 0 && n < sizeof(shortname)) {
                    say("   equivalent short name         : %s\n", shortname);
                    say("   reading back by the short name: %s\n",
                        read_matches(shortname, "content") ? "succeeds" : "FAILS");
                } else {
                    say("   GetShortPathNameA             : fails (%lu)\n",
                        (unsigned long)GetLastError());
                }
            }
            DeleteFileA(longname);
        } else {
            say("   creating a long name          : FAILS, error %lu\n",
                (unsigned long)GetLastError());
            say("   -> the names must fit in 8.3\n");
        }
    }

    /* --- 3. Where the executable is ---------------------------------------- *
     *
     * There is no %APPDATA% under Windows 95, and a program launched from the
     * Start menu inherits a current directory that has nothing to do with where it
     * is installed. The save therefore goes next to the executable, and one must
     * know how to find it.
     */
    say("\n3. Location of the executable\n");
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
        if (n > 0 && n < sizeof(path)) {
            say("   GetModuleFileNameA            : %s\n", path);
        } else {
            say("   GetModuleFileNameA            : fails (%lu)\n",
                (unsigned long)GetLastError());
        }
        n = GetCurrentDirectoryA(sizeof(path), path);
        if (n > 0 && n < sizeof(path)) {
            say("   current directory             : %s\n", path);
        }
    }

    /* --- 4. Writing to a read-only medium ---------------------------------- *
     *
     * E: is the installation disc; we try to write to it to see the shape of the
     * failure. What counts is not that it fails - it is that it fails with a
     * usable code rather than by crashing.
     */
    say("\n4. Refused write\n");
    {
        HANDLE h = CreateFileA("A:\\FIOTEST.TMP", GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            say("   write to A: (empty)           : refused, error %lu\n",
                (unsigned long)GetLastError());
        } else {
            say("   write to A:                   : accepted (unexpected)\n");
            CloseHandle(h);
            DeleteFileA("A:\\FIOTEST.TMP");
        }
    }

    /* --- 5. Free space ------------------------------------------------------ */
    say("\n5. Free space on D:\n");
    {
        DWORD spc, bps, free_clusters, total_clusters;
        if (GetDiskFreeSpaceA("D:\\", &spc, &bps, &free_clusters, &total_clusters)) {
            say("   %lu bytes per sector, %lu sectors per cluster\n",
                (unsigned long)bps, (unsigned long)spc);
            say("   %lu free clusters out of %lu\n",
                (unsigned long)free_clusters, (unsigned long)total_clusters);
        } else {
            say("   GetDiskFreeSpaceA             : fails (%lu)\n",
                (unsigned long)GetLastError());
        }
    }

    say("\nreport finished\n");
    if (out) { fclose(out); }
    return 0;
}
