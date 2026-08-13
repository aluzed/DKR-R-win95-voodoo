/* E01-S03 — couche de compatibilite d'API Windows 95.
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

#include "compat.h"

#include <sys/stat.h>
#include <io.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* --- Les quatre autres manques de MSVCRT, et un de KERNEL32 ---------------- *
 *
 * Trouves a la premiere edition de liens du jeu, et tous de la meme famille que
 * `_fstat64` : des fonctions que les MSVCRT ulterieures ont ajoutees et que
 * celle de Windows 95 n'a pas. Absentes, donc bloquantes au chargement.
 *
 *   _wfopen_s, _wfreopen_s   basic_file.o de libstdc++, ouverture par nom large
 *   _strtoi64, _strtoui64    conversion 64 bits
 *   GetModuleHandleExW       atexit_thread.o de libstdc++
 */

/* Les deux ouvertures larges ne sont jamais atteintes par le code du projet :
   E02-S05 a etabli que passer un `path` a un flux ouvre par l'API large, qui est
   bouchonnee ici, et tous les sites d'appel passent desormais `.string()`.
   L'import, lui, reste — d'ou ces definitions.

   Elles ne se contentent pas d'echouer. Convertir le nom en octets etroits et
   appeler `fopen` est aussi court a ecrire, et rend la fonction juste pour tout
   nom representable dans la page de codes du systeme. Un echec silencieux aurait
   ete un piege pour qui les appellerait un jour sans le savoir. */
static int widen_to_ansi(const wchar_t *w, char *out, int out_size)
{
    BOOL used_default = FALSE;
    int  n;

    if (!w || !out || out_size <= 0) {
        return 0;
    }
    n = WideCharToMultiByte(CP_ACP, 0, w, -1, out, out_size, NULL, &used_default);
    /* Un caractere de remplacement designerait un autre fichier que celui
       demande : mieux vaut refuser que d'ouvrir le mauvais. */
    return (n > 0 && !used_default) ? 1 : 0;
}

int _wfopen_s(FILE **stream, const wchar_t *filename, const wchar_t *mode)
{
    char name[MAX_PATH], m[16];

    if (!stream) {
        return EINVAL;
    }
    *stream = NULL;
    if (!widen_to_ansi(filename, name, sizeof(name)) ||
        !widen_to_ansi(mode, m, sizeof(m))) {
        return EINVAL;
    }
    *stream = fopen(name, m);
    return *stream ? 0 : errno;
}

int _wfreopen_s(FILE **stream, const wchar_t *filename, const wchar_t *mode,
                FILE *old)
{
    char name[MAX_PATH], m[16];

    if (!stream) {
        return EINVAL;
    }
    *stream = NULL;
    if (!widen_to_ansi(filename, name, sizeof(name)) ||
        !widen_to_ansi(mode, m, sizeof(m))) {
        return EINVAL;
    }
    *stream = freopen(name, m, old);
    return *stream ? 0 : errno;
}

/* L'analyse est ecrite ici plutot que deleguee a `strtoll`, et ce n'est pas par
   gout : sous mingw, `strtoll` est un renvoi vers `_strtoi64` **importe de
   MSVCRT**. S'appuyer dessus rendait la definition circulaire — elle compilait,
   se liait, et le symbole absent reapparaissait dans la table d'imports sans que
   rien ne le signale. Le controle des imports l'a vu ; une lecture du code, non.
 *
 * Le contrat suivi est celui de `strtoull` de C99 : espaces en tete, signe
 * facultatif, prefixe `0x` pour la base 16 et base 0 deduite, `endptr` pose sur
 * le premier caractere non consomme — et sur `nptr` si rien n'a ete consomme —
 * et `ERANGE` avec saturation en cas de debordement. */
