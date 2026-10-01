/* E06-S05 - implementation. See ini_settings.h. */
#include "ini_settings.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_ini_path[MAX_PATH];
static int  g_applied = 0;
static int  g_overridden = 0;
/* Whether the file existed when the game started, taken before any profile
   call: Windows 95 keeps INI files in a cache of its own, and reading a missing
   one there left it existing for `GetFileAttributesA` afterwards -- the
   template was never written, and the cache was flushed over it with only the
   ROM line in it (1 October 2026). */
static int  g_existed = -1;

const char *dkr_ini_path(void)
{
    if (g_ini_path[0] == '\0') {
        char exe[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
        char *slash;
        if (n == 0 || n >= sizeof(exe)) { strcpy(exe, ".\\DKRR.EXE"); }
        else { exe[n] = '\0'; }
        slash = strrchr(exe, '\\');
        if (slash) { slash[1] = '\0'; } else { strcpy(exe, ".\\"); }
        _snprintf(g_ini_path, sizeof(g_ini_path) - 1, "%sDKRR.INI", exe);
    }
    return g_ini_path;
}

int dkr_ini_settings_applied(void)    { return g_applied; }
int dkr_ini_settings_overridden(void) { return g_overridden; }

/* Priority 101: before every C++ static initialiser, several of which read
   their `DKR_*` variable once and keep the answer. `_putenv`, not
   `SetEnvironmentVariableA`: `getenv` reads the C runtime's own copy, made
   when it started, and would not see the process environment change. */
__attribute__((constructor(101)))
static void dkr_ini_settings_load(void)
{
    char section[8192];
    const char *entry;
    DWORD n;
    g_existed = (GetFileAttributesA(dkr_ini_path()) != 0xFFFFFFFFu) ? 1 : 0;
    if (!g_existed) { return; }
    n = GetPrivateProfileSectionA("Settings", section, sizeof(section), dkr_ini_path());
    if (n == 0) { return; }
    for (entry = section; *entry != '\0'; entry += strlen(entry) + 1) {
        char name[128], line[512];
        const char *equals = strchr(entry, '=');
        size_t key_length;
        if (entry[0] == ';' || equals == NULL || equals == entry) { continue; }
        key_length = (size_t)(equals - entry);
        while (key_length > 0 && (entry[key_length - 1] == ' ' || entry[key_length - 1] == '\t')) {
            key_length--;
        }
        if (key_length == 0 || key_length + 5 > sizeof(name)) { continue; }
        memcpy(name, "DKR_", 4);
        memcpy(name + 4, entry, key_length);
        name[4 + key_length] = '\0';
        if (getenv(name) != NULL) { g_overridden++; continue; }
        {
            const char *value = equals + 1;
            while (*value == ' ' || *value == '\t') { value++; }
            _snprintf(line, sizeof(line) - 1, "%s=%s", name, value);
            line[sizeof(line) - 1] = '\0';
            if (_putenv(line) == 0) { g_applied++; }
        }
    }
}

int dkr_ini_write_template_if_absent(void)
{
    FILE *f;
    if (g_existed < 0) {
        g_existed = (GetFileAttributesA(dkr_ini_path()) != 0xFFFFFFFFu) ? 1 : 0;
    }
    if (g_existed) { return 0; }
    /* All NULL: Windows 95 writes back and drops its cached copy, so that the
       next profile call reads the file written here. */
    WritePrivateProfileStringA(NULL, NULL, NULL, dkr_ini_path());
    f = fopen(dkr_ini_path(), "w");
    if (!f) { return 0; }
    fputs(
"; DKRR.INI - settings of Diddy Kong Racing for Windows 95 and 3dfx Voodoo.\n"
"; Read when the game starts. A line beginning with ';' is a comment.\n"
"; Every setting is described in docs/CONFIGURATION.md.\n"
"\n"
"[Paths]\n"
"; The ROM. Filled in by the game the first time it finds one beside DKRR.EXE.\n"
"; Rom=C:\\GAMES\\DKR\\DKR.Z64\n"
"\n"
"[Settings]\n"
"; Each line NAME=VALUE sets the option NAME. An option that is a switch is\n"
"; on when its line is present: put a ';' in front of the line to turn it off.\n"
"\n"
"; Copy the game's memory for every frame drawn, as the original port did.\n"
"; Slower: about 51 ms a frame in a race against 35 ms without.\n"
"; RDRAM_SNAPSHOT=copy\n"
"\n"
"; Present frames immediately instead of at the monitor's retrace: may tear.\n"
"; GLIDE_SWAP=immediate\n"
"\n"
"; Frame rate, render time and audio counters drawn over the game.\n"
"; OSD=1\n"
"\n"
"; --- Diagnostics: for reporting a problem, not for playing ----------------\n"
"\n"
"; The renderer's per-triangle statistics in the log (costs up to 4 ms a frame).\n"
"; GFX_STATS=1\n"
"\n"
"; Write FRAMES.BIN and AUDIO.BIN, one record per frame and per audio task,\n"
"; into this folder (keep the final backslash).\n"
"; TIMING_EXPORT=C:\\DKR\\\n",
        f);
    fclose(f);
    WritePrivateProfileStringA(NULL, NULL, NULL, dkr_ini_path());
    g_existed = 1;
    return 1;
}
