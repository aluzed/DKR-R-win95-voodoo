/* E00-S05 - does Glide 3.x work on the target?
 *
 * 3dfx's reference driver installs `glide2x.dll` and `glide3x.dll` side by side.
 * E09-S01 proved glide2x; the hardware-target ADR cannot pick Glide 3.x without
 * the same proof.
 *
 * This bench loads glide3x, queries its identification strings, opens a 640x480
 * context and performs a buffer swap. It displays nothing: what is tested is
 * that the API answers, not the rendering - that is already covered by E09-S01's
 * Glide demonstration.
 *
 * Glide 3 replaces `grSstQueryHardware` with `grGet`/`grGetString`, and above all
 * with `grVertexLayout`: the vertex format is declared instead of having to match
 * a fixed structure. That is exactly the trap which cost half a day in E09-S01,
 * where a red vertex came out green because `ooz` and `a` sit between the colours
 * and `oow`.
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

/* Queryable integers. The values are those of Glide 3.1's `glide.h`; if one of
   them turns out to be wrong, `grGet` returns 0 and it shows. */
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
    MessageBoxA(NULL, "Glide 3 trial finished.\n\nResult in D:\\GLIDE3.TXT",
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

    logs("Trying Glide 3.x on the target\r\n");
    logs("==============================\r\n");

    dll = LoadLibraryA("GLIDE3X.DLL");
    if (!dll) { logs("FAILED: GLIDE3X.DLL not found\r\n"); finish(1); }
    logs("GLIDE3X.DLL loaded\r\n");

    grGlideInit     = (pfn_grGlideInit)    GetProcAddress(dll, "_grGlideInit@0");
    grGlideShutdown = (pfn_grGlideShutdown)GetProcAddress(dll, "_grGlideShutdown@0");
    grSstSelect     = (pfn_grSstSelect)    GetProcAddress(dll, "_grSstSelect@4");
    grSstWinOpen    = (pfn_grSstWinOpen)   GetProcAddress(dll, "_grSstWinOpen@28");
    grSstWinClose   = (pfn_grSstWinClose)  GetProcAddress(dll, "_grSstWinClose@4");
    grBufferClear   = (pfn_grBufferClear)  GetProcAddress(dll, "_grBufferClear@12");
    grBufferSwap    = (pfn_grBufferSwap)   GetProcAddress(dll, "_grBufferSwap@4");
    grGetString     = (pfn_grGetString)    GetProcAddress(dll, "_grGetString@4");
    grGet           = (pfn_grGet)          GetProcAddress(dll, "_grGet@12");

    logs("exports  : grGlideInit "); logs(grGlideInit ? "ok" : "MISSING");
    logs(", grSstWinOpen ");         logs(grSstWinOpen ? "ok" : "MISSING");
    logs(", grGetString ");          logs(grGetString ? "ok" : "MISSING");
    logs(", grGet ");                logs(grGet ? "ok" : "MISSING");
    logs("\r\n");

    if (!grGlideInit || !grSstWinOpen || !grBufferClear || !grBufferSwap) {
        logs("FAILED: essential exports missing\r\n"); finish(2);
    }

    grGlideInit();
    if (grSstSelect) grSstSelect(0);

    if (grGetString) {
        logs("vendor   : "); logs(grGetString(GR_VENDOR));   logs("\r\n");
        logs("hardware : "); logs(grGetString(GR_HARDWARE)); logs("\r\n");
        logs("renderer : "); logs(grGetString(GR_RENDERER)); logs("\r\n");
        logs("version  : "); logs(grGetString(GR_VERSION));  logs("\r\n");
    }

    if (grGet) {
        val = 0; grGet(GR_NUM_BOARDS, 4, &val);
        logs("boards   : "); lognum(val); logs("\r\n");
        val = 0; grGet(GR_NUM_TMU, 4, &val);
        logs("TMUs     : "); lognum(val); logs("\r\n");
        val = 0; grGet(GR_MEMORY_FB, 4, &val);
        logs("fb mem   : "); lognum(val); logs(" bytes\r\n");
        val = 0; grGet(GR_MEMORY_TMU, 4, &val);
        logs("TMU mem  : "); lognum(val); logs(" bytes\r\n");
    }

    ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
        logs("FAILED: grSstWinOpen refused 640x480 double buffer + Z\r\n");
        finish(3);
    }
    logs("context  : open at 640x480, double buffer + Z\r\n");

    grBufferClear(0x00204060, 0, 0xFFFF);
    grBufferSwap(1);
    logs("swap     : done\r\n");

    if (grSstWinClose)  grSstWinClose(ctx);
    if (grGlideShutdown) grGlideShutdown();
    FreeLibrary(dll);

    logs("\r\nGlide 3.x is functional on this machine.\r\n");
    finish(0);
}
