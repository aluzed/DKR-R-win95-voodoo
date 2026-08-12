/* E00-S05 — que dit la carte d'elle-meme ?
 *
 * E09-S01 a montre que le fichier de configuration de 86Box ment : des reglages
 * Voodoo ecrits a la main y ont ete ignores en silence, et la machine a emule
 * une Voodoo 1 a 2 Mo alors que le fichier annoncait une Voodoo 2 a 4 Mo. Tout
 * budget de texture etabli sur le fichier serait donc faux.
 *
 * Ce banc pose la question a Glide : `grSstQueryHardware` remplit une
 * GrHwConfiguration dont on extrait le type de carte, la memoire d'image, le
 * nombre de TMU et la memoire de chacune. C'est la meme information que le
 * moteur consultera a l'execution (E05-S01), obtenue par le meme chemin.
 *
 * La structure est aussi vidangee en brut : si la disposition supposee ne
 * correspondait pas a celle de ce pilote, les entiers restent lisibles et
 * l'erreur se voit au lieu de se cacher.
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

/* Disposition de Glide 2.x, telle que la declare `glide.h` de 3dfx. */
#define MAX_NUM_SST   4
#define GLIDE_NUM_TMU 3

typedef struct { int tmuRev; int tmuRam; } GrTMUConfig_t;

typedef struct {
    int fbRam;                 /* Mo de memoire d'image */
    int fbiRev;
    int nTexelfx;              /* nombre de TMU */
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
    MessageBoxA(NULL, "Interrogation terminee.\n\nResultat dans D:\\GLIDEHW.TXT",
                "Materiel 3dfx", 0x40);
    ExitProcess((unsigned)code);
}

static const char *type_name(int t)
{
    switch (t) {
    case 0: return "Voodoo Graphics (Voodoo 1)";
    case 1: return "SST96 / Voodoo Rush";
    case 2: return "AT3D";
    case 3: return "Voodoo2";
    default: return "inconnu";
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
    if (!dll) { logs("ECHEC: GLIDE2X.DLL introuvable\r\n"); finish(1); }

    grGlideInit        = (pfn_grGlideInit)       GetProcAddress(dll, "_grGlideInit@0");
    grSstQueryHardware = (pfn_grSstQueryHardware)GetProcAddress(dll, "_grSstQueryHardware@4");
    grGlideShutdown    = (pfn_grGlideShutdown)   GetProcAddress(dll, "_grGlideShutdown@0");
    grGlideGetVersion  = (pfn_grGlideGetVersion) GetProcAddress(dll, "_grGlideGetVersion@4");

    if (!grGlideInit || !grSstQueryHardware) {
        logs("ECHEC: exports Glide introuvables\r\n"); finish(2);
    }

    logs("Materiel 3dfx vu par Glide 2.x\r\n");
    logs("==============================\r\n");

    if (grGlideGetVersion) {
        for (i = 0; i < (int)sizeof(version); i++) version[i] = 0;
        grGlideGetVersion(version);
        logs("version Glide : "); logs(version); logs("\r\n");
    } else {
        logs("version Glide : grGlideGetVersion absente de cet arbre\r\n");
    }

    grGlideInit();
    if (!grSstQueryHardware(&hw)) {
        logs("ECHEC: grSstQueryHardware — aucun materiel detecte\r\n");
        finish(3);
    }

    logs("cartes detectees : "); lognum(hw.num_sst); logs("\r\n\r\n");

    for (s = 0; s < hw.num_sst && s < MAX_NUM_SST; s++) {
        GrVoodooConfig_t *v = &hw.SSTs[s].sstBoard.VoodooConfig;
        logs("carte "); lognum(s); logs("\r\n");
        logs("  type          : "); lognum(hw.SSTs[s].type);
        logs(" ("); logs(type_name(hw.SSTs[s].type)); logs(")\r\n");
        logs("  memoire image : "); lognum(v->fbRam);     logs(" Mo\r\n");
        logs("  revision FBI  : "); lognum(v->fbiRev);    logs("\r\n");
        logs("  TMU           : "); lognum(v->nTexelfx);  logs("\r\n");
        logs("  SLI detecte   : "); lognum(v->sliDetect); logs("\r\n");
        for (i = 0; i < v->nTexelfx && i < GLIDE_NUM_TMU; i++) {
            logs("  TMU "); lognum(i); logs(" memoire  : ");
            lognum(v->tmuConfig[i].tmuRam); logs(" Mo\r\n");
        }
        logs("\r\n");
    }

    /* Vidange brute : de quoi relire la structure si la disposition supposee
       ne correspondait pas a ce pilote. */
    logs("structure brute (16 premiers entiers)\r\n ");
    for (i = 0; i < 16; i++) {
        loghex(((unsigned *)&hw)[i]);
        logs((i % 4 == 3) ? "\r\n " : " ");
    }
    logs("\r\n");

    if (grGlideShutdown) grGlideShutdown();
    FreeLibrary(dll);
    finish(0);
}
