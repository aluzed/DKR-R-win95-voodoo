/* E00-S06 — combien de memoire reste-t-il quand la pile 3dfx est active ?
 *
 * Le budget memoire ne peut pas se deduire de la RAM installee : Windows 95,
 * ses pilotes et Glide en prennent une part qu'il faut mesurer, pas estimer.
 * Ce banc releve MEMORYSTATUS a trois moments — au repos, apres chargement de
 * glide2x.dll, et une fois le contexte Glide ouvert en 640x480 — puis ecrit le
 * resultat sur D:.
 *
 * Meme forme que glidetest.c : pas de CRT, Glide charge par LoadLibrary, pour
 * que le binaire ne depende que de kernel32 et user32.
 */
typedef unsigned int   FxU32;
typedef unsigned char  FxU8;
typedef int            FxBool;

#define WINAPI __stdcall
#define NULL ((void *)0)

__declspec(dllimport) void   *WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void   *WINAPI GetProcAddress(void *, const char *);
__declspec(dllimport) int     WINAPI FreeLibrary(void *);
__declspec(dllimport) void    WINAPI ExitProcess(unsigned int);
__declspec(dllimport) void   *WINAPI CreateFileA(const char *, unsigned int, unsigned int,
                                                 void *, unsigned int, unsigned int, void *);
__declspec(dllimport) int     WINAPI WriteFile(void *, const void *, unsigned int,
                                               unsigned int *, void *);
__declspec(dllimport) int     WINAPI CloseHandle(void *);
__declspec(dllimport) int     WINAPI MessageBoxA(void *, const char *, const char *, unsigned int);

typedef struct {
    unsigned int dwLength, dwMemoryLoad;
    unsigned int dwTotalPhys, dwAvailPhys;
    unsigned int dwTotalPageFile, dwAvailPageFile;
    unsigned int dwTotalVirtual, dwAvailVirtual;
} MEMORYSTATUS;
__declspec(dllimport) void WINAPI GlobalMemoryStatus(MEMORYSTATUS *);

#define GENERIC_WRITE 0x40000000u
#define CREATE_ALWAYS 2u
#define FILE_ATTR_NORM 0x80u
#define INVALID_HANDLE ((void *)-1)

#define GR_RESOLUTION_640x480 0x7
#define GR_REFRESH_60Hz       0x0
#define GR_COLORFORMAT_ARGB   0x0
#define GR_ORIGIN_UPPER_LEFT  0x0

typedef FxU32  (WINAPI *pfn_grGlideInit)(void);
typedef void   (WINAPI *pfn_grGlideShutdown)(void);
typedef FxBool (WINAPI *pfn_grSstQueryHardware)(void *);
typedef void   (WINAPI *pfn_grSstSelect)(int);
typedef FxBool (WINAPI *pfn_grSstWinOpen)(FxU32, int, int, int, int, int, int);
typedef void   (WINAPI *pfn_grSstWinClose)(void);

static char log_buf[2048];
static int  log_len = 0;

static void logs(const char *s)
{
    while (*s && log_len < (int)sizeof(log_buf) - 1) log_buf[log_len++] = *s++;
}

static void lognum(unsigned int v)
{
    char tmp[12];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0 && log_len < (int)sizeof(log_buf) - 1) log_buf[log_len++] = tmp[--i];
}

/* Kio plutot que Mio : a ce niveau de budget, arrondir au Mio efface
   precisement ce qu'on cherche a voir. */
static void report(const char *label)
{
    MEMORYSTATUS ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    logs(label);
    logs("\r\n  phys total   "); lognum(ms.dwTotalPhys    >> 10); logs(" Kio");
    logs("\r\n  phys dispo   "); lognum(ms.dwAvailPhys    >> 10); logs(" Kio");
    logs("\r\n  charge       "); lognum(ms.dwMemoryLoad);         logs(" %");
    logs("\r\n  virt dispo   "); lognum(ms.dwAvailVirtual  >> 10); logs(" Kio");
    logs("\r\n  swap dispo   "); lognum(ms.dwAvailPageFile >> 10); logs(" Kio");
    logs("\r\n\r\n");
}

static void flush_and_exit(int code)
{
    unsigned int written = 0;
    void *h = CreateFileA("D:\\GLIDEMEM.TXT", GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTR_NORM, NULL);
    if (h != INVALID_HANDLE) {
        WriteFile(h, log_buf, (unsigned int)log_len, &written, NULL);
        CloseHandle(h);
    }
    MessageBoxA(NULL, "Mesure terminee.\n\nResultat dans D:\\GLIDEMEM.TXT",
                "Budget memoire", 0x40);
    ExitProcess((unsigned int)code);
}

/* Nom sans underscore : la decoration PE i686 en ajoute un, et c'est ce
   `_start` decore que reclame `-Wl,-e,_start`. Ecrire `_start` ici donne
   `__start`, que le lieur ne trouve pas — il retombe alors silencieusement sur
   0x401000, qui n'est pas l'entree voulue. Meme convention que glidetest.c. */
void start(void)
{
    void *dll;
    pfn_grGlideInit        grGlideInit;
    pfn_grSstQueryHardware grSstQueryHardware;
    pfn_grSstSelect        grSstSelect;
    pfn_grSstWinOpen       grSstWinOpen;
    pfn_grSstWinClose      grSstWinClose;
    pfn_grGlideShutdown    grGlideShutdown;
    char hwinfo[512];

    report("1. au repos, avant tout chargement");

    dll = LoadLibraryA("GLIDE2X.DLL");
    if (!dll) { logs("ECHEC: GLIDE2X.DLL introuvable\r\n"); flush_and_exit(1); }
    report("2. glide2x.dll chargee");

    /* Les exports de Glide 2.x sont en stdcall decore : le nom porte la taille
       de la pile. Sans le suffixe, GetProcAddress echoue silencieusement. */
    grGlideInit        = (pfn_grGlideInit)       GetProcAddress(dll, "_grGlideInit@0");
    grSstQueryHardware = (pfn_grSstQueryHardware)GetProcAddress(dll, "_grSstQueryHardware@4");
    grSstSelect        = (pfn_grSstSelect)       GetProcAddress(dll, "_grSstSelect@4");
    grSstWinOpen       = (pfn_grSstWinOpen)      GetProcAddress(dll, "_grSstWinOpen@28");
    grSstWinClose      = (pfn_grSstWinClose)     GetProcAddress(dll, "_grSstWinClose@0");
    grGlideShutdown    = (pfn_grGlideShutdown)   GetProcAddress(dll, "_grGlideShutdown@0");

    if (!grGlideInit || !grSstQueryHardware || !grSstSelect ||
        !grSstWinOpen || !grSstWinClose || !grGlideShutdown) {
        logs("ECHEC: exports Glide introuvables\r\n"); flush_and_exit(2);
    }

    grGlideInit();
    if (!grSstQueryHardware(hwinfo)) {
        logs("ECHEC: aucun materiel Voodoo detecte\r\n"); flush_and_exit(3);
    }
    grSstSelect(0);

    if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        logs("ECHEC: grSstWinOpen\r\n"); flush_and_exit(4);
    }
    report("3. contexte Glide ouvert, 640x480, double tampon + Z");

    grSstWinClose();
    grGlideShutdown();
    report("4. contexte ferme");

    FreeLibrary(dll);
    flush_and_exit(0);
}
