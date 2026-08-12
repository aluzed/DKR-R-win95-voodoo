/* E09-S01 — Démonstration Glide sur la machine de test Windows 95 / 3dfx.
 *
 * Prouve que toute la pile 3dfx fonctionne : `glide2x.dll` se charge,
 * `fxmemmap.vxd` mappe les registres de la carte, un contexte s'ouvre en
 * 640x480, la Voodoo prend le contrôle de l'écran, efface, dessine et échange
 * ses tampons.
 *
 * C'est aussi, volontairement, la première marche de
 * E00-S02 : ce programme est compilé **sans bibliothèque C**. Il n'importe que
 * kernel32 et user32, et charge Glide par LoadLibrary. Le démarrage du CRT de
 * mingw-w64 — qui est le point d'achoppement attendu sous Windows 95 — est donc
 * hors de l'équation, et l'exécutable ne dépend d'aucun redistribuable.
 *
 * Le résultat est écrit dans D:\GLIDETST.TXT, lisible depuis l'hôte par mtools
 * une fois la machine arrêtée. Une capture d'écran montre le rendu ; ce fichier
 * prouve le déroulé, y compris ce qui a échoué.
 *
 * Compilation : voir tools/win95/build-glidetest.sh
 */

typedef unsigned int   FxU32;
typedef int            FxBool;
typedef unsigned char  FxU8;

#define WINAPI __stdcall
#define NULL ((void *)0)

/* --- le strict minimum de l'API Win32, déclaré à la main ------------------ */
__declspec(dllimport) void   *WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void   *WINAPI GetProcAddress(void *, const char *);
__declspec(dllimport) int     WINAPI FreeLibrary(void *);
__declspec(dllimport) void    WINAPI Sleep(unsigned int);
__declspec(dllimport) void    WINAPI ExitProcess(unsigned int);
__declspec(dllimport) void   *WINAPI CreateFileA(const char *, unsigned int, unsigned int,
                                                 void *, unsigned int, unsigned int, void *);
__declspec(dllimport) int     WINAPI WriteFile(void *, const void *, unsigned int,
                                               unsigned int *, void *);
__declspec(dllimport) int     WINAPI CloseHandle(void *);
__declspec(dllimport) int     WINAPI MessageBoxA(void *, const char *, const char *, unsigned int);

#define GENERIC_WRITE   0x40000000u
#define CREATE_ALWAYS   2u
#define FILE_ATTR_NORM  0x80u
#define INVALID_HANDLE  ((void *)-1)

/* --- Glide 2.x ------------------------------------------------------------ */
#define GR_RESOLUTION_640x480    0x7
#define GR_REFRESH_60Hz          0x0
#define GR_COLORFORMAT_ARGB      0x0
#define GR_ORIGIN_UPPER_LEFT     0x0

#define GR_COMBINE_FUNCTION_LOCAL 0x1
#define GR_COMBINE_FACTOR_NONE    0x0
#define GR_COMBINE_LOCAL_ITERATED 0x0
#define GR_COMBINE_OTHER_NONE     0x3

/* GrVertex de Glide 2.x — l'ordre des champs est celui de `glide.h`, et il n'est
   pas intuitif : `ooz` et `a` s'intercalent entre les couleurs et `oow`. Une
   structure « logique » (x, y, ooz, oow, r, g, b, a) compile parfaitement et
   rend des couleurs permutées, sans la moindre erreur — Glide lit simplement
   les flottants aux mauvais décalages. Vérifié à l'écran : avec la mauvaise
   disposition, un sommet rouge sort vert.

   Les coordonnées de texture ne sont pas lues tant qu'aucune texture n'est
   active ; le remplissage garantit seulement la taille. */
typedef struct {
    float x, y, z;       /* espace écran ; z est ignoré par Glide */
    float r, g, b;       /* 0..255 */
    float ooz;           /* 65535/Z, tampon de profondeur */
    float a;             /* 0..255 */
    float oow;           /* 1/W, correction perspective */
    float tmuvtx[3 * 4]; /* trois TMU, réservées */
} GrVertex;

typedef FxU32  (WINAPI *pfn_grGlideInit)(void);
typedef void   (WINAPI *pfn_grGlideShutdown)(void);
typedef FxBool (WINAPI *pfn_grSstQueryHardware)(void *);
typedef void   (WINAPI *pfn_grSstSelect)(int);
typedef FxBool (WINAPI *pfn_grSstWinOpen)(FxU32, int, int, int, int, int, int);
typedef void   (WINAPI *pfn_grSstWinClose)(void);
typedef void   (WINAPI *pfn_grBufferClear)(FxU32, FxU8, FxU32);
typedef void   (WINAPI *pfn_grBufferSwap)(int);
typedef void   (WINAPI *pfn_grColorCombine)(int, int, int, int, FxBool);
typedef void   (WINAPI *pfn_grDrawTriangle)(const void *, const void *, const void *);

/* --- journal ------------------------------------------------------------- */
static char  log_buf[2048];
static int   log_len = 0;

