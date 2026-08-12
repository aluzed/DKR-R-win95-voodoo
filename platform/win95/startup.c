/* E01-S03 — demarrage de la cible Windows 95. Voir startup.h. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "startup.h"

/* Le journal vit a cote de l'executable et non dans le repertoire courant :
   lance depuis le menu Demarrer, un programme herite d'un repertoire courant
   qui n'a rien a voir avec l'endroit ou l'utilisateur ira chercher le fichier. */
#define DKR_LOG_NAME "DKR-BOOT.LOG"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static char   g_app[64] = "DKR-R";

static void write_raw(const char *s, DWORD n)
{
    DWORD written = 0;
    if (g_log != INVALID_HANDLE_VALUE) {
        WriteFile(g_log, s, n, &written, NULL);
        /* Vidange immediate. Sans elle, la derniere ligne — celle qui dirait
           ou le programme est mort — resterait dans le cache et disparaitrait
           avec le processus. C'est exactement la ligne qui compte. */
        FlushFileBuffers(g_log);
    }
}

void dkr_win95_log(const char *message)
{
    if (!message) { return; }
    write_raw(message, (DWORD)strlen(message));
    write_raw("\r\n", 2);
}

void dkr_win95_log_num(const char *message, long value)
{
    char buf[32];
    int n;
    if (message) { write_raw(message, (DWORD)strlen(message)); }
    n = sprintf(buf, " %ld\r\n", value);
    write_raw(buf, (DWORD)n);
}

/* --- filtre d'exceptions ---------------------------------------------------
 *
 * Windows 95 n'a pas les gestionnaires vectorises ; `SetUnhandledExceptionFilter`
 * est le seul point d'accroche, et il suffit : on ne cherche pas a rattraper
 * l'exception, seulement a en laisser une trace lisible avant de mourir.
 */
static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "acces memoire invalide";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "instruction illegale";
    case EXCEPTION_PRIV_INSTRUCTION:      return "instruction privilegiee";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "division entiere par zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "division flottante par zero";
    case EXCEPTION_FLT_INVALID_OPERATION: return "operation flottante invalide";
    case EXCEPTION_STACK_OVERFLOW:        return "debordement de pile";
    case EXCEPTION_IN_PAGE_ERROR:         return "erreur de pagination";
    default:                              return "exception inconnue";
    }
}

static LONG WINAPI on_unhandled(EXCEPTION_POINTERS *info)
{
    char buf[256];
    DWORD code = info->ExceptionRecord->ExceptionCode;

    dkr_win95_log("");
    dkr_win95_log("*** exception non rattrapee ***");
    sprintf(buf, "  code    : 0x%08lX (%s)", (unsigned long)code, exception_name(code));
    dkr_win95_log(buf);
    sprintf(buf, "  adresse : 0x%08lX",
            (unsigned long)(ULONG_PTR)info->ExceptionRecord->ExceptionAddress);
    dkr_win95_log(buf);

    /* `EXCEPTION_ILLEGAL_INSTRUCTION` merite un mot : sur cette cible, c'est le
       symptome d'une instruction posterieure au Pentium II qui aurait echappe au
       controle de E01-S01. L'ecrire ici epargne une heure de recherche. */
    if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
        dkr_win95_log("  piste   : instruction hors Pentium II ? "
                      "voir tools/win95/check-instruction-set.sh");
    }

    sprintf(buf, "%s s'est arrete sur une %s.\n\nDetails dans " DKR_LOG_NAME ".",
            g_app, exception_name(code));
    MessageBoxA(NULL, buf, g_app, MB_ICONERROR | MB_OK);

    return EXCEPTION_EXECUTE_HANDLER;
}

/* --- controle de version --------------------------------------------------- */

