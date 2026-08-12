/* E00-S05 — Glide 3.x fonctionne-t-il sur la cible ?
 *
 * Le pilote de reference 3dfx installe `glide2x.dll` et `glide3x.dll` cote a
 * cote. E09-S01 a prouve glide2x ; l'ADR de cible materielle ne peut pas
 * choisir Glide 3.x sans la meme preuve.
 *
 * Ce banc charge glide3x, interroge ses chaines d'identification, ouvre un
 * contexte 640x480 et effectue un echange de tampons. Il n'affiche rien : ce
 * qui est teste, c'est que l'API repond, pas le rendu — celui-ci est deja
 * couvert par la demonstration Glide de E09-S01.
 *
 * Glide 3 remplace `grSstQueryHardware` par `grGet`/`grGetString`, et surtout
 * `grVertexLayout` : le format de sommet se declare au lieu de devoir
 * correspondre a une structure figee. C'est exactement le piege qui a coute une
 * demi-journee en E09-S01, ou un sommet rouge sortait vert parce que `ooz` et
 * `a` s'intercalent entre les couleurs et `oow`.
 */
typedef unsigned int FxU32;
typedef int          FxI32;
typedef int          FxBool;

#define WINAPI __stdcall
#define NULL ((void *)0)

__declspec(dllimport) void *WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void *WINAPI GetProcAddress(void *, const char *);
__declspec(dllimport) int   WINAPI FreeLibrary(void *);
__declspec(dllimport) void  WINAPI ExitProcess(unsigned);
__declspec(dllimport) void *WINAPI CreateFileA(const char *, unsigned, unsigned,
                                               void *, unsigned, unsigned, void *);
__declspec(dllimport) int   WINAPI WriteFile(void *, const void *, unsigned,
                                             unsigned *, void *);
__declspec(dllimport) int   WINAPI CloseHandle(void *);
__declspec(dllimport) int   WINAPI MessageBoxA(void *, const char *, const char *, unsigned);

#define GENERIC_WRITE  0x40000000u
#define CREATE_ALWAYS  2u
#define FILE_ATTR_NORM 0x80u
#define INVALID_HANDLE ((void *)-1)

#define GR_RESOLUTION_640x480 0x7
#define GR_REFRESH_60Hz       0x0
#define GR_COLORFORMAT_ARGB   0x0
#define GR_ORIGIN_UPPER_LEFT  0x0

/* Chaines d'identification de Glide 3. */
#define GR_HARDWARE 0xF001
#define GR_RENDERER 0xF002
#define GR_VENDOR   0xF003
#define GR_VERSION  0xF004

/* Entiers interrogeables. Les valeurs sont celles de `glide.h` de Glide 3.1 ;
   si l'une d'elles se revele fausse, `grGet` renvoie 0 et on le voit. */
#define GR_NUM_BOARDS 0x0F
#define GR_NUM_TMU    0x11
#define GR_MEMORY_FB  0x0D
#define GR_MEMORY_TMU 0x0E

typedef FxU32 (WINAPI *pfn_grGlideInit)(void);
typedef void  (WINAPI *pfn_grGlideShutdown)(void);
typedef void  (WINAPI *pfn_grSstSelect)(int);
typedef FxU32 (WINAPI *pfn_grSstWinOpen)(FxU32, int, int, int, int, int, int);
typedef void  (WINAPI *pfn_grSstWinClose)(FxU32);
typedef void  (WINAPI *pfn_grBufferClear)(FxU32, unsigned char, FxU32);
typedef void  (WINAPI *pfn_grBufferSwap)(FxU32);
typedef const char * (WINAPI *pfn_grGetString)(FxU32);
typedef FxU32 (WINAPI *pfn_grGet)(FxU32, FxU32, FxI32 *);

static char log_buf[3072];
static int  log_len = 0;

static void logs(const char *s)
{
    if (!s) { logs("(null)"); return; }
    while (*s && log_len < (int)sizeof(log_buf) - 1) log_buf[log_len++] = *s++;
}

static void lognum(int v)
{
    char tmp[12];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    if (neg && log_len < (int)sizeof(log_buf) - 1) log_buf[log_len++] = '-';
    while (i > 0 && log_len < (int)sizeof(log_buf) - 1) log_buf[log_len++] = tmp[--i];
}

