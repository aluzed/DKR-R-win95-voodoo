/* E01-S03 — temoin exercant l'ensemble de la couche de plate-forme.
 *
 * Critere d'acceptation du ticket : « un executable temoin utilisant l'ensemble
 * de la couche demarre sous Windows 95 emule ». Il exerce donc, dans l'ordre :
 *
 *   le demarrage        journal dans un fichier, filtre d'exceptions, version
 *   les six API         celles que Windows 95 n'exporte pas
 *   l'horloge 64 bits   avec le rebouclage deja couvert par un test sur l'hote
 *   les fils            deux fils, une section critique, un evenement
 *
 * Il ecrit son resultat sur D: comme les autres temoins, et l'affiche.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "compat.h"
#include "startup.h"

static CRITICAL_SECTION cs;
static HANDLE done_event;
static long   counter = 0;

static DWORD WINAPI worker(LPVOID param)
{
    int i;
    (void)param;
    for (i = 0; i < 2000; i++) {
        EnterCriticalSection(&cs);
        counter++;
        LeaveCriticalSection(&cs);
    }
    SetEvent(done_event);
    return 0;
}

int main(void)
{
    char report[1024];
    int  n = 0;
    int  rc;
    HANDLE th;
    DWORD tid = 0;
    ULONGLONG t0, t1;
    BOOL tried;
    FILE *f;

    rc = dkr_win95_startup("Temoin plate-forme");
    if (rc != DKR_WIN95_STARTUP_OK) {
        return rc;                       /* le message a deja ete affiche */
    }

    n += sprintf(report + n, "Temoin de la couche plate-forme Win95\r\n");
    n += sprintf(report + n, "=====================================\r\n");

    /* --- les six API que Windows 95 n'exporte pas ------------------------- */
    dkr_win95_log("controle des API de compatibilite");

    n += sprintf(report + n, "IsDebuggerPresent      : %s\r\n",
                 IsDebuggerPresent() ? "vrai (inattendu)" : "faux");
    n += sprintf(report + n, "SetProcessAffinityMask : %s\r\n",
                 SetProcessAffinityMask(GetCurrentProcess(), 1) ? "accepte" : "refuse");

    /* --- horloge 64 bits -------------------------------------------------- */
    t0 = GetTickCount64();
    Sleep(120);
    t1 = GetTickCount64();
    n += sprintf(report + n, "GetTickCount64         : %lu ms ecoulees\r\n",
                 (unsigned long)(t1 - t0));
    dkr_win95_log_num("ecart d'horloge (ms)", (long)(t1 - t0));

    /* --- sections critiques et fils --------------------------------------- */
    InitializeCriticalSection(&cs);
    done_event = CreateEventA(NULL, TRUE, FALSE, NULL);

    /* La section critique est libre : la tentative doit reussir. C'est le seul
       endroit ou `TryEnterCriticalSection` est reellement exercee — Windows 95
       ne l'exporte pas, et sa premiere version dans cette couche figeait la
       machine entiere. */
    tried = TryEnterCriticalSection(&cs);
    if (tried) { LeaveCriticalSection(&cs); }
    n += sprintf(report + n, "TryEnterCriticalSection: %s\r\n",
                 tried ? "verrou libre pris" : "ECHEC");

    th = CreateThread(NULL, 0, worker, NULL, 0, &tid);
    if (!th) {
        dkr_win95_log("CreateThread a echoue");
        n += sprintf(report + n, "fils                   : ECHEC de creation\r\n");
    } else {
        int i;
        for (i = 0; i < 2000; i++) {     /* contention reelle entre deux fils */
            EnterCriticalSection(&cs);
            counter++;
            LeaveCriticalSection(&cs);
        }
        WaitForSingleObject(done_event, 10000);
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
        n += sprintf(report + n, "deux fils, 4000 tours  : compteur = %ld / 4000\r\n",
                     counter);
    }
    CloseHandle(done_event);
    DeleteCriticalSection(&cs);
    dkr_win95_log_num("compteur final", counter);

    n += sprintf(report + n, "\r\njournal de demarrage : DKR-BOOT.LOG\r\n");

    f = fopen("D:\\PLATFORM.TXT", "wb");
    if (f) { fwrite(report, 1, (size_t)n, f); fclose(f); }

    MessageBoxA(NULL, report, "Temoin plate-forme", MB_ICONINFORMATION | MB_OK);
    dkr_win95_shutdown();
    return counter == 4000 ? 0 : 1;
}
