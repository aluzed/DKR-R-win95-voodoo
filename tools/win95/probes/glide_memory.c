/* E00-S06 - how much memory is left once the 3dfx stack is up?
 *
 * The memory budget cannot be deduced from the installed RAM: Windows 95, its
 * drivers and Glide take a share of it that must be measured, not estimated.
 * This bench reads MEMORYSTATUS at three moments - at rest, after loading
 * glide2x.dll, and once the Glide context is open at 640x480 - then writes the
 * result to D:.
 *
 * Same shape as glidetest.c: no CRT, Glide loaded by LoadLibrary, so that the
 * binary depends on nothing but kernel32 and user32.
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

/* KiB rather than MiB: at this budget's scale, rounding to the MiB erases
   precisely what we are trying to see. */
static void report(const char *label)
{
    MEMORYSTATUS ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    logs(label);
    logs("\r\n  phys total   "); lognum(ms.dwTotalPhys    >> 10); logs(" KiB");
    logs("\r\n  phys free    "); lognum(ms.dwAvailPhys    >> 10); logs(" KiB");
    logs("\r\n  load         "); lognum(ms.dwMemoryLoad);         logs(" %");
    logs("\r\n  virt free    "); lognum(ms.dwAvailVirtual  >> 10); logs(" KiB");
    logs("\r\n  swap free    "); lognum(ms.dwAvailPageFile >> 10); logs(" KiB");
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
    MessageBoxA(NULL, "Measurement finished.\n\nResult in D:\\GLIDEMEM.TXT",
                "Memory budget", 0x40);
    ExitProcess((unsigned int)code);
}

/* Name without an underscore: the i686 PE decoration adds one, and it is that
   decorated `_start` which `-Wl,-e,_start` asks for. Writing `_start` here gives
   `__start`, which the linker does not find - it then silently falls back to
   0x401000, which is not the intended entry point. Same convention as
   glidetest.c. */
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

    report("1. at rest, before any loading");

    dll = LoadLibraryA("GLIDE2X.DLL");
    if (!dll) { logs("FAILED: GLIDE2X.DLL not found\r\n"); flush_and_exit(1); }
    report("2. glide2x.dll loaded");

    /* Glide 2.x's exports are decorated stdcall: the name carries the stack
       size. Without the suffix, GetProcAddress fails silently. */
    grGlideInit        = (pfn_grGlideInit)       GetProcAddress(dll, "_grGlideInit@0");
    grSstQueryHardware = (pfn_grSstQueryHardware)GetProcAddress(dll, "_grSstQueryHardware@4");
    grSstSelect        = (pfn_grSstSelect)       GetProcAddress(dll, "_grSstSelect@4");
    grSstWinOpen       = (pfn_grSstWinOpen)      GetProcAddress(dll, "_grSstWinOpen@28");
    grSstWinClose      = (pfn_grSstWinClose)     GetProcAddress(dll, "_grSstWinClose@0");
    grGlideShutdown    = (pfn_grGlideShutdown)   GetProcAddress(dll, "_grGlideShutdown@0");

    if (!grGlideInit || !grSstQueryHardware || !grSstSelect ||
        !grSstWinOpen || !grSstWinClose || !grGlideShutdown) {
        logs("FAILED: Glide exports not found\r\n"); flush_and_exit(2);
    }

    grGlideInit();
    if (!grSstQueryHardware(hwinfo)) {
        logs("FAILED: no Voodoo hardware detected\r\n"); flush_and_exit(3);
    }
    grSstSelect(0);

    if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        logs("FAILED: grSstWinOpen\r\n"); flush_and_exit(4);
    }
    report("3. Glide context open, 640x480, double buffer + Z");

    grSstWinClose();
    grGlideShutdown();
    report("4. context closed");

    FreeLibrary(dll);
    flush_and_exit(0);
}
