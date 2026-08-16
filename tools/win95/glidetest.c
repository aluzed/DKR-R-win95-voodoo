/* E09-S01 - Glide demonstration on the Windows 95 / 3dfx test machine.
 *
 * Proves the whole 3dfx stack works: `glide2x.dll` loads, `fxmemmap.vxd` maps the
 * board's registers, a context opens at 640x480, the Voodoo takes control of the
 * screen, clears, draws and swaps its buffers.
 *
 * It is also, deliberately, E00-S02's first step: this program is compiled
 * **without a C library**. It imports nothing but kernel32 and user32, and loads
 * Glide through LoadLibrary. mingw-w64's CRT startup - the expected sticking
 * point under Windows 95 - is therefore out of the equation, and the executable
 * depends on no redistributable.
 *
 * The result is written to D:\GLIDETST.TXT, readable from the host with mtools
 * once the machine is shut down. A screenshot shows the rendering; this file
 * proves the sequence of events, including whatever failed.
 *
 * Building: see tools/win95/build-glidetest.sh
 */

typedef unsigned int   FxU32;
typedef int            FxBool;
typedef unsigned char  FxU8;

#define WINAPI __stdcall
#define NULL ((void *)0)

/* --- the bare minimum of the Win32 API, declared by hand ------------------ */
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

/* Glide 2.x's GrVertex - the field order is `glide.h`'s, and it is not intuitive:
   `ooz` and `a` sit between the colours and `oow`. A "logical" structure
   (x, y, ooz, oow, r, g, b, a) compiles perfectly and renders permuted colours,
   without the slightest error - Glide simply reads the floats at the wrong
   offsets. Verified on screen: with the wrong layout, a red vertex comes out
   green.

   The texture coordinates are not read as long as no texture is active; the
   padding only guarantees the size. */
typedef struct {
    float x, y, z;       /* screen space; z is ignored by Glide */
    float r, g, b;       /* 0..255 */
    float ooz;           /* 65535/Z, depth buffer */
    float a;             /* 0..255 */
    float oow;           /* 1/W, perspective correction */
    float tmuvtx[3 * 4]; /* three TMUs, reserved */
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

/* --- log ----------------------------------------------------------------- */
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
    logs("FAILED: "); logs(msg); logs("\r\n");
    log_flush();
    MessageBoxA(NULL, msg, "Glide test - failed", 0x10 /* MB_ICONERROR */);
    ExitProcess(1);
}

/* Entry point: no CRT, therefore no mainCRTStartup. */
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
    static char hwconfig[4096];   /* GrHwConfiguration, generously sized */
    GrVertex a, b, c;
    int i;

    logs("Glide test - DKR-R port to Windows 95 + 3dfx\r\n");
    logs("------------------------------------------------\r\n");

    dll = LoadLibraryA("glide2x.dll");
    if (!dll) fail("glide2x.dll not found");
    logs("glide2x.dll loaded\r\n");

    /* The exports are __stdcall decorated: _name@bytes. */
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
        fail("Glide symbols missing");
    logs("Glide symbols resolved\r\n");

    grGlideInit();
    logs("grGlideInit\r\n");

    if (!grSstQueryHardware(hwconfig))
        fail("grSstQueryHardware: no 3dfx board");
    /* GrHwConfiguration's first word is the number of boards found. */
    logs("3dfx boards detected: "); lognum(*(int *)hwconfig); logs("\r\n");

    grSstSelect(0);

    if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1))
        fail("grSstWinOpen: 640x480 refused");
    logs("640x480 context open, double buffered\r\n");

    /* Colour taken from the vertices, without a texture. */
    if (grColorCombine)
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0);

    /* Three solid backgrounds in a row: proves the clear and the swap. */
    for (i = 0; i < 3; i++) {
        static const FxU32 colors[3] = { 0x00203080u, 0x00802030u, 0x00308020u };
        grBufferClear(colors[i], 0, 0xFFFFFFFFu);
        grBufferSwap(1);
        Sleep(700);
    }
    logs("grBufferClear + grBufferSwap x3\r\n");

    /* A full-screen triangle, colours interpolated per vertex. */
    for (i = 0; i < (int)(sizeof(a) / sizeof(float)); i++) {
        ((float *)&a)[i] = 0.0f; ((float *)&b)[i] = 0.0f; ((float *)&c)[i] = 0.0f;
    }
    a.x = 320.0f; a.y =  60.0f; a.oow = 1.0f; a.r = 255.0f; a.g =  40.0f; a.b =  40.0f; a.a = 255.0f;
    b.x =  80.0f; b.y = 420.0f; b.oow = 1.0f; b.r =  40.0f; b.g = 255.0f; b.b =  40.0f; b.a = 255.0f;
    c.x = 560.0f; c.y = 420.0f; c.oow = 1.0f; c.r =  40.0f; c.g =  40.0f; c.b = 255.0f; c.a = 255.0f;

    grBufferClear(0x00101018u, 0, 0xFFFFFFFFu);
    if (grDrawTriangle) {
        grDrawTriangle(&a, &b, &c);
        logs("grDrawTriangle: Gouraud triangle\r\n");
    }
    grBufferSwap(1);
    Sleep(6000);

    grSstWinClose();
    grGlideShutdown();
    FreeLibrary(dll);

    logs("------------------------------------------------\r\n");
    logs("SUCCESS: the Glide stack works end to end\r\n");
    log_flush();

    MessageBoxA(NULL,
                "Glide OK: 640x480 context open, clears, swaps and Gouraud "
                "triangle rendered.\n\nDetail in D:\\GLIDETST.TXT",
                "Glide test - success", 0x40 /* MB_ICONINFORMATION */);
    ExitProcess(0);
}
