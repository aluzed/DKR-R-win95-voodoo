/* E02-S05 — ce que Windows 95 permet reellement pour ecrire une sauvegarde.
 *
 * Le ticket avance deux affirmations qu'il vaut mieux verifier que supposer,
 * parce que toute la conception de l'ecriture atomique en depend :
 *
 *   « `MoveFileEx` avec remplacement n'y est pas disponible, la sequence doit
 *     donc etre ecrite a la main »
 *   « le systeme de fichiers peut etre FAT16 en 8.3 »
 *
 * `MoveFileExA` **est** exportee par le KERNEL32 de la machine, et n'est pas un
 * bouchon. Reste a savoir si elle accepte `MOVEFILE_REPLACE_EXISTING`, ce qui
 * n'est pas la meme question — Windows 9x accepte des fonctions dont il ignore
 * certains drapeaux.
 *
 * Le releve atterrit dans D:\FILEIO.TXT. Le disque D: est en FAT16, donc c'est
 * bien le systeme de fichiers qui nous interesse.
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

/* Ecrit un fichier avec un contenu connu. Rend 1 en cas de succes. */
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

/* Relit un fichier et compare. Rend 1 si le contenu est celui attendu. */
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
    say("Ecriture de fichiers sous Windows 95 — mesure, non supposition\n\n");

    /* --- 1. MoveFileExA accepte-t-elle le remplacement ? ------------------- *
     *
     * C'est la question qui decide de la forme de l'ecriture atomique. Si elle
     * l'accepte, le remplacement est une seule operation ; sinon il faut
     * effacer puis renommer, et la fenetre entre les deux est celle ou une
     * coupure de courant detruit la sauvegarde.
     */
    say("1. Remplacement d'un fichier existant\n");

    write_file("D:\\FIOOLD.TMP", "ancien");
    write_file("D:\\FIONEW.TMP", "nouveau");

    if (MoveFileExA("D:\\FIONEW.TMP", "D:\\FIOOLD.TMP", MOVEFILE_REPLACE_EXISTING)) {
        say("   MoveFileExA(REPLACE_EXISTING) : REUSSIT\n");
        say("   contenu apres remplacement    : %s\n",
            read_matches("D:\\FIOOLD.TMP", "nouveau") ? "nouveau (correct)"
                                                      : "INCORRECT");
        say("   la source a bien disparu      : %s\n",
            file_exists("D:\\FIONEW.TMP") ? "NON" : "oui");
    } else {
        say("   MoveFileExA(REPLACE_EXISTING) : ECHOUE, erreur %lu\n",
            (unsigned long)GetLastError());
        say("   -> la sequence manuelle est necessaire\n");
    }

    /* Et sans le drapeau, pour la comparaison : `MoveFileA` doit refuser
       d'ecraser, ce qui est le comportement documente. */
    write_file("D:\\FIOOLD.TMP", "ancien");
    write_file("D:\\FIONEW.TMP", "nouveau");
    say("   MoveFileA sur une cible existante : %s\n",
        MoveFileA("D:\\FIONEW.TMP", "D:\\FIOOLD.TMP") ? "reussit (inattendu)"
                                                      : "refuse (attendu)");
    DeleteFileA("D:\\FIONEW.TMP");
    DeleteFileA("D:\\FIOOLD.TMP");

    /* --- 2. Les noms longs sur FAT16 --------------------------------------- *
     *
     * Le ticket demande que les noms tiennent en 8.3 « ou que le comportement
     * sur FAT16 soit verifie plutot que suppose ». Verifions-le : ce Windows 95
     * a les noms longs (VFAT), et la question est de savoir si un nom long
     * survit a l'aller-retour ecriture / relecture / enumeration.
     */
    say("\n2. Noms de fichiers longs sur le volume FAT16\n");

    {
        const char *longname = "D:\\SauvegardeDeCourse-Joueur1.dkrsave";
        if (write_file(longname, "contenu")) {
            say("   creation d'un nom long        : reussit\n");
            say("   relecture par le meme nom     : %s\n",
                read_matches(longname, "contenu") ? "reussit" : "ECHOUE");

            /* Le nom court equivalent, tel que le systeme le fabrique. */
            {
                char shortname[MAX_PATH];
                DWORD n = GetShortPathNameA(longname, shortname, sizeof(shortname));
                if (n > 0 && n < sizeof(shortname)) {
                    say("   nom court equivalent          : %s\n", shortname);
                    say("   relecture par le nom court    : %s\n",
                        read_matches(shortname, "contenu") ? "reussit" : "ECHOUE");
                } else {
                    say("   GetShortPathNameA             : echoue (%lu)\n",
                        (unsigned long)GetLastError());
                }
            }
            DeleteFileA(longname);
        } else {
            say("   creation d'un nom long        : ECHOUE, erreur %lu\n",
                (unsigned long)GetLastError());
            say("   -> les noms doivent tenir en 8.3\n");
        }
    }

    /* --- 3. Ou se trouve l'executable -------------------------------------- *
     *
     * Il n'y a pas de %APPDATA% sous Windows 95, et un programme lance depuis le
     * menu Demarrer herite d'un repertoire courant qui n'a rien a voir avec
     * l'endroit ou il est installe. La sauvegarde va donc a cote de
     * l'executable, et il faut savoir le trouver.
     */
    say("\n3. Emplacement de l'executable\n");
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
        if (n > 0 && n < sizeof(path)) {
            say("   GetModuleFileNameA            : %s\n", path);
        } else {
            say("   GetModuleFileNameA            : echoue (%lu)\n",
                (unsigned long)GetLastError());
        }
        n = GetCurrentDirectoryA(sizeof(path), path);
        if (n > 0 && n < sizeof(path)) {
            say("   repertoire courant            : %s\n", path);
        }
    }

    /* --- 4. Ecriture sur un support en lecture seule ------------------------ *
     *
     * E: est le disque d'installation ; on tente d'y ecrire pour voir la forme
     * de l'echec. Ce qui compte n'est pas qu'il echoue — c'est qu'il echoue avec
     * un code exploitable plutot qu'en plantant.
     */
    say("\n4. Ecriture refusee\n");
    {
        HANDLE h = CreateFileA("A:\\FIOTEST.TMP", GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            say("   ecriture sur A: (vide)        : refusee, erreur %lu\n",
                (unsigned long)GetLastError());
        } else {
            say("   ecriture sur A:               : acceptee (inattendu)\n");
            CloseHandle(h);
            DeleteFileA("A:\\FIOTEST.TMP");
        }
    }

    /* --- 5. Place disponible ----------------------------------------------- */
    say("\n5. Place disponible sur D:\n");
    {
        DWORD spc, bps, free_clusters, total_clusters;
        if (GetDiskFreeSpaceA("D:\\", &spc, &bps, &free_clusters, &total_clusters)) {
            say("   %lu octets par secteur, %lu secteurs par unite\n",
                (unsigned long)bps, (unsigned long)spc);
            say("   %lu unites libres sur %lu\n",
                (unsigned long)free_clusters, (unsigned long)total_clusters);
        } else {
            say("   GetDiskFreeSpaceA             : echoue (%lu)\n",
                (unsigned long)GetLastError());
        }
    }

    say("\nreleve termine\n");
    if (out) { fclose(out); }
    return 0;
}