static int check_version(void)
{
    OSVERSIONINFOA v;
    char buf[256];

    v.dwOSVersionInfoSize = sizeof(v);
    if (!GetVersionExA(&v)) {
        /* Ne pas savoir n'est pas une raison de refuser : sur un systeme trop
           ancien pour repondre, on aurait deja echoue au chargement. */
        dkr_win95_log("version du systeme : indeterminee, on continue");
        return DKR_WIN95_STARTUP_OK;
    }

    sprintf(buf, "systeme : plate-forme %lu, version %lu.%lu build %lu",
            (unsigned long)v.dwPlatformId, (unsigned long)v.dwMajorVersion,
            (unsigned long)v.dwMinorVersion, (unsigned long)(v.dwBuildNumber & 0xFFFF));
    dkr_win95_log(buf);
    if (v.szCSDVersion[0]) { dkr_win95_log(v.szCSDVersion); }

    /* Win32s est une couche 32 bits posee sur Windows 3.1 : elle n'a ni fils
       d'execution ni la moitie de KERNEL32. Le refus doit etre explicite. */
    if (v.dwPlatformId == VER_PLATFORM_WIN32s) {
        MessageBoxA(NULL,
                    "Win32s sur Windows 3.1 n'est pas supporte.\n\n"
                    "Ce programme demande Windows 95 ou plus recent.",
                    g_app, MB_ICONERROR | MB_OK);
        dkr_win95_log("REFUS : Win32s");
        return DKR_WIN95_STARTUP_TOO_OLD;
    }

    /* Windows 95 est 4.0. Toute version 4.0 et au-dela convient, NT compris —
       le binaire y tourne aussi, ce qui rend le developpement moins penible. */
    if (v.dwMajorVersion < 4) {
        sprintf(buf, "Windows %lu.%lu est anterieur a Windows 95.\n\n"
                     "Ce programme demande Windows 95 ou plus recent.",
                (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion);
        MessageBoxA(NULL, buf, g_app, MB_ICONERROR | MB_OK);
        dkr_win95_log("REFUS : systeme anterieur a Windows 95");
        return DKR_WIN95_STARTUP_TOO_OLD;
    }

    return DKR_WIN95_STARTUP_OK;
}

/* --- demarrage -------------------------------------------------------------- */

static void log_path_next_to_exe(char *out, DWORD cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, cap);
    if (n == 0 || n >= cap) {
        strcpy(out, DKR_LOG_NAME);       /* repli : repertoire courant */
        return;
    }
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/') { n--; }
    out[n] = '\0';
    if (n + sizeof(DKR_LOG_NAME) >= cap) { out[0] = '\0'; }
    strcat(out, DKR_LOG_NAME);
}

int dkr_win95_startup(const char *app_name)
{
    char path[MAX_PATH + 32];
    char buf[MAX_PATH + 64];
    int rc;

    if (app_name && *app_name) {
        strncpy(g_app, app_name, sizeof(g_app) - 1);
        g_app[sizeof(g_app) - 1] = '\0';
    }

    /* `CreateFileA`, pas `fopen` : le journal doit pouvoir s'ouvrir avant toute
       initialisation du CRT, puisqu'il sert justement a diagnostiquer ce qui
       echoue tot. Et `...A`, jamais `...W` — sous Windows 9x, la famille Unicode
       est un bouchon qui echoue avec ERROR_CALL_NOT_IMPLEMENTED. */
    log_path_next_to_exe(path, sizeof(path));
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE) {
        /* Un journal impossible a ouvrir n'est pas fatal en soi, mais il annonce
           un probleme de droits ou de chemin qu'il vaut mieux signaler tout de
           suite que decouvrir plus tard sans trace. */
        sprintf(buf, "Impossible d'ecrire le journal de demarrage :\n%s", path);
        MessageBoxA(NULL, buf, g_app, MB_ICONWARNING | MB_OK);
        return DKR_WIN95_STARTUP_NO_LOG;
    }

    dkr_win95_log("=== journal de demarrage ===");
    dkr_win95_log(g_app);
    dkr_win95_log(path);

    SetUnhandledExceptionFilter(on_unhandled);
    dkr_win95_log("filtre d'exceptions installe");

    rc = check_version();
    if (rc != DKR_WIN95_STARTUP_OK) { return rc; }

    dkr_win95_log("demarrage termine");
    return DKR_WIN95_STARTUP_OK;
}

void dkr_win95_shutdown(void)
{
    if (g_log != INVALID_HANDLE_VALUE) {
        dkr_win95_log("=== fin ===");
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}
