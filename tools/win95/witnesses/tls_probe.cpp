/* E02-S02 — `thread_local` fonctionne-t-il sous Windows 95 ?
 *
 * GCC implemente `thread_local` par une section .tls et le repertoire TLS du
 * PE. Windows 9x est repute ne pas traiter ce repertoire ; s'il ne le traite
 * pas, tous les fils partagent la meme case, et les trois variables locales au
 * fil de `ultramodern/src/threads.cpp` — dont `thread_self` — se marchent
 * dessus. Ce serait un defaut silencieux et catastrophique.
 *
 * On ne le suppose pas : deux fils ecrivent chacun leur valeur, se synchronisent
 * pour garantir l'entrelacement, puis relisent la leur.
 */
#include <windows.h>
#include <stdio.h>

thread_local int         tl_value = 0;
thread_local const char *tl_name  = "aucun";

static HANDLE ecrit, relit;
static int    vu_par_le_fil = -1;
static const char *nom_vu   = "?";

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    tl_value = 222;                 /* le fil pose SA valeur */
    tl_name  = "fil";
    SetEvent(ecrit);                /* le principal peut ecrire la sienne */
    WaitForSingleObject(relit, 5000);
    vu_par_le_fil = tl_value;       /* doit valoir 222, pas 111 */
    nom_vu        = tl_name;
    return 0;
}

int main(void)
{
    HANDLE th; DWORD tid; FILE *f;
    int principal_ok, fil_ok;

    ecrit = CreateEventA(NULL, TRUE, FALSE, NULL);
    relit = CreateEventA(NULL, TRUE, FALSE, NULL);

    tl_value = 111;
    tl_name  = "principal";

    th = CreateThread(NULL, 0, worker, NULL, 0, &tid);
    WaitForSingleObject(ecrit, 5000);   /* le fil a ecrit 222 */

    /* Si le TLS n'est pas isole, notre 111 a ete ecrase par le 222 du fil. */
    principal_ok = (tl_value == 111);
    SetEvent(relit);
    WaitForSingleObject(th, 5000);
    fil_ok = (vu_par_le_fil == 222);

    f = fopen("D:\\TLSPROBE.TXT", "w");
    if (f) {
        fprintf(f, "thread_local sous Windows 95\n");
        fprintf(f, "  fil principal : attendu 111, lu %d  (%s)\n",
                tl_value, tl_name);
        fprintf(f, "  fil cree      : attendu 222, lu %d  (%s)\n",
                vu_par_le_fil, nom_vu);
        fprintf(f, "  verdict       : %s\n",
                (principal_ok && fil_ok) ? "ISOLE — thread_local fonctionne"
                                         : "PARTAGE — thread_local est inutilisable");
        fclose(f);
    }
    printf("principal %d, fil %d -> %s\n", tl_value, vu_par_le_fil,
           (principal_ok && fil_ok) ? "ISOLE" : "PARTAGE");
    return (principal_ok && fil_ok) ? 0 : 1;
}
