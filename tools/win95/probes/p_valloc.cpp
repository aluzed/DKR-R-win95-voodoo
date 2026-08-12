/* E00-S01 / E00-S06 — que peut reellement reserver et valider Windows 95 ?
 *
 * librecomp reserve `allocation_size` (4 Gio) puis valide `mem_size` (512 Mio).
 * Aucune des deux valeurs n'est atteignable sur la cible ; ce banc mesure ce
 * qui l'est, pour donner un plancher chiffre a l'ADR du budget memoire.
 */
#include <windows.h>
#include <stdio.h>

static SIZE_T probe(DWORD flags, DWORD prot)
{
    for (SIZE_T mb = 2048; mb >= 1; mb /= 2) {
        SIZE_T sz = mb * 1024u * 1024u;
        void *p = VirtualAlloc(NULL, sz, flags, prot);
        if (p) { VirtualFree(p, 0, MEM_RELEASE); return mb; }
    }
    return 0;
}

int main(void)
{
    char buf[1024]; int n = 0;
    MEMORYSTATUS ms; ms.dwLength = sizeof(ms); GlobalMemoryStatus(&ms);
    SYSTEM_INFO si; GetSystemInfo(&si);

    n += sprintf(buf+n, "sizeof(size_t)      = %d\r\n", (int)sizeof(size_t));
    n += sprintf(buf+n, "allocation_size 32b = %lu\r\n",
                 (unsigned long)(size_t)(4096ULL*1024ULL*1024ULL));
    n += sprintf(buf+n, "mem_size 32b        = %lu\r\n",
                 (unsigned long)(size_t)(512ULL*1024ULL*1024ULL));
    n += sprintf(buf+n, "RAM physique        = %lu Mio\r\n",
                 (unsigned long)(ms.dwTotalPhys >> 20));
    n += sprintf(buf+n, "RAM dispo           = %lu Mio\r\n",
                 (unsigned long)(ms.dwAvailPhys >> 20));
    n += sprintf(buf+n, "espace virtuel total= %lu Mio\r\n",
                 (unsigned long)(ms.dwTotalVirtual >> 20));
    n += sprintf(buf+n, "granularite         = %lu\r\n",
                 (unsigned long)si.dwAllocationGranularity);
    n += sprintf(buf+n, "MAX reserve         = %lu Mio\r\n",
                 (unsigned long)probe(MEM_RESERVE, PAGE_NOACCESS));
    n += sprintf(buf+n, "MAX commit RW       = %lu Mio\r\n",
                 (unsigned long)probe(MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));

    /* Le schema exact de librecomp : reserver puis valider une fenetre. */
    void *p = VirtualAlloc(NULL, 8u*1024u*1024u, MEM_RESERVE, PAGE_NOACCESS);
    DWORD old = 0;
    int ok = 0;
    if (p) { ok = VirtualAlloc(p, 8u*1024u*1024u, MEM_COMMIT, PAGE_READWRITE) != NULL
                  && VirtualProtect(p, 8u*1024u*1024u, PAGE_READWRITE, &old) != 0;
             VirtualFree(p, 0, MEM_RELEASE); }
    n += sprintf(buf+n, "schema librecomp 8Mio = %s\r\n", ok ? "OK" : "ECHEC");

    fputs(buf, stdout);
    { FILE *f = fopen("D:\\VALLOC.TXT", "wb"); if (f) { fwrite(buf,1,n,f); fclose(f);} }
    return 0;
}