static unsigned __int64 parse_u64(const char *s, char **end, int base,
                                  int *negative, int *overflow)
{
    const char        *p = s;
    const char        *digits_begin;
    unsigned __int64   value = 0;
    int                any = 0;

    *negative = 0;
    *overflow = 0;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
        p++;
    }
    if (*p == '+' || *p == '-') {
        *negative = (*p == '-');
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    if (base < 2 || base > 36) {
        if (end) { *end = (char *)s; }
        return 0;
    }

    digits_begin = p;
    for (; *p; p++) {
        int digit;
        if (*p >= '0' && *p <= '9')      { digit = *p - '0'; }
        else if (*p >= 'a' && *p <= 'z') { digit = *p - 'a' + 10; }
        else if (*p >= 'A' && *p <= 'Z') { digit = *p - 'A' + 10; }
        else                             { break; }
        if (digit >= base) {
            break;
        }
        if (value > (~(unsigned __int64)0 - (unsigned)digit) / (unsigned)base) {
            *overflow = 1;
        } else {
            value = value * (unsigned)base + (unsigned)digit;
        }
        any = 1;
    }
    /* Rien de consommable : `endptr` revient au depart, prefixe compris. */
    if (end) { *end = (char *)(any ? p : s); }
    (void)digits_begin;
    return value;
}

__int64 _strtoi64(const char *s, char **end, int base)
{
    int negative = 0, overflow = 0;
    unsigned __int64 v;

    if (!s) {
        if (end) { *end = NULL; }
        return 0;
    }
    v = parse_u64(s, end, base, &negative, &overflow);
    if (negative) {
        if (overflow || v > 0x8000000000000000ULL) {
            errno = ERANGE;
            return (__int64)0x8000000000000000ULL;   /* LLONG_MIN */
        }
        return -(__int64)v;
    }
    if (overflow || v > 0x7FFFFFFFFFFFFFFFULL) {
        errno = ERANGE;
        return (__int64)0x7FFFFFFFFFFFFFFFULL;       /* LLONG_MAX */
    }
    return (__int64)v;
}

unsigned __int64 _strtoui64(const char *s, char **end, int base)
{
    int negative = 0, overflow = 0;
    unsigned __int64 v;

    if (!s) {
        if (end) { *end = NULL; }
        return 0;
    }
    v = parse_u64(s, end, base, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ~(unsigned __int64)0;
    }
    /* `strtoull` rend la negation modulaire, et non une erreur. */
    return negative ? (unsigned __int64)(-(__int64)v) : v;
}

/* `strtoll` et `strtoull` viennent avec, et c'est la partie qui manquait.
 *
 * Sous mingw ce ne sont pas des fonctions mais des renvois vers `_strtoi64` et
 * `_strtoui64` **importes de MSVCRT**. Definir les deux precedentes ne suffisait
 * donc pas : `mod_manifest.cpp` et `mods.cpp` appellent `strtoll`, le renvoi
 * etait tire de `libmsvcrt.a`, et l'import absent revenait par cette porte.
 *
 * C'est le meme piege que la premiere version de `_strtoi64`, vu d'un autre
 * cote : sur cette cible, une fonction de la bibliotheque C peut en cacher une
 * autre, et seule la table d'imports du binaire fini le dit. */
long long strtoll(const char *s, char **end, int base)
{
    return (long long)_strtoi64(s, end, base);
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    return (unsigned long long)_strtoui64(s, end, base);
}

/* Reclamee par `atexit_thread.o` de libstdc++, qui s'en sert pour epingler le
   module portant un destructeur de variable locale au fil, afin qu'il ne soit
   pas decharge avant la fin du fil.
 *
 * Ce binaire est entierement statique : il n'y a pas de DLL a maintenir en vie,
 * et le module portant le code est l'executable lui-meme. Rendre son descripteur
 * est donc la reponse juste, et non un pis-aller. L'epinglage demande n'a rien a
 * faire — un executable ne se decharge pas. */
BOOL WINAPI GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module)
{
    (void)flags;
    (void)name;
    if (!module) {
        return FALSE;
    }
    *module = GetModuleHandleA(NULL);
    return *module != NULL;
}