static void logs(const char *s)
{
    while (*s && log_len < (int)sizeof(log_buf) - 1)
        log_buf[log_len++] = *s++;
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

static void log_flush(void)
{
    unsigned int written = 0;
    void *h = CreateFileA("D:\\GLIDETST.TXT", GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTR_NORM, NULL);
    if (h != INVALID_HANDLE) {
        WriteFile(h, log_buf, (unsigned int)log_len, &written, NULL);
        CloseHandle(h);
    }
}

static void fail(const char *msg)
{
    logs("ECHEC: "); logs(msg); logs("\r\n");
    log_flush();
    MessageBoxA(NULL, msg, "Test Glide - echec", 0x10 /* MB_ICONERROR */);
    ExitProcess(1);
}

/* Point d'entrée : pas de CRT, donc pas de mainCRTStartup. */
void start(void)
{
    void *dll;
    pfn_grGlideInit        grGlideInit;
    pfn_grGlideShutdown    grGlideShutdown;
    pfn_grSstQueryHardware grSstQueryHardware;
    pfn_grSstSelect        grSstSelect;
    pfn_grSstWinOpen       grSstWinOpen;
    pfn_grSstWinClose      grSstWinClose;
    pfn_grBufferClear      grBufferClear;
    pfn_grBufferSwap       grBufferSwap;
    pfn_grColorCombine     grColorCombine;
    pfn_grDrawTriangle     grDrawTriangle;
    static char hwconfig[4096];   /* GrHwConfiguration, généreusement dimensionné */
    GrVertex a, b, c;
    int i;

    logs("Test Glide - portage DKR-R vers Windows 95 + 3dfx\r\n");
    logs("------------------------------------------------\r\n");

    dll = LoadLibraryA("glide2x.dll");
    if (!dll) fail("glide2x.dll introuvable");
    logs("glide2x.dll charge\r\n");

    /* Les exports sont decores __stdcall : _nom@octets. */
    grGlideInit        = (pfn_grGlideInit)        GetProcAddress(dll, "_grGlideInit@0");
    grGlideShutdown    = (pfn_grGlideShutdown)    GetProcAddress(dll, "_grGlideShutdown@0");
    grSstQueryHardware = (pfn_grSstQueryHardware) GetProcAddress(dll, "_grSstQueryHardware@4");
    grSstSelect        = (pfn_grSstSelect)        GetProcAddress(dll, "_grSstSelect@4");
    grSstWinOpen       = (pfn_grSstWinOpen)       GetProcAddress(dll, "_grSstWinOpen@28");
    grSstWinClose      = (pfn_grSstWinClose)      GetProcAddress(dll, "_grSstWinClose@0");
    grBufferClear      = (pfn_grBufferClear)      GetProcAddress(dll, "_grBufferClear@12");
    grBufferSwap       = (pfn_grBufferSwap)       GetProcAddress(dll, "_grBufferSwap@4");
    grColorCombine     = (pfn_grColorCombine)     GetProcAddress(dll, "_grColorCombine@20");
    grDrawTriangle     = (pfn_grDrawTriangle)     GetProcAddress(dll, "_grDrawTriangle@12");

    if (!grGlideInit || !grSstQueryHardware || !grSstSelect || !grSstWinOpen ||
        !grBufferClear || !grBufferSwap || !grSstWinClose || !grGlideShutdown)
        fail("symboles Glide manquants");
    logs("symboles Glide resolus\r\n");

    grGlideInit();
    logs("grGlideInit\r\n");

    if (!grSstQueryHardware(hwconfig))
        fail("grSstQueryHardware : aucune carte 3dfx");
    /* Le premier mot de GrHwConfiguration est le nombre de cartes trouvees. */
    logs("cartes 3dfx detectees : "); lognum(*(int *)hwconfig); logs("\r\n");

    grSstSelect(0);

    if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1))
        fail("grSstWinOpen : ouverture 640x480 refusee");
    logs("contexte 640x480 ouvert, double buffer\r\n");

    /* Couleur issue des sommets, sans texture. */
    if (grColorCombine)
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);

    /* Trois fonds pleins successifs : prouve l'effacement et l'echange. */
    for (i = 0; i < 3; i++) {
        static const FxU32 colors[3] = { 0x00203080u, 0x00802030u, 0x00308020u };
        grBufferClear(colors[i], 0, 0xFFFFFFFFu);
        grBufferSwap(1);
        Sleep(700);
    }
    logs("grBufferClear + grBufferSwap x3\r\n");

    /* Un triangle plein ecran, couleurs interpolees par sommet. */
    for (i = 0; i < (int)(sizeof(a) / sizeof(float)); i++) {
        ((float *)&a)[i] = 0.0f; ((float *)&b)[i] = 0.0f; ((float *)&c)[i] = 0.0f;
    }
    a.x = 320.0f; a.y =  60.0f; a.oow = 1.0f; a.r = 255.0f; a.g =  40.0f; a.b =  40.0f; a.a = 255.0f;
    b.x =  80.0f; b.y = 420.0f; b.oow = 1.0f; b.r =  40.0f; b.g = 255.0f; b.b =  40.0f; b.a = 255.0f;
    c.x = 560.0f; c.y = 420.0f; c.oow = 1.0f; c.r =  40.0f; c.g =  40.0f; c.b = 255.0f; c.a = 255.0f;

    grBufferClear(0x00101018u, 0, 0xFFFFFFFFu);
    if (grDrawTriangle) {
        grDrawTriangle(&a, &b, &c);
        logs("grDrawTriangle : triangle Gouraud\r\n");
    }
    grBufferSwap(1);
    Sleep(6000);

    grSstWinClose();
    grGlideShutdown();
    FreeLibrary(dll);

    logs("------------------------------------------------\r\n");
    logs("SUCCES : la pile Glide fonctionne de bout en bout\r\n");
    log_flush();

    MessageBoxA(NULL,
                "Glide OK : contexte 640x480 ouvert, effacements, echanges "
                "et triangle Gouraud rendus.\n\nDetail dans D:\\GLIDETST.TXT",
                "Test Glide - succes", 0x40 /* MB_ICONINFORMATION */);
    ExitProcess(0);
}
