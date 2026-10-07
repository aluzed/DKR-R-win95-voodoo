/* SETUP.EXE - installs Diddy Kong Racing for Windows 95 from the folder it runs
 * in (E09-S05).
 *
 * A fresh Windows 95 cannot open a ZIP, so the package is a folder -- copied to
 * the machine, or burnt on a CD -- with this program in it. It asks where to
 * install (C:\GAMES\DKR by default), copies the game and its documents, takes
 * the player's ROM from beside it or asks for it, puts a shortcut in Start >
 * Programs and offers to start the game. An existing installation is updated in
 * place: DKRR.INI and the saves, which the game writes beside itself, are not
 * touched.
 *
 * Message boxes and the system's own dialogs only: every call is an ANSI one
 * that Windows 95 implements (checked by `check_imports.py` at the build, like
 * the game). No game asset is in the package; the ROM is the player's own.
 */
#define COBJMACROS
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>
#include <string.h>

static const char kTitle[] = "Diddy Kong Racing - Setup";
static const char kDefaultTarget[] = "C:\\GAMES\\DKR";

/* The game and its documents, as the package script stages them. */
static const char *const kFiles[] = {
    "DKRR.EXE", "README.TXT", "CONFIG.TXT", "LICENSE.TXT", "COPYING.TXT",
    "THIRDPTY.TXT"
};
static const char *const kRomPatterns[] = { "*.Z64", "*.N64", "*.V64" };

static int ask(const char *text, UINT flags)
{
    return MessageBoxA(NULL, text, kTitle, flags | MB_SETFOREGROUND);
}

/* `path` without its last component, in place. */
static void strip_last(char *path)
{
    char *slash = strrchr(path, '\\');
    if (slash) { *slash = '\0'; }
}

static void join(char *out, const char *dir, const char *name)
{
    lstrcpyA(out, dir);
    if (out[0] != '\0' && out[lstrlenA(out) - 1] != '\\') { lstrcatA(out, "\\"); }
    lstrcatA(out, name);
}

/* Creates every missing folder of `path`. */
static int make_dirs(const char *path)
{
    char partial[MAX_PATH];
    int i, n = lstrlenA(path);
    if (n >= MAX_PATH) { return 0; }
    for (i = 0; i <= n; i++) {
        partial[i] = path[i];
        if ((path[i] == '\\' || path[i] == '\0') && i > 2) {
            partial[i] = '\0';
            if (GetFileAttributesA(partial) == 0xFFFFFFFFu &&
                !CreateDirectoryA(partial, NULL)) {
                return 0;
            }
            partial[i] = path[i];
        }
    }
    return 1;
}

/* The first ROM file in `dir`, written to `name`; 0 if none. */
static int find_rom(const char *dir, char *name)
{
    int p;
    for (p = 0; p < 3; p++) {
        char pattern[MAX_PATH];
        WIN32_FIND_DATAA found;
        HANDLE h;
        join(pattern, dir, kRomPatterns[p]);
        h = FindFirstFileA(pattern, &found);
        if (h != INVALID_HANDLE_VALUE) {
            lstrcpyA(name, found.cFileName);
            FindClose(h);
            return 1;
        }
    }
    return 0;
}

static int choose_folder(char *target)
{
    BROWSEINFOA info;
    char display[MAX_PATH];
    LPITEMIDLIST list;
    int ok = 0;
    memset(&info, 0, sizeof(info));
    info.pszDisplayName = display;
    info.lpszTitle = "Choose the folder Diddy Kong Racing goes into. "
                     "A folder named DKR is made inside it.";
    info.ulFlags = BIF_RETURNONLYFSDIRS;
    list = SHBrowseForFolderA(&info);
    if (list) {
        char chosen[MAX_PATH];
        if (SHGetPathFromIDListA(list, chosen)) {
            join(target, chosen, "DKR");
            ok = 1;
        }
        CoTaskMemFree(list);
    }
    return ok;
}

/* Asks for the ROM with the system's Open dialog; its full path to `rom`. */
static int choose_rom(char *rom)
{
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    /* Windows 95's dialog refuses the larger structure later systems added:
       the 4.0 size is what it knows. */
#ifdef OPENFILENAME_SIZE_VERSION_400A
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
#else
    ofn.lStructSize = sizeof(ofn);
#endif
    ofn.lpstrFilter = "N64 ROM (*.z64, *.n64, *.v64)\0*.z64;*.n64;*.v64\0"
                      "All files (*.*)\0*.*\0";
    rom[0] = '\0';
    ofn.lpstrFile = rom;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Your Diddy Kong Racing ROM (USA, version 1.0)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameA(&ofn) != 0;
}

