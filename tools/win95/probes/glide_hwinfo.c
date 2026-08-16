/* E00-S05 - what does the card say about itself?
 *
 * E09-S01 showed that 86Box's configuration file lies: hand-written Voodoo
 * settings were silently ignored there, and the machine emulated a 2 MB Voodoo 1
 * while the file announced a 4 MB Voodoo 2. Any texture budget established from
 * the file would therefore be wrong.
 *
 * This bench puts the question to Glide: `grSstQueryHardware` fills a
 * GrHwConfiguration from which we extract the board type, the frame buffer
 * memory, the number of TMUs and the memory of each. That is the same
 * information the engine will consult at run time (E05-S01), obtained by the
 * same path.
 *
 * The structure is also dumped raw: if the assumed layout did not match this
 * driver's, the integers stay readable and the mistake shows instead of
 * hiding.
 */
typedef unsigned int  FxU32;
typedef int           FxBool;

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

/* Glide 2.x's layout, as 3dfx's `glide.h` declares it. */
#define MAX_NUM_SST   4
#define GLIDE_NUM_TMU 3

typedef struct { int tmuRev; int tmuRam; } GrTMUConfig_t;

typedef struct {
    int fbRam;                 /* MB of frame buffer memory */
    int fbiRev;
    int nTexelfx;              /* number of TMUs */
    FxBool sliDetect;
    GrTMUConfig_t tmuConfig[GLIDE_NUM_TMU];
} GrVoodooConfig_t;

typedef struct {
    int num_sst;
    struct {
        int type;              /* 0 Voodoo, 1 SST96, 2 AT3D, 3 Voodoo2 */
        union {
            GrVoodooConfig_t VoodooConfig;
            char pad[128];
        } sstBoard;
    } SSTs[MAX_NUM_SST];
} GrHwConfiguration;

typedef FxU32  (WINAPI *pfn_grGlideInit)(void);
typedef void   (WINAPI *pfn_grGlideShutdown)(void);
typedef FxBool (WINAPI *pfn_grSstQueryHardware)(GrHwConfiguration *);
typedef void   (WINAPI *pfn_grGlideGetVersion)(char *);

static char log_buf[4096];
static int  log_len = 0;

static void logs(const char *s)
{
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

static void loghex(unsigned v)
{
    const char *d = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4) {
        if (log_len < (int)sizeof(log_buf) - 1) log_buf[log_len++] = d[(v >> i) & 0xF];
    }
}

static void finish(int code)
{
    unsigned written = 0;
    void *h = CreateFileA("D:\\GLIDEHW.TXT", GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTR_NORM, NULL);
    if (h != INVALID_HANDLE) {
        WriteFile(h, log_buf, (unsigned)log_len, &written, NULL);
        CloseHandle(h);
    }
    MessageBoxA(NULL, "Query finished.\n\nResult in D:\\GLIDEHW.TXT",
                "3dfx hardware", 0x40);
    ExitProcess((unsigned)code);
}

static const char *type_name(int t)
{
    switch (t) {
    case 0: return "Voodoo Graphics (Voodoo 1)";
    case 1: return "SST96 / Voodoo Rush";
    case 2: return "AT3D";
    case 3: return "Voodoo2";
    default: return "unknown";
    }
}

void start(void)
{
    void *dll;
    pfn_grGlideInit        grGlideInit;
    pfn_grSstQueryHardware grSstQueryHardware;
    pfn_grGlideShutdown    grGlideShutdown;
    pfn_grGlideGetVersion  grGlideGetVersion;
    GrHwConfiguration hw;
    char version[128];
    int i, s;

    for (i = 0; i < (int)sizeof(hw); i++) ((char *)&hw)[i] = 0;

    dll = LoadLibraryA("GLIDE2X.DLL");
    if (!dll) { logs("FAILED: GLIDE2X.DLL not found\r\n"); finish(1); }

    grGlideInit        = (pfn_grGlideInit)       GetProcAddress(dll, "_grGlideInit@0");
    grSstQueryHardware = (pfn_grSstQueryHardware)GetProcAddress(dll, "_grSstQueryHardware@4");
    grGlideShutdown    = (pfn_grGlideShutdown)   GetProcAddress(dll, "_grGlideShutdown@0");
    grGlideGetVersion  = (pfn_grGlideGetVersion) GetProcAddress(dll, "_grGlideGetVersion@4");

    if (!grGlideInit || !grSstQueryHardware) {
        logs("FAILED: Glide exports not found\r\n"); finish(2);
    }

    logs("3dfx hardware as seen by Glide 2.x\r\n");
    logs("==================================\r\n");

    if (grGlideGetVersion) {
        for (i = 0; i < (int)sizeof(version); i++) version[i] = 0;
        grGlideGetVersion(version);
        logs("Glide version : "); logs(version); logs("\r\n");
    } else {
        logs("Glide version : grGlideGetVersion absent from this build\r\n");
    }

    grGlideInit();
    if (!grSstQueryHardware(&hw)) {
        logs("FAILED: grSstQueryHardware - no hardware detected\r\n");
        finish(3);
    }

    logs("boards detected : "); lognum(hw.num_sst); logs("\r\n\r\n");

    for (s = 0; s < hw.num_sst && s < MAX_NUM_SST; s++) {
        GrVoodooConfig_t *v = &hw.SSTs[s].sstBoard.VoodooConfig;
        logs("board "); lognum(s); logs("\r\n");
        logs("  type          : "); lognum(hw.SSTs[s].type);
        logs(" ("); logs(type_name(hw.SSTs[s].type)); logs(")\r\n");
        logs("  frame buffer  : "); lognum(v->fbRam);     logs(" MB\r\n");
        logs("  FBI revision  : "); lognum(v->fbiRev);    logs("\r\n");
        logs("  TMUs          : "); lognum(v->nTexelfx);  logs("\r\n");
        logs("  SLI detected  : "); lognum(v->sliDetect); logs("\r\n");
        for (i = 0; i < v->nTexelfx && i < GLIDE_NUM_TMU; i++) {
            logs("  TMU "); lognum(i); logs(" memory   : ");
            lognum(v->tmuConfig[i].tmuRam); logs(" MB\r\n");
        }
        logs("\r\n");
    }

    /* Raw dump: enough to re-read the structure if the assumed layout did not
       match this driver's. */
    logs("raw structure (first 16 integers)\r\n ");
    for (i = 0; i < 16; i++) {
        loghex(((unsigned *)&hw)[i]);
        logs((i % 4 == 3) ? "\r\n " : " ");
    }
    logs("\r\n");

    if (grGlideShutdown) grGlideShutdown();
    FreeLibrary(dll);
    finish(0);
}
