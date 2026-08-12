/* E00-S02 / E02-S01 — pont de compatibilite Windows 95.
 *
 * La bibliotheque standard de GCC 13 reclame six fonctions que KERNEL32 de
 * Windows 95 n'exporte pas. Aucune n'est appelee par le code du projet : c'est
 * libstdc++ et winpthreads qui les importent. Mais Windows 95 resout *tous* les
 * imports au chargement, donc leur seule presence dans la table suffit a
 * empecher le programme de demarrer — « lie a une exportation manquante ».
 *
 * Ce fichier les fournit. Il se place avant `libkernel32.a` dans l'ordre de
 * resolution du lieur, qui retient alors ces definitions plutot que les
 * declarations d'import.
 *
 * Les six manques, et leur origine :
 *
 *   AddVectoredExceptionHandler      XP      libgcc, gestion d'exceptions
 *   RemoveVectoredExceptionHandler   XP      idem
 *   GetTickCount64                   Vista   winpthreads, horloge monotone
 *   IsDebuggerPresent                98/NT4  libstdc++, diagnostic
 *   SetProcessAffinityMask           NT      winpthreads, placement des fils
 *   TryEnterCriticalSection          98/NT4  std::mutex::try_lock
 *
 * Cinq sur six sont sans consequence. La sixieme demande une explication, plus
 * bas.
 */
#include <windows.h>

/* --- diagnostic ---------------------------------------------------------- */

/* Il n'y a pas de debogueur attache : la reponse est toujours la meme, et elle
   est vraie. */
BOOL WINAPI IsDebuggerPresent(void)
{
    return FALSE;
}

/* --- placement des fils -------------------------------------------------- */

/* La cible est monoprocesseur. Accepter sans rien faire est le comportement
   correct, pas une approximation. */
BOOL WINAPI SetProcessAffinityMask(HANDLE process, DWORD_PTR mask)
{
    (void)process; (void)mask;
    return TRUE;
}

/* --- gestionnaires d'exceptions vectorises ------------------------------- */

/* Windows 95 n'a que `SetUnhandledExceptionFilter`, qui est un point unique et
   non une chaine. libgcc s'en sert de facon optionnelle et teste le retour :
   renvoyer NULL signifie « je n'ai pas pu enregistrer », ce qu'il sait traiter.
   Mentir en renvoyant un jeton non nul serait pire — le desenregistrement
   suivant porterait sur rien. */
PVOID WINAPI AddVectoredExceptionHandler(ULONG first,
                                         PVECTORED_EXCEPTION_HANDLER handler)
{
    (void)first; (void)handler;
    return NULL;
}

ULONG WINAPI RemoveVectoredExceptionHandler(PVOID handle)
{
    (void)handle;
    return 0;
}

/* --- horloge monotone 64 bits -------------------------------------------- */

/* `GetTickCount` revient a zero apres 49,7 jours. On accumule les debordements
   pour rendre un compteur qui, lui, ne revient pas.
   Ce n'est exact que si la fonction est appelee au moins une fois par periode
   de 49 jours — condition largement tenue par une boucle de jeu, et de toute
   facon la machine cible ne reste pas allumee 49 jours. */
ULONGLONG WINAPI GetTickCount64(void)
{
    static volatile LONG high = 0;
    static volatile LONG last = 0;
    DWORD now = GetTickCount();
    LONG  prev = last;

    if ((DWORD)prev > now) {          /* le compteur 32 bits a reboucle */
        InterlockedIncrement((LONG *)&high);
    }
    InterlockedExchange((LONG *)&last, (LONG)now);

    return ((ULONGLONG)(DWORD)high << 32) | (ULONGLONG)now;
}