/* --- <fstream> : `_fstat64` de MSVCRT ------------------------------------- *
 *
 * Le seul manque qui ne vienne pas de KERNEL32, et il coute cher : la simple
 * inclusion de `<fstream>` rend le binaire inchargeable sous Windows 95.
 *
 * `basic_file.o` de libstdc++ importe `__imp___fstat64`. La MSVCRT.DLL de
 * Windows 95 n'exporte que la famille `_fstat` d'origine — les variantes 64 bits
 * sont arrivees bien plus tard. Mesure : un binaire qui n'inclut que `<cstdio>`
 * se charge, un binaire qui inclut `<fstream>` ne se charge pas.
 *
 * L'enjeu depasse les suites d'epreuve. Treize fichiers du projet emploient
 * `<fstream>`, dont le coeur de `librecomp` — `recomp.cpp`, `pi.cpp`, `sp.cpp`.
 * Sans cette fonction, le jeu ne se lierait pas pour cette cible.
 *
 * Ce dont libstdc++ se sert reellement est etroit : `showmanyc()` demande
 * `st_mode` pour savoir si le descripteur designe un fichier ordinaire, et
 * `st_size` pour dire combien d'octets restent a lire. Le reste de la structure
 * est mis a zero plutot que rempli au jugé — une date fausse serait pire qu'une
 * date absente, parce qu'elle aurait l'air d'une donnee.
 */

int _fstat64(int fd, struct _stat64 *st)
{
    HANDLE h;
    DWORD  type, low, high = 0;

    if (!st) {
        errno = EINVAL;
        return -1;
    }
    h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;

    type = GetFileType(h);
    if (type == FILE_TYPE_DISK) {
        st->st_mode = _S_IFREG | _S_IREAD | _S_IWRITE;
        low = GetFileSize(h, &high);
        if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
            errno = EBADF;
            return -1;
        }
        st->st_size = ((__int64)high << 32) | (__int64)low;
    } else if (type == FILE_TYPE_CHAR) {
        /* La console et NUL. `showmanyc` doit alors rendre « je ne sais pas »,
           ce qu'il fait des lors que ce n'est pas un fichier ordinaire. */
        st->st_mode = _S_IFCHR;
    } else if (type == FILE_TYPE_PIPE) {
        st->st_mode = _S_IFIFO;
    } else {
        errno = EBADF;
        return -1;
    }
    return 0;
}


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

/* `GetTickCount` revient a zero apres 49,7 jours. On accumule les rebouclages
   pour rendre un compteur qui, lui, ne revient pas.
 *
 * La logique est isolee en fonction pure pour que le passage a zero se teste au
 * lieu de s'attendre sept semaines : `platform/win95/tests/test_tick64.c` la
 * pilote avec des valeurs choisies, sur l'hote, sans Windows.
 *
 * Condition de validite : etre appele au moins une fois par periode de 49 jours.
 * Une boucle de jeu la tient largement ; un programme qui dormirait plus
 * longtemps entre deux lectures verrait un rebouclage lui echapper. C'est une
 * limite reelle, ecrite dans docs/WIN95-COMPAT.md.
 */
/* `dkr_tick64_step` vit dans `tick64.c` : sans dependance a Windows, elle est
   pilotable par un test sur l'hote. */


/* Un verrou tournant plutot qu'une section critique : `GetTickCount64` peut
   etre appelee depuis n'importe quel fil, y compris pendant l'initialisation ou
   aucune section critique n'est encore prete. `__sync_*` se compile en
   `lock cmpxchg`, sans appel systeme. */
static volatile long   dkr_tick_lock  = 0;
static dkr_tick64_state dkr_tick_state = { 0, 0 };

