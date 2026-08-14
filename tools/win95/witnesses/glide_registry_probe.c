/* Peut-on savoir qu'il n'y a pas de Voodoo **avant** de charger `glide2x.dll` ?
 *
 * Sur une machine sans carte 3dfx, `LoadLibraryA("glide2x.dll")` déclenche
 * l'initialisation de la DLL, qui affiche **sa propre boîte modale** :
 *
 *     _GlideInitEnvironment: glide2x.dll expected Voodoo, none detected
 *
 * Le programme ne reprend la main qu'après le clic, et le message est en anglais,
 * signé d'une bibliothèque dont le joueur n'a jamais entendu parler. Le chemin
 * d'erreur propre de `dkr_glide_detect` — qui nomme le geste possible — n'est
 * jamais atteint : la boîte le précède.
 *
 * Mesuré le 14 août 2026 en retirant la carte de la configuration de l'émulateur.
 * Le témoin y a aussi bloqué l'arrêt de la machine, le dialogue volant le focus,
 * ce qui a laissé le volume de transfert marqué sale — un rappel que ce genre de
 * boîte ne gêne pas que le joueur.
 *
 * Ce témoin cherche donc un signal antérieur au chargement. Il n'invente rien :
 * il relève ce que le registre contient, avec et sans carte, et laisse la
 * comparaison décider. Un signal qui existerait dans les deux cas ne vaudrait
 * rien, et c'est précisément ce qu'on veut savoir avant d'écrire du code qui s'y
 * fie.
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

/* Windows 95 range le matériel réellement énuméré sous `HKLM\Enum`. Un
   périphérique retiré peut y laisser une trace, et c'est tout l'enjeu : on
   relève, on ne conclut pas. */
static void dump_key_children(HKEY root, const char *path, const char *why)
{
    HKEY  k;
    DWORD i = 0;
    char  name[256];
    DWORD len;
    int   found = 0;

    say("\n[%s]  %s\n", path, why);
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        say("  (clef absente)\n");
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
    if (!found) { say("  (aucune sous-clef)\n"); }
    RegCloseKey(k);
}

/* Pour une instance de périphérique, ce qui distingue « présent » de « connu
   mais absent » : `ConfigFlags` porte le bit 0x20 (CONFIGFLAG_REMOVED) et le
   gestionnaire y range aussi les problèmes. */
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
                    say("    instance %s : ConfigFlags=0x%08X%s  desc=\"%s\"\n",
                        inst, f, (f & 0x20u) ? " (RETIRE)" : "", desc);
                } else {
                    say("    instance %s : pas de ConfigFlags  desc=\"%s\"\n",
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
    say("ce que le registre sait d'une carte 3dfx, avant tout LoadLibrary\n");

    /* 121A est l'identifiant PCI de 3dfx Interactive ; 0001 est la Voodoo
       Graphics, 0002 la Voodoo 2. On énumère plutôt que de deviner la clef
       exacte, dont la forme dépend du sous-système. */
    dump_key_children(HKEY_LOCAL_MACHINE, "Enum\\PCI",
                      "peripheriques PCI enumeres par Windows");

    {
        HKEY  k;
        DWORD i = 0, len;
        char  name[256];
        say("\n-- ceux de 3dfx (VEN_121A) --\n");
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
            if (!any) { say("  aucun\n"); }
            RegCloseKey(k);
        }
    }

    /* Le pilote laisse aussi une trace logicielle. Elle survit au retrait de la
       carte — c'est justement l'hypothèse à réfuter. */
    dump_key_children(HKEY_LOCAL_MACHINE, "Software\\3Dfx Interactive",
                      "trace du pilote (survit-elle au retrait ?)");
    dump_key_children(HKEY_LOCAL_MACHINE, "Software\\3dfx Interactive",
                      "idem, autre casse");

    /* Et le fichier lui-même : présent ne veut pas dire utilisable. */
    {
        const DWORD a = GetFileAttributesA("glide2x.dll");
        char sysdir[MAX_PATH];
        char full[MAX_PATH + 32];
        say("\n-- la bibliotheque --\n");
        say("  glide2x.dll dans le chemin courant : %s\n",
            (a == 0xFFFFFFFFu) ? "non" : "oui");
        if (GetSystemDirectoryA(sysdir, sizeof(sysdir))) {
            sprintf(full, "%s\\glide2x.dll", sysdir);
            say("  %s : %s\n", full,
                (GetFileAttributesA(full) == 0xFFFFFFFFu) ? "absent" : "present");
        }
    }

    say("\nfin\n");
    if (g_out) { fclose(g_out); }
    return 0;
}