/* --- sections critiques --------------------------------------------------- *
 *
 * Windows 95 n'offre aucun moyen fiable de *tenter* une entree en section
 * critique, et la premiere version de ce fichier s'est contentee de renvoyer
 * FALSE — reponse licite du contrat, puisque tout appelant doit prevoir l'echec.
 *
 * Elle a fige la machine entiere. `winpthreads` boucle sur
 * `TryEnterCriticalSection` pour prendre ses verrous ; un echec perpetuel donne
 * une attente active qui, sous Windows 95, affame l'ordonnanceur au point que
 * meme l'horloge de la barre des taches s'arrete. Le symptome est spectaculaire
 * et la lecon vaut d'etre gardee : un bouchon « licite » n'est pas un bouchon
 * inoffensif.
 *
 * On fournit donc les **cinq** fonctions de section critique, ce qui permet de
 * disposer librement des 24 octets de CRITICAL_SECTION : puisque tout le binaire
 * passe par nous, leur signification n'appartient qu'a nous.
 *
 *   LockCount      -> mot de verrou : 0 libre, 1 pris
 *   RecursionCount -> profondeur de reentrance
 *   OwningThread   -> identifiant du fil proprietaire
 *   LockSemaphore  -> semaphore de reveil
 *
 * L'echange atomique passe par `__sync_bool_compare_and_swap`, que GCC traduit
 * en `lock cmpxchg` — une instruction du 486, sans appel systeme. C'est ce qui
 * rend l'ensemble possible : Windows 95 n'exporte pas
 * `InterlockedCompareExchange`, mais le processeur, lui, sait le faire.
 */
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    cs->DebugInfo      = NULL;
    cs->LockCount      = 0;
    cs->RecursionCount = 0;
    cs->OwningThread   = NULL;
    cs->LockSemaphore  = CreateSemaphoreA(NULL, 0, 0x7FFFFFFF, NULL);
    cs->SpinCount      = 0;
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    DWORD me = GetCurrentThreadId();

    if ((DWORD)(ULONG_PTR)cs->OwningThread == me) {   /* deja proprietaire */
        cs->RecursionCount++;
        return TRUE;
    }
    if (__sync_bool_compare_and_swap(&cs->LockCount, 0, 1)) {
        cs->OwningThread   = (HANDLE)(ULONG_PTR)me;
        cs->RecursionCount = 1;
        return TRUE;
    }
    return FALSE;
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    /* Attente avec delai plutot qu'infinie : si un reveil se perd entre le test
       et la mise en attente, la boucle le rattrape au tour suivant au lieu de
       dormir pour toujours. */
    while (!TryEnterCriticalSection(cs)) {
        if (cs->LockSemaphore) {
            WaitForSingleObject(cs->LockSemaphore, 1);
        } else {
            Sleep(0);
        }
    }
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    if (--cs->RecursionCount > 0) {
        return;
    }
    cs->OwningThread = NULL;
    __sync_lock_release(&cs->LockCount);
    if (cs->LockSemaphore) {
        ReleaseSemaphore(cs->LockSemaphore, 1, NULL);
    }
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    if (cs->LockSemaphore) {
        CloseHandle(cs->LockSemaphore);
        cs->LockSemaphore = NULL;
    }
    cs->LockCount      = 0;
    cs->RecursionCount = 0;
    cs->OwningThread   = NULL;
}


/* --- redirection des pointeurs d'import ---------------------------------- *
 *
 * Definir les fonctions ne suffit pas. `winpthreads` et `libstdc++` sont
 * compilees avec `__declspec(dllimport)` : leurs appels ne visent pas le
 * symbole `_X@n` mais le pointeur `__imp__X@n`, que la bibliotheque d'import
 * `libkernel32.a` fournirait normalement — et qui designerait une fonction que
 * Windows 95 n'a pas.
 *
 * On definit donc nous-memes ces pointeurs, en les faisant designer nos
 * implementations. Le nom porte la decoration stdcall complete, taille des
 * arguments comprise, d'ou les suffixes @0, @4 et @8.
 *
 * A lier avec `-Wl,--whole-archive` : sans cela l'archive n'est consultee qu'au
 * moment ou elle apparait sur la ligne de commande, avant que `libwinpthread`
 * n'ait introduit les references — et `libkernel32.a`, placee en dernier par
 * les specs du compilateur, l'emporterait.
 */
#define REDIRECT(name, deco)                                                   \
    void *const __imp_##name __asm__("__imp__" #name deco) = (void *)&name

REDIRECT(IsDebuggerPresent,              "@0");
REDIRECT(InitializeCriticalSection,      "@4");
REDIRECT(EnterCriticalSection,           "@4");
REDIRECT(LeaveCriticalSection,           "@4");
REDIRECT(DeleteCriticalSection,          "@4");
REDIRECT(GetTickCount64,                 "@0");
REDIRECT(SetProcessAffinityMask,         "@8");
REDIRECT(TryEnterCriticalSection,        "@4");
REDIRECT(AddVectoredExceptionHandler,    "@8");
REDIRECT(RemoveVectoredExceptionHandler, "@4");