ULONGLONG WINAPI GetTickCount64(void)
{
    unsigned long long v;
    while (!__sync_bool_compare_and_swap(&dkr_tick_lock, 0, 1)) { /* attente */ }
    v = dkr_tick64_step(&dkr_tick_state, (unsigned long)GetTickCount());
    __sync_lock_release(&dkr_tick_lock);
    return v;
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


/* --- CreateSemaphoreW : exportee, mais vide (E02-S01) --------------------- *
 *
 * Celle-ci n'est pas du meme genre que les six precedentes. Les six manquaient a
 * la table d'exports, et leur absence est bruyante : le programme ne demarre
 * pas, et Windows nomme le symbole. `CreateSemaphoreW`, elle, *est* exportee.
 * Elle ne fait simplement rien :
 *
 *     0x03500a:  33 c0              xor  eax,eax     ; retour 0
 *                b1 04              mov  cl,0x4      ; index du bouchon
 *                e9 06 c3 fc ff     jmp  0x1319      ; queue commune
 *     0x001319:  51                 push ecx
 *                68 78 00 00 00     push 0x78        ; ERROR_CALL_NOT_IMPLEMENTED
 *                e8 be c7 00 00     call SetLastError
 *
 * Elle partage son adresse avec `CreateEventW`, ce qui ne laisse aucun doute :
 * aucune des deux n'a de code. Releve sur la KERNEL32.DLL de la machine de test.
 *
 * Pourquoi cela compte : `moodycamel::LightweightSemaphore` l'appelle, et c'est
 * le primitif de blocage sur lequel repose *tout* le planificateur
 * d'`ultramodern` — le semaphore `running` de chaque fil de jeu, et chaque
 * `BlockingConcurrentQueue`. Avec un descripteur nul, les deux cotes cassent, et
 * differemment :
 *
 *   - `wait()`  -> `WaitForSingleObject(NULL, INFINITE)` echoue au lieu de
 *     bloquer. `ultramodern` ignore le retour : le fil poursuit comme s'il avait
 *     ete reveille. Les fils de jeu, qui doivent courir un par un, courent alors
 *     tous en meme temps.
 *   - `signal()` -> `while (!ReleaseSemaphore(NULL, ...));` — une boucle qui ne
 *     se termine jamais. C'est la meme famine d'ordonnanceur que la premiere
 *     version de `TryEnterCriticalSection`, et le meme symptome : la machine
 *     entiere se fige.
 *
 * Le controle d'imports de E01-S04 ne peut rien y voir, puisque le symbole est
 * bien exporte. C'est pourquoi il est desormais double d'une liste de bouchons
 * connus (`tools/win95/exports/stubs.json`).
 *
 * Le contournement est immediat : `CreateSemaphoreA` existe et fonctionne. Le
 * nom, quand il y en a un, est converti. `ultramodern` n'en pose aucun — ses
 * semaphores sont anonymes — mais rendre un semaphore anonyme la ou l'appelant
 * en a demande un nomme casserait le partage entre processus sans le dire.
 */
HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES attributes,
                               LONG initial_count, LONG maximum_count,
                               LPCWSTR name)
{
    /* MAX_PATH est la longueur maximale d'un nom d'objet noyau : un tampon plus
       court ferait echouer la couche la ou l'API d'origine aurait reussi. */
    char  narrow[MAX_PATH + 1];
    char *narrow_name = NULL;
    BOOL  substituted = FALSE;

    if (name) {
        /* Le tampon borne la conversion : au-dela, WideCharToMultiByte echoue
           avec ERROR_INSUFFICIENT_BUFFER plutot que d'ecrire hors limites. On
           laisse son code d'erreur en place au lieu d'en poser un autre — il
           dit precisement ce qui s'est passe, et c'est tout ce que l'appelant
           pourra lire.

           `substituted` n'est pas un ornement. Sans lui, un caractere absent de
           la page de codes du systeme devient « ? » en silence, et deux noms
           larges differents s'effondrent sur un meme nom etroit : deux
           processus croiraient ouvrir des semaphores distincts et
           partageraient le meme. Puisque la conversion du nom n'a d'autre
           raison d'etre que de preserver ce partage, une substitution la vide
           de son sens — et on echoue plutot que de mentir. */
        int n = WideCharToMultiByte(CP_ACP, 0, name, -1, narrow,
                                    (int)sizeof(narrow), NULL, &substituted);
        if (n <= 0) {
            return NULL;
        }
        if (substituted) {
            SetLastError(ERROR_INVALID_NAME);
            return NULL;
        }
        narrow_name = narrow;
    }
    return CreateSemaphoreA(attributes, initial_count, maximum_count, narrow_name);
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
REDIRECT(CreateSemaphoreW,               "@16");
/* cdecl : pas de suffixe de taille d'arguments, a la difference des
   fonctions de KERNEL32 ci-dessus. */
REDIRECT(_fstat64,                       "");
REDIRECT(_wfopen_s,                      "");
REDIRECT(_wfreopen_s,                    "");
REDIRECT(_strtoi64,                      "");
REDIRECT(_strtoui64,                     "");
REDIRECT(strtoll,                        "");
REDIRECT(strtoull,                       "");
REDIRECT(GetModuleHandleExW,             "@12");