static void finish(int code)
{
    unsigned written = 0;
    void *h = CreateFileA("D:\\GLIDE3.TXT", GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTR_NORM, NULL);
    if (h != INVALID_HANDLE) {
        WriteFile(h, log_buf, (unsigned)log_len, &written, NULL);
        CloseHandle(h);
    }
    MessageBoxA(NULL, "Essai Glide 3 termine.\n\nResultat dans D:\\GLIDE3.TXT",
                "Glide 3.x", 0x40);
    ExitProcess((unsigned)code);
}

void start(void)
{
    void *dll;
    pfn_grGlideInit     grGlideInit;
    pfn_grGlideShutdown grGlideShutdown;
    pfn_grSstSelect     grSstSelect;
    pfn_grSstWinOpen    grSstWinOpen;
    pfn_grSstWinClose   grSstWinClose;
    pfn_grBufferClear   grBufferClear;
    pfn_grBufferSwap    grBufferSwap;
    pfn_grGetString     grGetString;
    pfn_grGet           grGet;
    FxU32 ctx;
    FxI32 val;

    logs("Essai de Glide 3.x sur la cible\r\n");
    logs("===============================\r\n");

    dll = LoadLibraryA("GLIDE3X.DLL");
    if (!dll) { logs("ECHEC: GLIDE3X.DLL introuvable\r\n"); finish(1); }
    logs("GLIDE3X.DLL chargee\r\n");

    grGlideInit     = (pfn_grGlideInit)    GetProcAddress(dll, "_grGlideInit@0");
    grGlideShutdown = (pfn_grGlideShutdown)GetProcAddress(dll, "_grGlideShutdown@0");
    grSstSelect     = (pfn_grSstSelect)    GetProcAddress(dll, "_grSstSelect@4");
    grSstWinOpen    = (pfn_grSstWinOpen)   GetProcAddress(dll, "_grSstWinOpen@28");
    grSstWinClose   = (pfn_grSstWinClose)  GetProcAddress(dll, "_grSstWinClose@4");
    grBufferClear   = (pfn_grBufferClear)  GetProcAddress(dll, "_grBufferClear@12");
    grBufferSwap    = (pfn_grBufferSwap)   GetProcAddress(dll, "_grBufferSwap@4");
    grGetString     = (pfn_grGetString)    GetProcAddress(dll, "_grGetString@4");
    grGet           = (pfn_grGet)          GetProcAddress(dll, "_grGet@12");

    logs("exports  : grGlideInit "); logs(grGlideInit ? "ok" : "ABSENT");
    logs(", grSstWinOpen ");         logs(grSstWinOpen ? "ok" : "ABSENT");
    logs(", grGetString ");          logs(grGetString ? "ok" : "ABSENT");
    logs(", grGet ");                logs(grGet ? "ok" : "ABSENT");
    logs("\r\n");

    if (!grGlideInit || !grSstWinOpen || !grBufferClear || !grBufferSwap) {
        logs("ECHEC: exports essentiels manquants\r\n"); finish(2);
    }

    grGlideInit();
    if (grSstSelect) grSstSelect(0);

    if (grGetString) {
        logs("vendeur  : "); logs(grGetString(GR_VENDOR));   logs("\r\n");
        logs("materiel : "); logs(grGetString(GR_HARDWARE)); logs("\r\n");
        logs("rendu    : "); logs(grGetString(GR_RENDERER)); logs("\r\n");
        logs("version  : "); logs(grGetString(GR_VERSION));  logs("\r\n");
    }

    if (grGet) {
        val = 0; grGet(GR_NUM_BOARDS, 4, &val);
        logs("cartes   : "); lognum(val); logs("\r\n");
        val = 0; grGet(GR_NUM_TMU, 4, &val);
        logs("TMU      : "); lognum(val); logs("\r\n");
        val = 0; grGet(GR_MEMORY_FB, 4, &val);
        logs("mem image: "); lognum(val); logs(" octets\r\n");
        val = 0; grGet(GR_MEMORY_TMU, 4, &val);
        logs("mem TMU  : "); lognum(val); logs(" octets\r\n");
    }

    ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
        logs("ECHEC: grSstWinOpen a refuse 640x480 double tampon + Z\r\n");
        finish(3);
    }
    logs("contexte : ouvert en 640x480, double tampon + Z\r\n");

    grBufferClear(0x00204060, 0, 0xFFFF);
    grBufferSwap(1);
    logs("echange  : effectue\r\n");

    if (grSstWinClose)  grSstWinClose(ctx);
    if (grGlideShutdown) grGlideShutdown();
    FreeLibrary(dll);

    logs("\r\nGlide 3.x est fonctionnel sur cette machine.\r\n");
    finish(0);
}
