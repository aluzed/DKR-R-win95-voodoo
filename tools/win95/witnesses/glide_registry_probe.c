/* Can one know there is no Voodoo **before** loading `glide2x.dll`?
 *
 * On a machine without a 3dfx board, `LoadLibraryA("glide2x.dll")` triggers the
 * DLL's initialisation, which displays **its own modal box**:
 *
 *     _GlideInitEnvironment: glide2x.dll expected Voodoo, none detected
 *
 * The program only regains control after the click, and the message is signed by
 * a library the player has never heard of. `dkr_glide_detect`'s clean error path
 * - which names the action available - is never reached: the box precedes it.
 *
 * Measured on 14 August 2026 by removing the board from the emulator's
 * configuration. The witness also blocked the machine's shutdown there, the
 * dialog stealing the focus, which left the transfer volume marked dirty - a
 * reminder that this kind of box does not inconvenience only the player.
 *
 * This witness therefore looks for a signal available before the load. It invents
 * nothing: it reports what the registry contains, with and without a board, and
 * lets the comparison decide. A signal that existed in both cases would be worth
 * nothing, and that is precisely what we want to know before writing code that
 * relies on it.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE *g_out;

static void say(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

/* Windows 95 stores the hardware it actually enumerated under `HKLM\Enum`. A
   removed device may leave a trace there, and that is the whole point: we report,
   we do not conclude. */
static void dump_key_children(HKEY root, const char *path, const char *why)
{
    HKEY  k;
    DWORD i = 0;
    char  name[256];
    DWORD len;
    int   found = 0;

    say("\n[%s]  %s\n", path, why);
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        say("  (key absent)\n");
        return;
    }
    for (;;) {
        len = sizeof(name);
        if (RegEnumKeyExA(k, i, name, &len, 0, 0, 0, 0) != ERROR_SUCCESS) {
            break;
        }
        say("  %s\n", name);
        found++;
        i++;
    }
    if (!found) { say("  (no subkey)\n"); }
    RegCloseKey(k);
}

/* For a device instance, what tells "present" from "known but absent":
   `ConfigFlags` carries bit 0x20 (CONFIGFLAG_REMOVED), and the device manager also
   stores its problems there. */
static void dump_instance(const char *pci_sub)
{
    char path[512];
    HKEY k;
    DWORD i = 0, len;
    char inst[256];

    sprintf(path, "Enum\\PCI\\%s", pci_sub);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    for (;;) {
        len = sizeof(inst);
        if (RegEnumKeyExA(k, i, inst, &len, 0, 0, 0, 0) != ERROR_SUCCESS) { break; }
        i++;
        {
            char ipath[768];
            HKEY ik;
            sprintf(ipath, "%s\\%s", path, inst);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ipath, 0, KEY_READ, &ik)
                == ERROR_SUCCESS) {
                DWORD type = 0, sz;
                unsigned char buf[256];
                char desc[256];

                sz = sizeof(desc);
                desc[0] = 0;
                RegQueryValueExA(ik, "DeviceDesc", 0, &type,
                                 (unsigned char *)desc, &sz);
                sz = sizeof(buf);
                memset(buf, 0, sizeof(buf));
                if (RegQueryValueExA(ik, "ConfigFlags", 0, &type, buf, &sz)
                    == ERROR_SUCCESS && sz >= 4) {
                    const unsigned f = (unsigned)buf[0] | ((unsigned)buf[1] << 8) |
                                       ((unsigned)buf[2] << 16) | ((unsigned)buf[3] << 24);
                    say("    instance %s: ConfigFlags=0x%08X%s  desc=\"%s\"\n",
                        inst, f, (f & 0x20u) ? " (REMOVED)" : "", desc);
                } else {
                    say("    instance %s: no ConfigFlags  desc=\"%s\"\n",
                        inst, desc);
                }
                RegCloseKey(ik);
            }
        }
    }
    RegCloseKey(k);
}

int main(void)
{
    g_out = fopen("D:\\GLREG.TXT", "w");
    say("what the registry knows of a 3dfx board, before any LoadLibrary\n");

    /* 121A is 3dfx Interactive's PCI identifier; 0001 is the Voodoo Graphics, 0002
       the Voodoo 2. We enumerate rather than guess the exact key, whose shape
       depends on the subsystem. */
    dump_key_children(HKEY_LOCAL_MACHINE, "Enum\\PCI",
                      "PCI devices enumerated by Windows");

    {
        HKEY  k;
        DWORD i = 0, len;
        char  name[256];
        say("\n-- those from 3dfx (VEN_121A) --\n");
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ, &k)
            == ERROR_SUCCESS) {
            int any = 0;
            for (;;) {
                len = sizeof(name);
                if (RegEnumKeyExA(k, i, name, &len, 0, 0, 0, 0) != ERROR_SUCCESS) {
                    break;
                }
                i++;
                if (strstr(name, "121A") || strstr(name, "121a")) {
                    say("  %s\n", name);
                    dump_instance(name);
                    any = 1;
                }
            }
            if (!any) { say("  none\n"); }
            RegCloseKey(k);
        }
    }

    /* The driver also leaves a software trace. It survives the board's removal -
       that is exactly the hypothesis to refute. */
    dump_key_children(HKEY_LOCAL_MACHINE, "Software\\3Dfx Interactive",
                      "the driver's trace (does it survive removal?)");
    dump_key_children(HKEY_LOCAL_MACHINE, "Software\\3dfx Interactive",
                      "the same, other case");

    /* And the file itself: present does not mean usable. */
    {
        const DWORD a = GetFileAttributesA("glide2x.dll");
        char sysdir[MAX_PATH];
        char full[MAX_PATH + 32];
        say("\n-- the library --\n");
        say("  glide2x.dll in the current path: %s\n",
            (a == 0xFFFFFFFFu) ? "no" : "yes");
        if (GetSystemDirectoryA(sysdir, sizeof(sysdir))) {
            sprintf(full, "%s\\glide2x.dll", sysdir);
            say("  %s: %s\n", full,
                (GetFileAttributesA(full) == 0xFFFFFFFFu) ? "absent" : "present");
        }
    }

    say("\nend\n");
    if (g_out) { fclose(g_out); }
    return 0;
}