/* Start > Programs > Diddy Kong Racing. */
static int make_shortcut(const char *target)
{
    IShellLinkA *link = NULL;
    IPersistFile *file = NULL;
    LPITEMIDLIST list = NULL;
    char programs[MAX_PATH], exe[MAX_PATH], lnk[MAX_PATH];
    WCHAR wide[MAX_PATH];
    int ok = 0;
    if (SHGetSpecialFolderLocation(NULL, CSIDL_PROGRAMS, &list) != S_OK) { return 0; }
    if (!SHGetPathFromIDListA(list, programs)) { CoTaskMemFree(list); return 0; }
    CoTaskMemFree(list);
    join(lnk, programs, "Diddy Kong Racing.lnk");
    join(exe, target, "DKRR.EXE");
    if (CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                         &IID_IShellLinkA, (void **)&link) != S_OK) {
        return 0;
    }
    IShellLinkA_SetPath(link, exe);
    IShellLinkA_SetWorkingDirectory(link, target);
    IShellLinkA_SetDescription(link, "Diddy Kong Racing for Windows 95 and 3dfx Voodoo");
    if (IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&file) == S_OK) {
        MultiByteToWideChar(CP_ACP, 0, lnk, -1, wide, MAX_PATH);
        ok = IPersistFile_Save(file, wide, TRUE) == S_OK;
        IPersistFile_Release(file);
    }
    IShellLinkA_Release(link);
    return ok;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    char source[MAX_PATH], target[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    char rom[MAX_PATH], text[1024];
    int answer;
    size_t i;
    HMODULE glide;
    (void)instance; (void)previous; (void)command; (void)show;

    GetModuleFileNameA(NULL, source, MAX_PATH);
    strip_last(source);
    join(from, source, "DKRR.EXE");
    if (GetFileAttributesA(from) == 0xFFFFFFFFu) {
        ask("DKRR.EXE is not beside SETUP.EXE. Run SETUP.EXE from the folder "
            "or the CD it came in.", MB_OK | MB_ICONSTOP);
        return 1;
    }

    lstrcpyA(target, kDefaultTarget);
    /* Worded so that it reads right whatever language Windows gives the
       buttons: a French system shows Oui, Non and Annuler. */
    wsprintfA(text, "Install Diddy Kong Racing for Windows 95 and 3dfx Voodoo "
              "into\n\n    %s ?\n\nAnswering no lets you choose another "
              "folder; cancelling leaves without installing.", target);
    answer = ask(text, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) { return 0; }
    CoInitialize(NULL);
    if (answer == IDNO && !choose_folder(target)) { CoUninitialize(); return 0; }

    /* The game needs the 3dfx driver's GLIDE2X.DLL, which is not ours to ship. */
    glide = LoadLibraryA("glide2x.dll");
    if (glide) {
        FreeLibrary(glide);
    } else if (ask("GLIDE2X.DLL was not found. The game needs your 3dfx card's "
                   "Windows 95 driver, which installs it in WINDOWS\\SYSTEM.\n\n"
                   "Install the game anyway?", MB_YESNO | MB_ICONWARNING) != IDYES) {
        CoUninitialize();
        return 0;
    }

    if (!make_dirs(target)) {
        wsprintfA(text, "The folder %s could not be created.", target);
        ask(text, MB_OK | MB_ICONSTOP);
        CoUninitialize();
        return 1;
    }
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    for (i = 0; i < sizeof(kFiles) / sizeof(kFiles[0]); i++) {
        join(from, source, kFiles[i]);
        join(to, target, kFiles[i]);
        if (GetFileAttributesA(from) == 0xFFFFFFFFu && i > 0) { continue; }
        SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);   /* read-only off a CD */
        if (!CopyFileA(from, to, FALSE)) {
            wsprintfA(text, "%s could not be copied to %s.", kFiles[i], target);
            ask(text, MB_OK | MB_ICONSTOP);
            CoUninitialize();
            return 1;
        }
        SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
    }

    /* The ROM: one beside SETUP.EXE, or one already installed, or asked for. */
    {
        char name[MAX_PATH], existing[MAX_PATH];
        if (find_rom(source, name)) {
            join(from, source, name);
            join(to, target, name);
            if (CopyFileA(from, to, FALSE)) {
                SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
            } else {
                wsprintfA(text, "%s could not be copied; copy it into %s yourself.",
                          name, target);
                ask(text, MB_OK | MB_ICONWARNING);
            }
        } else if (!find_rom(target, existing)) {
            if (ask("The game needs your own Diddy Kong Racing ROM (USA, version "
                    "1.0, a .z64, .n64 or .v64 file). Show where it is now?\n\n"
                    "If not, copy it into the game's folder later; the game says "
                    "so when it starts without one.", MB_YESNO | MB_ICONQUESTION) == IDYES &&
                choose_rom(rom)) {
                const char *base = strrchr(rom, '\\');
                join(to, target, base ? base + 1 : rom);
                if (CopyFileA(rom, to, FALSE)) {
                    SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
                } else {
                    ask("The ROM could not be copied; copy it into the game's "
                        "folder yourself.", MB_OK | MB_ICONWARNING);
                }
            }
        }
    }

    {
        const int shortcut = make_shortcut(target);
        CoUninitialize();
        wsprintfA(text, "Diddy Kong Racing is installed in\n\n    %s\n\n%s"
                  "README.TXT there says how to play and how to quit.\n\n"
                  "Start the game now?", target,
                  shortcut ? "It is in Start > Programs.\n\n"
                           : "The Start menu shortcut could not be made; "
                             "start DKRR.EXE from that folder.\n\n");
    }
    if (ask(text, MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        join(from, target, "DKRR.EXE");
        memset(&startup, 0, sizeof(startup));
        startup.cb = sizeof(startup);
        if (CreateProcessA(from, NULL, NULL, NULL, FALSE, 0, NULL, target,
                           &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
    return 0;
}
